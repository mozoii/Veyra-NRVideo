// QML bridge: streaming (Moonlight / Xbox) responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {

QObject* QmlPlayerBridge::moonlightModel() const {
#ifdef VEYRA_ENABLE_MOONLIGHT
    return impl_->moonlight.get();
#else
    return nullptr;
#endif
}
bool QmlPlayerBridge::moonlightCaptured() const {
#ifdef VEYRA_ENABLE_MOONLIGHT
    return impl_->moonlightCapture && impl_->moonlightCapture->captured();
#else
    return false;
#endif
}
bool QmlPlayerBridge::moonlightStatsVisible() const {
#ifdef VEYRA_ENABLE_MOONLIGHT
    return impl_->moonlightStatsVisible;
#else
    return false;
#endif
}
void QmlPlayerBridge::setMoonlightStatsVisible(bool visible) {
#ifdef VEYRA_ENABLE_MOONLIGHT
    if (impl_->moonlightStatsVisible == visible) return;
    impl_->moonlightStatsVisible = visible;
    emit moonlightUiChanged();
#else
    (void)visible;
#endif
}
void QmlPlayerBridge::openMoonlightDialog() { emit navigate(QStringLiteral("moonlight")); }

// --- Xbox home streaming (unofficial) ------------------------------------------------------------------
QObject* QmlPlayerBridge::xboxModel() const {
#ifdef VEYRA_ENABLE_XBOX
    return impl_->xbox.get();
#else
    return nullptr;
#endif
}
void QmlPlayerBridge::openXboxDialog() { emit navigate(QStringLiteral("xbox")); }
void QmlPlayerBridge::xboxDisconnect() {
#ifdef VEYRA_ENABLE_XBOX
    if (impl_->snapshot.xboxActive) stopPlayback();
#endif
}

#ifdef VEYRA_ENABLE_XBOX
void QmlPlayerBridge::setupXbox() {
    auto& i = *impl_;
    i.xbox = std::make_unique<ui::XboxModel>();
    connect(i.xbox.get(), &ui::XboxModel::notice, this, &QmlPlayerBridge::notice);
    i.xbox->setLaunchHandler([this](ui::XboxModel::Launch launch) -> bool {
        auto& j = *impl_;
        if (!j.videoWindow) return false;
        rememberPosition(true);
        emit mediaOpenRequested();
        j.openingSource = true;
        j.openingSessionId = 0;
        j.sourceLabel = L"Xbox · " + launch.label.toStdWString();
        j.resumeSession = 0;
        const bool pad = launch.desc.gamepad;
        const QString label = launch.label;
        auto request = std::make_shared<source::XboxConnectDesc>(std::move(launch.desc));
        openAfterLivePage(tr("Connecting to Xbox · %1").arg(label), [this, request, pad] {
            auto& k = *impl_;
            if (k.preOpen) k.preOpen();
            k.engine.previewView({});
            k.engine.openXbox(k.videoWindow, std::move(*request), k.options);
            k.openingSessionId = k.engine.snapshot().sessionId;
            refreshSubtitles({});
            // The pad is polled on the UI thread at 250 Hz; the session sends changes and a 33 ms heartbeat.
            if (pad) {
                if (!k.acquirePad(Impl::PadXbox)) veyra::log::warn("xbox-input", "SDL gamepad initialization failed");
                if (!k.xboxTimer) {
                    k.xboxTimer = new QTimer(this);
                    k.xboxTimer->setTimerType(Qt::PreciseTimer);
                    connect(k.xboxTimer, &QTimer::timeout, this, [this] {
                        auto& m = *impl_;
                        if (!m.snapshot.xboxActive) { m.releasePad(Impl::PadXbox); m.xboxTimer->stop(); return; }
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        m.engine.xboxController(m.controller.poll(focused));
                        m.controller.feedback(m.engine.xboxFeedback(), focused);
                    });
                }
                k.xboxTimer->start(4);
            } else {
                if (k.xboxTimer) k.xboxTimer->stop();
                k.releasePad(Impl::PadXbox);
            }
        });
        rememberSource(QStringLiteral("xbox"), label);
        return true;
    });
}

void QmlPlayerBridge::tickXbox() {
    auto& i = *impl_;
    if (!i.xbox) return;
    const auto& s = i.snapshot;
    const bool active = s.xboxActive;
    if (active != i.xboxWasActive) {
        i.xboxWasActive = active;
        i.xbox->updateStream(active, s.xbox);
    } else if (active && ++i.xboxStatsTicks >= 15) {
        i.xboxStatsTicks = 0;
        i.xbox->updateStream(true, s.xbox);
    }
}
#else
void QmlPlayerBridge::tickXbox() {}
#endif

void QmlPlayerBridge::moonlightCapture(bool on) {
#ifdef VEYRA_ENABLE_MOONLIGHT
    auto& i = *impl_;
    if (!i.moonlightCapture) return;
    if (!on) {
        i.moonlightWantCapture = false;
        i.moonlightCapture->capture(false);
        return;
    }
    if (!i.snapshot.moonlightActive || !i.videoWindow) return;
    i.moonlightCapture->setWindow(GetAncestor(i.videoWindow, GA_ROOT));
    // The tick keeps trying until the window is in the foreground (right after connecting it may not be yet).
    i.moonlightWantCapture = !i.moonlightCapture->capture(true);
#else
    (void)on;
#endif
}

void QmlPlayerBridge::moonlightDisconnect() {
#ifdef VEYRA_ENABLE_MOONLIGHT
    moonlightCapture(false);
    if (impl_->snapshot.moonlightActive) stopPlayback();
#endif
}

#ifdef VEYRA_ENABLE_MOONLIGHT
void QmlPlayerBridge::setupMoonlight() {
    auto& i = *impl_;
    i.moonlight = std::make_unique<ui::MoonlightModel>();
    i.moonlightCapture = std::make_unique<ui::MoonlightInputCapture>(i.engine);
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::capturedChanged, this, [this](bool) { emit moonlightUiChanged(); });
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::releaseRequested, this, [this] {
        impl_->moonlightWantCapture = false;
        emit notice(tr("Keyboard and mouse released (click the picture to capture again)"), false);
    });
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::quitRequested, this, [this] { moonlightDisconnect(); });
    connect(i.moonlightCapture.get(), &ui::MoonlightInputCapture::statsRequested, this, [this] { setMoonlightStatsVisible(!moonlightStatsVisible()); });
    connect(i.moonlight.get(), &ui::MoonlightModel::notice, this, &QmlPlayerBridge::notice);
    i.moonlight->setLaunchHandler([this](ui::MoonlightModel::Launch launch) -> bool {
        auto& j = *impl_;
        if (!j.videoWindow) return false;
        rememberPosition(true);
        emit mediaOpenRequested();
        j.openingSource = true;
        j.openingSessionId = 0;
        j.sourceLabel = L"PC · " + launch.label.toStdWString();
        j.resumeSession = 0;
        j.screenFillActive = false;
        const bool pad = (launch.desc.options.gamepadMask & 1) != 0;
        j.moonlightWantCapture = launch.captureInput;
        const QString label = launch.label;
        auto request = std::make_shared<source::MoonlightConnectDesc>(std::move(launch.desc));
        openAfterLivePage(tr("Connecting to %1").arg(label), [this, request, pad] {
            auto& k = *impl_;
            if (k.preOpen) k.preOpen();
            k.engine.previewView({});
            k.engine.openMoonlight(k.videoWindow, std::move(*request), k.options);
            k.openingSessionId = k.engine.snapshot().sessionId;
            refreshSubtitles({});
            // The pad is polled on the UI thread at 250 Hz and sent only when it changes.
            if (pad) {
                if (!k.acquirePad(Impl::PadMoonlight)) veyra::log::warn("moonlight-input", "SDL gamepad initialization failed");
                if (!k.moonlightTimer) {
                    k.moonlightTimer = new QTimer(this);
                    k.moonlightTimer->setTimerType(Qt::PreciseTimer);
                    connect(k.moonlightTimer, &QTimer::timeout, this, [this] {
                        auto& m = *impl_;
                        if (!m.snapshot.moonlightActive) { m.releasePad(Impl::PadMoonlight); m.moonlightTimer->stop(); return; }
                        const bool focused = QGuiApplication::focusWindow() != nullptr;
                        m.engine.moonlightController(m.controller.poll(focused));
                        m.controller.feedback(m.engine.moonlightFeedback(), focused);
                    });
                }
                k.moonlightTimer->start(4);
            } else {
                if (k.moonlightTimer) k.moonlightTimer->stop();
                k.releasePad(Impl::PadMoonlight);
            }
        });
        rememberSource(QStringLiteral("moonlight"), label);
        return true;
    });
}

void QmlPlayerBridge::tickMoonlight() {
    auto& i = *impl_;
    if (!i.moonlight) return;
    const auto& s = i.snapshot;
    const bool active = s.moonlightActive;
    if (active != i.moonlightWasActive) {
        i.moonlightWasActive = active;
        if (!active) {
            i.moonlightWantCapture = false;
            if (i.moonlightCapture) i.moonlightCapture->capture(false);
        }
        i.moonlight->updateStream(active, s.moonlight);
    } else if (active && ++i.moonlightStatsTicks >= 15) {
        i.moonlightStatsTicks = 0;
        i.moonlight->updateStream(true, s.moonlight);
    }
    // Take the keyboard and mouse as soon as the picture is up (and again when the window comes to the front).
    if (active && i.moonlightWantCapture && s.moonlight.state == source::MoonlightStats::State::Streaming) moonlightCapture(true);
}
#else
void QmlPlayerBridge::tickMoonlight() {}
#endif

} // namespace veyra::ui
