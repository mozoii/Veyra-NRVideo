// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The "Xbox stream" side of the UI (unofficial): Microsoft device-code sign-in, the consoles on the account,
// and starting a stream. Network work runs on worker threads and reports back on the UI thread.
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "veyra/source/XboxSessionSource.h"
#include "veyra/xbox/Account.h"

namespace veyra::ui {

class XboxModel final : public QObject {
    Q_OBJECT
    // {signedIn, busy, status, code, verificationUri, selected, gamepad, controllerGuid, streaming, stream{...}, lastLabel}
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    // [{id, name, type, power, selected}]
    Q_PROPERTY(QVariantList consoles READ consoles NOTIFY changed)
public:
    struct Launch {
        source::XboxConnectDesc desc;
        QString label;
    };
    using LaunchFn = std::function<bool(Launch)>;

    explicit XboxModel(QObject* parent = nullptr);
    ~XboxModel() override;

    void setLaunchHandler(LaunchFn handler) { launch_ = std::move(handler); }
    void updateStream(bool active, const source::XboxStats& stats);

    QVariantMap state() const;
    QVariantList consoles() const { return consoles_; }

    Q_INVOKABLE void load();
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void openSignInPage();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool connectStream();
    Q_INVOKABLE bool resumeLast();
    Q_INVOKABLE bool set(const QString& key, const QVariant& value);

    QString lastLabel() const { return lastName_; }

signals:
    void changed();
    void started();
    void notice(const QString& text, bool isError);

private:
    void startJob(const QString& status, std::function<std::function<void()>(const std::atomic<bool>&)> work);
    void loadSettings();
    void saveSettings() const;
    bool launchConsole(const QString& id, const QString& name);

    LaunchFn launch_;
    std::shared_ptr<xbox::Account> account_;
    QVariantList consoles_;
    QString selected_, status_, code_, verificationUri_, lastId_, lastName_, controllerGuid_;
    bool busy_ = false, gamepad_ = true, softwareDecode_ = false, loaded_ = false, streamActive_ = false, signingIn_ = false;
    int videoBitrateKbps_ = 0, decodeBackend_ = 0; // 0 = no b=AS / Auto (D3D12VA), 1 = D3D11VA
    bool useStun_ = false, fullTeredo_ = false;
    int transportEngine_ = 1; // 1 = V1 native (default), 2 = V2 WebView2
    QVariantMap stream_;
    std::string resolutionMode_ = "auto";
    std::atomic<bool> cancel_{false};
    std::jthread worker_;
};

} // namespace veyra::ui
