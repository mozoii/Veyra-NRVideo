#include "veyra/ui/NvidiaVrrProfileController.h"
#include "veyra/Log.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QThread>
#include <memory>
#include <optional>

namespace veyra::ui {
namespace {
using gfx::VrrProfileSnapshot;
QJsonArray values(const std::array<gfx::VrrProfileValue, 2>& items) {
    QJsonArray array;
    for (const auto& v : items) array.append(QJsonObject{{"value", int(v.value)}, {"local", v.local}});
    return array;
}
QByteArray encode(const VrrProfileSnapshot& state) {
    return QJsonDocument(QJsonObject{{"version", 1}, {"executable", QString::fromStdWString(state.executable)},
        {"profile", QString::fromStdWString(state.profileName)}, {"exists", state.exists},
        {"settingCount", qint64(state.settingCount)}, {"settings", values(state.settings)}, {"global", values(state.global)}}).toJson();
}
struct Backup { bool exists = false; std::optional<VrrProfileSnapshot> state; };
Backup readBackup(const QString& path, const QString& executable) {
    Backup result; result.exists = QFileInfo::exists(path);
    if (!result.exists) return result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32768) return result;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || object["version"].toInt(-1) != 1
        || object["executable"].toString() != executable || !object["exists"].isBool()
        || !object["profile"].isString() || object["profile"].toString().isEmpty()
        || object["profile"].toString().size() >= 2048) return result;
    const double count = object["settingCount"].toDouble(-1);
    if (count < 0 || count > UINT32_MAX || count != double(uint32_t(count))) return result;
    VrrProfileSnapshot before;
    before.executable = executable.toStdWString(); before.profileName = object["profile"].toString().toStdWString();
    before.exists = object["exists"].toBool(); before.isolated = true; before.settingCount = uint32_t(count);
    for (const auto* key : {"settings", "global"}) {
        const auto array = object[key].toArray();
        if (array.size() != 2) return result;
        auto& target = key[0] == 's' ? before.settings : before.global;
        for (int k = 0; k < 2; ++k) {
            const auto value = array[k].toObject(); const int number = value["value"].toInt(-1);
            if (number < 0 || number > 2 || value["value"].toDouble(-1) != number || !value["local"].isBool()) return result;
            target[size_t(k)] = {uint32_t(number), value["local"].toBool()};
        }
    }
    if (!before.exists && (before.settingCount || before.settings[0].local || before.settings[1].local)) return result;
    result.state = std::move(before);
    return result;
}
bool saveBackup(const QString& path, const VrrProfileSnapshot& state) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto data = encode(state);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
struct Outcome {
    gfx::VrrProfileResult read, mutation;
    bool inspected = false, didMutate = false, conflict = false, storageError = false, canRestore = false;
};
} // namespace

NvidiaVrrProfileController::NvidiaVrrProfileController(const std::filesystem::path& dataDirectory, QObject* parent)
    : QObject(parent) {
    executable_ = QDir::toNativeSeparators(QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath()).toLower();
    const auto key = QString::fromLatin1(QCryptographicHash::hash(executable_.toUtf8(), QCryptographicHash::Sha256).toHex());
    backupPath_ = QDir(QString::fromStdWString(dataDirectory.wstring())).filePath(QStringLiteral("driver-profiles/vrr-%1.json").arg(key));
}
QString NvidiaVrrProfileController::summary() const {
    if (busy_) return tr("Processing driver profile…");
    if (!checked_) return tr("Not checked");
    if (storageError_) return tr("Cannot save or remove the backup");
    if (error_ == gfx::VrrProfileError::Busy) return tr("Driver profile is in use");
    if (driverStatus_ == -175) return tr("Administrator rights required");
    if (error_ != gfx::VrrProfileError::None) return tr("Driver profile not confirmed");
    if (backupConflict_) return tr("Backup does not match the current profile");
    if (!state_.isolated) return tr("This profile cannot be changed independently");
    return state_.configured() ? tr("Windowed and fullscreen allowed") : tr("Windowed and fullscreen not configured");
}
QString NvidiaVrrProfileController::detail() const {
    if (error_ == gfx::VrrProfileError::Busy) return tr("Another application instance is updating the driver profile. Try again shortly.");
    if (driverStatus_ == -175) return tr("Run this application as administrator and try again. After setup, you can start it normally.");
    if (error_ == gfx::VrrProfileError::Unavailable) return tr("Cannot read NVIDIA driver profiles. Check whether the GPU and driver support this feature.");
    if (error_ == gfx::VrrProfileError::Changed || backupConflict_)
        return tr("The profile or backup changed, so this operation was stopped. Check again, or use Profile Inspector to handle it manually.");
    if (error_ == gfx::VrrProfileError::UnsafeProfile || (readable_ && !state_.isolated))
        return tr("The profile is not exclusive to this application path and may be shared. Use Profile Inspector to configure this application separately.");
    if (storageError_) return tr("Check that the application data directory is writable. Backups are kept in its driver-profiles folder.");
    if (error_ != gfx::VrrProfileError::None)
        return tr("The result has not been confirmed. Check again. The backup is retained; writes will not be retried automatically.");
    if (restartRequired_) return tr("The driver profile was saved. Restart this application manually. Actual activation still depends on the driver and monitor reports.");
    return tr("Configure windowed and fullscreen G-SYNC for this application path, with an option to restore the original settings. G-SYNC must also be enabled on the monitor and in the driver.");
}
void NvidiaVrrProfileController::check() { run(Operation::Check); }
void NvidiaVrrProfileController::repair() { if (canRepair()) run(Operation::Repair); }
void NvidiaVrrProfileController::restore() { if (canRestore()) run(Operation::Restore); }
void NvidiaVrrProfileController::run(Operation operation) {
    if (busy_) return;
    busy_ = true; emit changed();
    const auto executable = executable_, path = backupPath_;
    auto result = std::make_shared<Outcome>();
    // No HWND, graphics device, focus change, automatic restart or per-frame
    // callback. A stalled driver call cannot hold the GUI/render thread.
    auto* worker = QThread::create([executable, path, operation, result] {
        try {
            // Serialize the backup/write pair across player instances too.
            std::unique_ptr<QLockFile> lock;
            if (operation != Operation::Check) {
                if (!QDir().mkpath(QFileInfo(path).absolutePath())) { result->storageError = true; return; }
                lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
                lock->setStaleLockTime(0);
                if (!lock->tryLock(0)) { result->read.error = gfx::VrrProfileError::Busy; return; }
            }
            auto backup = readBackup(path, executable);
            result->read = gfx::inspectVrrProfile(executable.toStdWString());
            result->inspected = true;
            if (!result->read) return;
            const auto before = result->read.snapshot;
            result->conflict = backup.exists && (!backup.state || backup.state->executable != before.executable);
            if (operation == Operation::Repair && !before.configured()) {
                // Preserve the FIRST original values across checks/retries,
                // including a failed write after the backup succeeded.
                if (backup.exists && (!backup.state || *backup.state != before)) result->conflict = true;
                if (result->conflict) return;
                if (!gfx::planVrrProfileRepair(before).allowed) {
                    result->read.error = gfx::VrrProfileError::UnsafeProfile; return;
                }
                if (!backup.exists && !saveBackup(path, before)) { result->storageError = true; return; }
                result->mutation = gfx::repairVrrProfile(before); result->didMutate = true;
                result->read = gfx::inspectVrrProfile(executable.toStdWString());
            } else if (operation == Operation::Restore) {
                if (!backup.state) { result->conflict = true; return; }
                result->mutation = gfx::restoreVrrProfile(*backup.state, before); result->didMutate = true;
                if (result->mutation && !QFile::remove(path)) result->storageError = true;
                result->read = gfx::inspectVrrProfile(executable.toStdWString());
            }
            backup = readBackup(path, executable);
            if (backup.state && result->read) {
                const auto plan = gfx::planVrrProfileRestore(*backup.state, result->read.snapshot);
                // An already-restored record still needs a cleanup action,
                // e.g. after its removal failed or a repair never saved.
                result->canRestore = plan.allowed;
                result->conflict = !plan.allowed && *backup.state != result->read.snapshot;
            }
        } catch (...) { result->read.error = gfx::VrrProfileError::Driver; }
    });
    connect(worker, &QThread::finished, this, [this, result, operation] {
        checked_ = true; readable_ = result->inspected && bool(result->read); state_ = result->read.snapshot;
        const auto& status = result->didMutate && !result->mutation ? result->mutation : result->read;
        error_ = status.error; driverStatus_ = status.driverStatus;
        backupConflict_ = result->conflict; storageError_ = result->storageError;
        restoreAvailable_ = result->canRestore;
        if (result->didMutate && result->mutation && result->mutation.changed) restartRequired_ = true;
        busy_ = false; emit changed();
        log::info("vrr-profile", "operation=" + std::to_string(int(operation)) + " status=" + std::to_string(driverStatus_)
            + " error=" + std::to_string(int(error_)) + " changed=" + std::to_string(result->mutation.changed));
        if (operation != Operation::Check) emit notice(summary() + QStringLiteral(". ") + detail(),
            error_ != gfx::VrrProfileError::None || storageError_ || backupConflict_);
    });
    // Value copies only; destroying the controller cancels its completion
    // callback without waiting for a vendor call at shutdown.
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
} // namespace veyra::ui
