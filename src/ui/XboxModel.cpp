// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/XboxModel.h"
#include "veyra/ui/UiLanguage.h"

#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSaveFile>
#include <QUrl>
#include <chrono>

#include "veyra/Log.h"
#include "veyra/xbox/ResolutionMode.h"
#include "veyra/xbox/StreamApi.h"

namespace veyra::ui {
namespace {

QString qs(const std::string& text) { return QString::fromUtf8(text.data(), qsizetype(text.size())); }

QString describe(const std::exception& e) {
    if (const auto* service = dynamic_cast<const xbox::ServiceError*>(&e)) {
        const std::wstring xerr = xbox::describeXboxError(service->body);
        if (!xerr.empty()) return veyra::ui::i18n::text(xerr);
        if (service->status == 400 || service->status == 401 || service->status == 403) return QObject::tr("Xbox sign-in has expired; sign in again.");
        return QObject::tr("The Xbox service returned an error (HTTP %1).").arg(service->status);
    }
    if (dynamic_cast<const xbox::NetworkError*>(&e)) return QObject::tr("Cannot reach Xbox services; check the network. (%1)").arg(qs(e.what()));
    return QObject::tr("Something went wrong: %1").arg(qs(e.what()));
}

QString powerText(const std::string& power) {
    if (power == "On") return QObject::tr("Power on");
    if (power == "ConnectedStandby") return QObject::tr("Asleep (can be woken)");
    if (power == "SystemUpdate") return QObject::tr("Updating");
    if (power == "Off") return QObject::tr("Power off");
    return qs(power);
}

QString typeText(const std::string& type) {
    if (type == "XboxSeriesX") return QStringLiteral("Xbox Series X");
    if (type == "XboxSeriesS") return QStringLiteral("Xbox Series S");
    if (type == "XboxOne") return QStringLiteral("Xbox One");
    if (type == "XboxOneS") return QStringLiteral("Xbox One S");
    if (type == "XboxOneX") return QStringLiteral("Xbox One X");
    return qs(type);
}

QString settingsPath() { return QString::fromStdWString((xbox::dataDirectory() / L"settings.json").wstring()); }

QVariantList resolutionLabels() {
    QVariantList out;
    for (const auto& d : xbox::kResolutionModes) {
        out << QVariantMap{{"id", QString::fromLatin1(d.name)}, {"label", QString::fromLatin1(d.label)}};
    }
    return out;
}

} // namespace

XboxModel::XboxModel(QObject* parent) : QObject(parent) {
    account_ = std::make_shared<xbox::Account>(xbox::makeWinHttpTransport(), xbox::dataDirectory() / L"account.bin");
}

XboxModel::~XboxModel() { cancel_ = true; }

void XboxModel::loadSettings() {
    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    lastId_ = o.value("lastConsole").toString();
    lastName_ = o.value("lastName").toString();
    gamepad_ = o.value("gamepad").toBool(true);
    softwareDecode_ = o.value("softwareDecode").toBool(false);
    const QString saved = o.value("resolutionMode").toString();
    const auto parsed = xbox::resolutionModeFromName(saved.toStdString(), xbox::ResolutionMode::Auto);
    resolutionMode_ = xbox::resolutionModeName(parsed);
}

void XboxModel::saveSettings() const {
    std::error_code ec;
    std::filesystem::create_directories(xbox::dataDirectory(), ec);
    QSaveFile file(settingsPath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(QJsonObject{{"lastConsole", lastId_}, {"lastName", lastName_}, {"gamepad", gamepad_}, {"softwareDecode", softwareDecode_}, {"resolutionMode", QString::fromStdString(resolutionMode_)}}).toJson());
    file.commit();
}

QVariantMap XboxModel::state() const {
    return QVariantMap{
        {"signedIn", account_->signedIn()}, {"busy", busy_}, {"status", status_}, {"code", code_},
        {"verificationUri", verificationUri_}, {"signingIn", signingIn_}, {"selected", selected_}, {"gamepad", gamepad_},
        {"streaming", streamActive_}, {"stream", stream_}, {"lastLabel", lastName_}, {"softwareDecode", softwareDecode_},
        {"resolutionMode", QString::fromStdString(resolutionMode_)}, {"resolutionLabels", resolutionLabels()},
    };
}

void XboxModel::startJob(const QString& status, std::function<std::function<void()>(const std::atomic<bool>&)> work) {
    if (busy_) { status_ = tr("The previous action is still in progress; wait a moment or click Cancel first."); emit changed(); return; }
    busy_ = true;
    cancel_ = false;
    status_ = status;
    emit changed();
    if (worker_.joinable()) worker_.join();
    worker_ = std::jthread([this, work = std::move(work)] {
        std::function<void()> done;
        try {
            done = work(cancel_);
        } catch (const std::exception& e) {
            const QString text = describe(e);
            done = [this, text] { status_ = text; };
        }
        QMetaObject::invokeMethod(this, [this, done = std::move(done)] {
            busy_ = false;
            if (done) done();
            emit changed();
        }, Qt::QueuedConnection);
    });
}

void XboxModel::cancel() {
    cancel_ = true;
    if (busy_) { status_ = tr("Cancelling…"); emit changed(); }
}

void XboxModel::load() {
    if (!loaded_) { loadSettings(); loaded_ = true; }
    if (account_->signedIn() && consoles_.isEmpty()) refresh();
    emit changed();
}

void XboxModel::signIn() {
    signingIn_ = true;
    code_.clear();
    startJob(tr("Requesting a sign-in code from Microsoft…"), [this](const std::atomic<bool>& cancel) -> std::function<void()> {
        const xbox::DeviceCode code = account_->beginSignIn();
        const QString user = qs(code.userCode), uri = qs(code.verificationUri);
        QMetaObject::invokeMethod(this, [this, user, uri] {
            code_ = user;
            verificationUri_ = uri;
            status_ = tr("On your phone or browser, open %1, enter the code above and sign in with your Xbox (Microsoft) account. This continues automatically once you have signed in.").arg(uri);
            emit changed();
        }, Qt::QueuedConnection);
        int interval = code.intervalSeconds;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(code.expiresInSeconds);
        for (;;) {
            for (int i = 0; i < interval * 10; ++i) {
                if (cancel.load()) return [this] { signingIn_ = false; code_.clear(); status_ = tr("Sign-in cancelled."); };
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (std::chrono::steady_clock::now() > deadline) return [this] { signingIn_ = false; code_.clear(); status_ = tr("The sign-in code has expired; click Sign in again."); };
            std::string detail;
            const xbox::SignInPoll result = account_->pollSignIn(code, &detail);
            if (result == xbox::SignInPoll::Pending) continue;
            if (result == xbox::SignInPoll::SlowDown) { interval += 5; continue; }
            if (result == xbox::SignInPoll::Done) break;
            const QString why = result == xbox::SignInPoll::Expired ? tr("The sign-in code has expired; click Sign in again.")
                              : result == xbox::SignInPoll::Declined ? tr("Sign-in was refused.")
                              : tr("Sign-in failed (%1).").arg(qs(detail));
            return [this, why] { signingIn_ = false; code_.clear(); status_ = why; };
        }
        // Signed in: prove the streaming sign-in works and read the consoles straight away.
        const xbox::StreamingAccess access = account_->streamingAccess();
        const auto list = xbox::StreamApi(xbox::makeWinHttpTransport(&cancel), access.host, access.gsToken).consoles();
        QVariantList consoles;
        for (const auto& c : list)
            consoles << QVariantMap{{"id", qs(c.serverId)}, {"name", qs(c.name)}, {"type", typeText(c.consoleType)}, {"power", powerText(c.powerState)}};
        return [this, consoles] {
            signingIn_ = false;
            code_.clear();
            consoles_ = consoles;
            if (selected_.isEmpty() && !consoles_.isEmpty()) selected_ = consoles_.front().toMap().value("id").toString();
            status_ = consoles_.isEmpty() ? tr("Signed in, but no console was found under this account. Sign in on the console with the same account and enable remote features under Settings → Devices & connections → Remote features.")
                                          : tr("Signed in.");
            emit notice(tr("Signed in to Xbox"), false);
        };
    });
}

void XboxModel::openSignInPage() {
    QDesktopServices::openUrl(QUrl(verificationUri_.isEmpty() ? QStringLiteral("https://www.microsoft.com/link") : verificationUri_));
}

void XboxModel::signOut() {
    if (busy_) return;
    account_->signOut();
    consoles_.clear();
    selected_.clear();
    status_ = tr("Signed out; the sign-in data saved on this PC was deleted.");
    emit changed();
}

void XboxModel::refresh() {
    if (!account_->signedIn()) { emit changed(); return; }
    startJob(tr("Reading the host list…"), [this](const std::atomic<bool>& cancel) -> std::function<void()> {
        const xbox::StreamingAccess access = account_->streamingAccess();
        const auto list = xbox::StreamApi(xbox::makeWinHttpTransport(&cancel), access.host, access.gsToken).consoles();
        QVariantList consoles;
        for (const auto& c : list)
            consoles << QVariantMap{{"id", qs(c.serverId)}, {"name", qs(c.name)}, {"type", typeText(c.consoleType)}, {"power", powerText(c.powerState)}};
        return [this, consoles] {
            consoles_ = consoles;
            bool known = false;
            for (const QVariant& c : std::as_const(consoles_)) known = known || c.toMap().value("id").toString() == selected_;
            if (!known) selected_ = consoles_.isEmpty() ? QString() : (lastId_.isEmpty() ? consoles_.front().toMap().value("id").toString() : lastId_);
            status_ = consoles_.isEmpty() ? tr("No console was found under this account. Sign in on the console with the same account and enable remote features.") : QString();
        };
    });
}

void XboxModel::select(const QString& id) {
    if (selected_ == id) return;
    selected_ = id;
    emit changed();
}

bool XboxModel::set(const QString& key, const QVariant& value) {
    if (key == "gamepad") { gamepad_ = value.toBool(); saveSettings(); emit changed(); return true; }
    if (key == "softwareDecode") { softwareDecode_ = value.toBool(); saveSettings(); emit changed(); return true; }
    if (key == "resolutionMode") {
        const QString raw = value.toString().trimmed().toLower();
        const auto parsed = xbox::resolutionModeFromName(raw.toStdString(), xbox::ResolutionMode::Auto);
        // Reject unknown names instead of silently mapping them onto Auto.
        bool known = false;
        for (const auto& d : xbox::kResolutionModes) known = known || (d.name == raw);
        if (!known) return false;
        resolutionMode_ = xbox::resolutionModeName(parsed);
        saveSettings(); emit changed(); return true;
    }
    return false;
}

bool XboxModel::launchConsole(const QString& id, const QString& name) {
    if (!account_->signedIn()) { status_ = tr("Sign in to your Xbox account first."); emit changed(); return false; }
    if (!launch_ || id.isEmpty()) return false;
    Launch launch;
    launch.desc.account = account_;
    launch.desc.serverId = id.toStdString();
    launch.desc.consoleName = name.toStdString();
    launch.desc.gamepad = gamepad_;
    launch.desc.softwareDecode = softwareDecode_;
    launch.desc.resolutionMode = xbox::resolutionModeFromName(resolutionMode_);
    launch.label = name.isEmpty() ? QStringLiteral("Xbox") : name;
    if (!launch_(std::move(launch))) { status_ = tr("Could not start streaming; see the log."); emit changed(); return false; }
    lastId_ = id;
    lastName_ = name;
    saveSettings();
    status_ = tr("Connecting to the host… (waking a sleeping host takes a moment)");
    emit changed();
    emit started();
    return true;
}

bool XboxModel::connectStream() {
    if (busy_) return false;
    QString name;
    for (const QVariant& c : std::as_const(consoles_)) if (c.toMap().value("id").toString() == selected_) name = c.toMap().value("name").toString();
    return launchConsole(selected_, name);
}

bool XboxModel::resumeLast() {
    if (!loaded_) { loadSettings(); loaded_ = true; }
    if (lastId_.isEmpty()) return false;
    return launchConsole(lastId_, lastName_);
}

void XboxModel::updateStream(bool active, const source::XboxStats& s) {
    QVariantMap now;
    if (active) {
        const char* names[] = {"idle", "starting", "connecting", "streaming", "ended", "failed"};
        now = QVariantMap{
            {"state", names[size_t(s.state)]}, {"message", veyra::ui::i18n::text(s.message)}, {"codec", "H.264"},
            {"hardware", s.hardwareDecode}, {"width", int(s.width)}, {"height", int(s.height)},
            {"receivedFps", s.receivedFps}, {"decodedFps", s.decodedFps}, {"videoMbps", s.videoMbps},
            {"decodeMs", s.decodeMs}, {"rttMs", s.rttMs}, {"dropped", double(s.dropped)},
            {"decodeErrors", double(s.decodeErrors)}, {"keyframes", double(s.keyframeRequests)},
        };
    }
    if (now == stream_ && active == streamActive_) return;
    stream_ = now;
    streamActive_ = active;
    emit changed();
}

} // namespace veyra::ui
