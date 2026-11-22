// QML bridge: FrameGeneration responsibilities.
#include "veyra/engine/DisplayFgBudget.h"
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- frame generation --------------------------------------------------------
QString QmlPlayerBridge::fgBackendName() const {
    switch(settings().frameGenerationBackend){
    case engine::FrameGenerationBackend::Dlss:return QStringLiteral("dlss");
    case engine::FrameGenerationBackend::XeSS:return QStringLiteral("xess");
    case engine::FrameGenerationBackend::Fsr:return QStringLiteral("fsr3");
    case engine::FrameGenerationBackend::Fsr4:return QStringLiteral("fsr4");
    case engine::FrameGenerationBackend::Vfg:return QStringLiteral("vfg");
    case engine::FrameGenerationBackend::Anr:return QStringLiteral("anr");
    case engine::FrameGenerationBackend::AmdFrc:return QStringLiteral("amdfrc");
    }
    return {};
}
QVariantList QmlPlayerBridge::fgBackendChoices() const {
    QVariantList choices = {
        QVariantMap{{"id","anr"},{"label",tr("ANR FG (Very poor quality) · Local experiment")},{"danger",true}},
        QVariantMap{{"id","dlss"},{"label",tr("DLSS Frame Generation")}},
        QVariantMap{{"id","xess"},{"label",tr("Intel XeSS · experimental")}},
        QVariantMap{{"id","fsr3"},{"label",tr("FSR 3.1 · 2X")}},
        QVariantMap{{"id","fsr4"},{"label",tr("FSR 4 ML · RX 9000")}},
        QVariantMap{{"id","amdfrc"},{"label",tr("AMD FRC · video frame interpolation · 2X")}},
        QVariantMap{{"id","vfg"},{"label",tr("NVIDIA VFG 👑 (Top-tier frame generation quality) · RTX 30 (experimental) / 40 / 50")},{"gold",true}}
    };
    const auto caps = effectCapabilities();
    for (auto& choice : choices) {
        auto row = choice.toMap();
        const auto cap = caps.value(row.value("id").toString()).toMap();
        row["disabled"] = !cap.value("available").toBool();
        row["note"] = cap.value("reason");
        choice = row;
    }
    return choices;
}
QString QmlPlayerBridge::fgProviderText() const {
    if(fgBackendName()==QLatin1String("anr"))return tr("Apex Neural Rendering · 2X–6X · Estimated video inputs; quality and speed depend on actual results");
    const auto& s=impl_->snapshot;
    if(fgBackendName()==QLatin1String("vfg")){
        if(!vfgHardwarePossible())return tr("VFG requires NVIDIA RTX 30 / 40 / 50 (RTX 30 compatibility is experimental)");
        if(!effectCapabilities().value("vfg").toMap().value("available").toBool())return tr("VFG runtime components were not found. Check their paths on the Components page.");
        if(effectGpu().ampere)return tr("RTX 30 VFG compatibility is experimental and reuses existing models; 2X–8X, with image quality and speed requiring hardware validation");
        return tr("VFG supports 2X–8X and Low / Medium / High quality. 6X–8X are experimental; higher multipliers and quality take more processing time.");
    }
    if(engine::fsrFrameGeneration(s.applied.frameGenerationBackend)&&s.applied.multiplier>1&&!s.fsrProviderVersion.empty())
        return tr("Actually running: FSR %1 · 2X").arg(QString::fromStdString(s.fsrProviderVersion));
    if(fgBackendName()==QLatin1String("amdfrc"))return tr("Video frame interpolation built into the AMD driver (AMF FRC); it estimates motion from the two pictures only, fixed at 2X");
    if(fgBackendName()==QLatin1String("fsr4"))return tr("FSR 4 ML needs a supported RX 9000 and driver and switches to FSR 3.1 when unavailable; not yet verified on an actual RX 9000");
    if(fgBackendName()==QLatin1String("fsr3"))return tr("FSR 3.1 cross-vendor frame generation · up to 2X");
    if(fgBackendName()==QLatin1String("xess"))
        return fgMaxMultiplier()<4?tr("The current XeSS module does not match the 3X / 4X unlock version; 2X works. Check the version on the components page"):
            tr("2X is the native path; 3X / 4X are experimental unlocks. See diagnostics for failures");
    return tr("NVIDIA RTX frame generation; the multiplier depends on what the runtime supports");
}
// XeSS ceiling (SettingsWindow): 4X with the audited unlock provider, else 2X,
// or a running session's reported interpolation count above 2X.
int xessCeiling(const engine::PlayerSnapshot& s) {
    static const bool audited = gfx::XessMfgUnlock::providerIsAudited(
        (runtime::localDataDirectory() / L"intel" / L"experimental" / L"libxess_fg.dll").wstring());
    int cap = audited ? 4 : 2;
    // A running context reports the count it was CREATED with (3X -> 2), not
    // what the provider allows: it may raise the ceiling, never lower it.
    // Lowering it made 4X vanish after picking 3X until the backend was reset.
    // The highest ceiling any context has reported this run stays offered:
    // a later context created at 2X reports 1 and must not hide 3X/4X again.
    static int seen = 0;
    if (s.xessMaxInterpolatedFrames > 1) seen = std::max(seen, std::clamp(s.xessMaxInterpolatedFrames + 1, 2, 4));
    return std::max(cap, seen);
}
void QmlPlayerBridge::setFgBackendName(const QString& value) {
    if(value!=QLatin1String("anr")&&value!=QLatin1String("dlss")&&value!=QLatin1String("xess")&&value!=QLatin1String("fsr3")&&value!=QLatin1String("fsr4")&&value!=QLatin1String("vfg")&&value!=QLatin1String("amdfrc")){emit notice(tr("Unknown frame generation backend"),true);return;}
    const auto availability = effectCapabilities().value(value).toMap();
    if (!availability.value("available").toBool()) { emit notice(availability.value("reason").toString(), true); emit settingsChanged(); return; }
    auto s = settings();
    const auto want = value == QLatin1String("anr") ? engine::FrameGenerationBackend::Anr
                    : value == QLatin1String("xess") ? engine::FrameGenerationBackend::XeSS
                    : value == QLatin1String("fsr3") ? engine::FrameGenerationBackend::Fsr
                    : value == QLatin1String("fsr4") ? engine::FrameGenerationBackend::Fsr4
                    : value == QLatin1String("vfg") ? engine::FrameGenerationBackend::Vfg
                    : value == QLatin1String("amdfrc") ? engine::FrameGenerationBackend::AmdFrc
                    : engine::FrameGenerationBackend::Dlss;
    if (s.frameGenerationBackend == want) return;
    if(want==engine::FrameGenerationBackend::Vfg&&(!vfgHardwarePossible()||!pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring()))){
        emit notice(!vfgHardwarePossible()?tr("VFG requires NVIDIA RTX 30 / 40 / 50 (RTX 30 compatibility is experimental)"):tr("VFG runtime components were not found. Check their paths on the Components page."),true);emit settingsChanged();return;
    }
    if (want == engine::FrameGenerationBackend::Fsr4 && !fsr4Possible()) {
        // Keep the previous choice; the menu snaps back to it.
        emit notice(tr("FSR 4 ML frame generation only supports AMD RX 9000 GPUs; this GPU cannot use it, so the current frame generation is kept"), true);
        emit settingsChanged();
        return;
    }
    s.frameGenerationBackend = want;
    // A DLSS multiplier above XeSS's ceiling used to make the switch fail
    // silently in effect (the backend stayed DLSS). Clamp to the ceiling instead.
    auto& chain = impl_->chain;
    const auto beforeMultiplier = chain.fgMultiplier;
    const int cap = want == engine::FrameGenerationBackend::XeSS ? xessCeiling(impl_->snapshot)
                  : engine::twoXFrameGeneration(want) ? 2 : want==engine::FrameGenerationBackend::Vfg?8:6;
    chain.fgMultiplier=normalizeFgMultiplier(chain.fgMultiplier,want,uint32_t(cap));
    const bool clamped = chain.fgMultiplier != beforeMultiplier;
    if(!impl_->commit(s)){
        chain.fgMultiplier = beforeMultiplier;
        emit notice(tr("The frame generation backend change was not accepted; previous setting kept"),true);return;
    }
    if (clamped) emit notice(tr("The selected frame-generation backend adjusted the multiplier to %1X.").arg(chain.fgMultiplier), false);
    emit settingsChanged();
    emitChainChanged();
}

int QmlPlayerBridge::fgMaxMultiplier() const {
    // AppShell/SettingsWindow rules. Capabilities report GENERATED frames, so the
    // multiplier ceiling is that count + 1 (DLSS MultiFrameCountMax 5 = 6X on
    // Blackwell, 1 = 2X on Ada). Unknown DLSS capability offers the full list; a
    // new context validates it. XeSS: 4X with the audited unlock provider, else
    // 2X, or what a running session already reported above 2X.
    const auto& s = impl_->snapshot;
    if(settings().frameGenerationBackend==engine::FrameGenerationBackend::Anr)
        return s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Anr&&s.fgCapabilityKnown&&s.fgMultiFrameMax>0?std::clamp(s.fgMultiFrameMax+1,2,6):6;
    if(settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg)return 8;
    if(engine::twoXFrameGeneration(settings().frameGenerationBackend))return 2;
    if (settings().frameGenerationBackend == engine::FrameGenerationBackend::XeSS)
        return xessCeiling(s);
    if (s.fgCapabilityKnown && s.fgMultiFrameMax > 0) return std::clamp(s.fgMultiFrameMax + 1, 2, 6);
    return 6;
}

QVariantList QmlPlayerBridge::fgMultiplierChoices() const {
    // Only multipliers the chosen backend supports are offered: DLSS reaches 6X on
    // capable hardware, XeSS stops at 4X. Offering a value the provider would
    // refuse is the same lie as a dead control.
    const bool xess = settings().frameGenerationBackend
                      == engine::FrameGenerationBackend::XeSS;
    const int ceiling = xess ? std::min(fgMaxMultiplier(), 4) : fgMaxMultiplier();
    QVariantList out;
    const auto choices=settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):settings().frameGenerationBackend==engine::FrameGenerationBackend::Anr?std::span<const uint32_t>(engine::kAnrMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    for (const auto choice : choices) {
        const int m = int(choice);
        if (m < 2 || m > ceiling) continue;
        QVariantMap item;
        item["id"] = QString::number(m);
        item["label"] = QString::number(m) + QStringLiteral("X");
        out << item;
    }
    return out;
}

int QmlPlayerBridge::vfgQuality()const{return int(settings().vfgQuality);}
void QmlPlayerBridge::setVfgQuality(int value){
    if(value<0||uint32_t(value)>=engine::kVfgQualityCount)return;auto s=settings();if(s.vfgQuality==uint32_t(value))return;
    s.vfgQuality=uint32_t(value);
    if(!impl_->commit(s)){emit notice(tr("VFG quality was not accepted; the previous setting was kept."),true);return;}
    emit settingsChanged();emitChainChanged();
}

bool QmlPlayerBridge::fgStrict() const { return impl_->chain.fgStrictAdmission; }
void QmlPlayerBridge::setFgStrict(bool value) {
    if (impl_->chain.fgStrictAdmission == value) return;
    impl_->chain.fgStrictAdmission = value;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain.fgStrictAdmission = !value; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged();
    emitChainChanged();
}

// The low-latency queue is the presentation queue (1.4.4 PresentationSettings.enabled),
// not EnhancementSettings::lowLatency, which means "NR before SR".
bool QmlPlayerBridge::fgLowQueue() const { return impl_->presentation.enabled; }
void QmlPlayerBridge::setFgLowQueue(bool value) {
    if (impl_->presentation.enabled == value) return;
    impl_->presentation.enabled = value;
    if(nvidiaLowLatencyAvailable())impl_->presentation.nvidiaLowLatency=value?engine::NvidiaLowLatency::On:engine::NvidiaLowLatency::Off;
    applyPresentation();
}
int QmlPlayerBridge::nvidiaLowLatency()const{return int(impl_->presentation.nvidiaLowLatency);}
bool QmlPlayerBridge::nvidiaLowLatencyAvailable()const{return primaryGpuVendor()==0x10de;}
void QmlPlayerBridge::setNvidiaLowLatency(int value){
    const bool nvidia=nvidiaLowLatencyAvailable();
    if(value<0||value>2||(!nvidia&&value!=0))return;
    // Legacy UI value 2 migrates to On; no UI/API request can enable Boost.
    auto p=impl_->presentation;p.nvidiaLowLatency=engine::savedNvidiaLowLatency(value);
    engine::syncNvidiaLowLatencyQueue(p,nvidia);
    if(p==impl_->presentation)return;
    impl_->presentation=p;applyPresentation();
}

// --- design gap A1-A5: flow, cadence, presentation ----------------------------
int QmlPlayerBridge::hdrOutputMode()const{return int(settings().hdrOutputMode);}
void QmlPlayerBridge::setHdrOutputMode(int value){if(value<0||value>1)return;auto s=settings();if(int(s.hdrOutputMode)==value)return;s.hdrOutputMode=engine::HdrOutputMode(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::fgMotionSource()const{return engine::motionUsesFlow(settings().fgMotion,settings().frameGenerationBackend)?1:0;}
void QmlPlayerBridge::setFgMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.fgMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::srMotionSource()const{return engine::motionUsesFlow(settings().srMotion)?1:0;}
void QmlPlayerBridge::setSrMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.srMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::nrMotionSource()const{return engine::motionUsesFlow(settings().nrMotion)?1:0;}
void QmlPlayerBridge::setNrMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.nrMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::flowQuality() const { return int(settings().flow); }
void QmlPlayerBridge::setFlowQuality(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings();
    if (int(s.flow) == value) return;
    s.flow = static_cast<engine::FlowQuality>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept the motion estimation quality; previous setting kept"), true); return; }
    emit settingsChanged();
}
bool QmlPlayerBridge::amdFlowHalf() const { return settings().amdFlowHalfResolution; }
void QmlPlayerBridge::setAmdFlowHalf(bool value) {
    auto s = settings();
    if (s.amdFlowHalfResolution == value) return;
    s.amdFlowHalfResolution = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept AMD performance mode; previous setting kept"), true); return; }
    emit settingsChanged();
}
bool QmlPlayerBridge::edgeAegis() const { return settings().fgMotionRefine; }
void QmlPlayerBridge::setEdgeAegis(bool value) {
    auto s = settings();
    if (s.fgMotionRefine == value) return;
    s.fgMotionRefine = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept Edge Aegis; previous setting kept"), true); return; }
    impl_->prefs[QStringLiteral("edgeAegis")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "edge aegis not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::dlssgLite() const { return settings().dlssgLite; }
void QmlPlayerBridge::setDlssgLite(bool value) {
    auto s = settings();
    if (s.dlssgLite == value) return;
    s.dlssgLite = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept the DLSS performance mode; the previous setting is kept"), true); return; }
    impl_->prefs[QStringLiteral("dlssgPerformance")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "DLSS performance mode not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::vfgAutoRegulate() const { return settings().vfgAutoRegulate; }
void QmlPlayerBridge::setVfgAutoRegulate(bool value) {
    auto s = settings();
    if (s.vfgAutoRegulate == value) return;
    s.vfgAutoRegulate = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept VFG auto regulation; the previous setting is kept"), true); return; }
    impl_->prefs[QStringLiteral("vfgAutoRegulate")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "VFG auto regulation not saved");
    emit settingsChanged();
}
// Ported from PushFPS (GPL-3.0-or-later): adaptive multiplier, frame pacing repair and Live
// HUD Lock. The first two only change what the engine loop reads per pair; HUD Lock
// rebuilds the graph (EffectChain.cpp) because its passes exist only when it is on.
bool QmlPlayerBridge::fgAdaptive() const { return settings().fgAdaptive; }
void QmlPlayerBridge::setFgAdaptive(bool value) {
    auto s = settings();
    if (s.fgAdaptive == value) return;
    s.fgAdaptive = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept the adaptive multiplier; the previous setting is kept"), true); return; }
    impl_->prefs[QStringLiteral("fgAdaptive")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "adaptive multiplier not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::pacingRepair() const { return settings().pacingRepair; }
void QmlPlayerBridge::setPacingRepair(bool value) {
    auto s = settings();
    if (s.pacingRepair == value) return;
    s.pacingRepair = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept frame pacing repair; the previous setting is kept"), true); return; }
    impl_->prefs[QStringLiteral("pacingRepair")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "frame pacing repair not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::hudLock() const { return settings().hudLock; }
void QmlPlayerBridge::setHudLock(bool value) {
    auto s = settings();
    if (s.hudLock == value) return;
    s.hudLock = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine did not accept auto HUD lock; the previous setting is kept"), true); return; }
    impl_->prefs[QStringLiteral("hudLock")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "HUD lock not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::dlssgLiteAvailable() const {
    static const bool present = ngx::dlssgLiteRuntimePresent(runtime::localRuntimeDirectory());
    return present;
}
QString QmlPlayerBridge::dlssgLiteHint() const {
    if (!dlssgLiteAvailable()) return tr("This package does not include the DLSS Frame Generation 310.9.16 runtime the performance mode needs");
    const auto& s = impl_->snapshot;
    if (s.running && s.dlssgLiteState == 3) return tr("RTX 50 series only for now: the RTX 40/30 multi-frame unlock works with the standard runtime only");
    if (s.running && s.dlssgLiteState == 1) return tr("Active: NVIDIA DLSS Frame Generation performance mode (beta)");
    return tr("NVIDIA's new performance mode (beta): frame generation uses less GPU and holds high multipliers better, possibly at slightly lower quality; export keeps the standard mode");
}
int QmlPlayerBridge::contentRate() const { return int(engine::currentContentRate(settings().content)); }
void QmlPlayerBridge::setContentRate(int value) {
    if (value < 0 || value > int(engine::ContentRate::Capture60To30)) return;
    value=int(engine::currentContentRate(static_cast<engine::ContentRate>(value)));
    auto s = settings();
    if (int(s.content) == value) return;
    s.content = static_cast<engine::ContentRate>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("The engine rejected the duplicate-removal setting. The previous setting was kept."), true); return; }
    // Shared cadence stays in shell preferences and can be included in presets.
    impl_->prefs[QStringLiteral("contentRate")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "content cadence not saved");
    emit settingsChanged();
}

int QmlPlayerBridge::displaySync() const { return int(impl_->presentation.display); }
bool QmlPlayerBridge::fullscreenMemorySafe() const { return !impl_->prefBool("fullscreenMemoryProtection",false)||!impl_->snapshot.vramFullscreenUnsafe; }
void QmlPlayerBridge::setPresentationFullscreen(bool value) {
    if(impl_->presentation.fullscreen==value)return;
    impl_->presentation.fullscreen=value;
    impl_->engine.requestPresentation(impl_->presentation);
    emit presentationChanged();
}
double QmlPlayerBridge::previewAspect() const {
    const int mode=impl_->aspectMode;
    auto view=impl_->engine.previewView();
    view.mode=mode;
    view.rotationDegrees=impl_->snapshot.sourceRotationDegrees;
    view.displayAspect=impl_->snapshot.sourceDisplayAspect;
    const auto e=impl_->snapshot.metrics.resolution.output;
    if(mode==1&&e.height)return view.aspect(float(e.width),float(e.height));
    return view.aspect(float(impl_->snapshot.sourceWidth),float(impl_->snapshot.sourceHeight));
}
void QmlPlayerBridge::setDisplaySync(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.display) == value) return;
    impl_->presentation.display = static_cast<engine::DisplaySync>(value);
    applyPresentation();
}
bool QmlPlayerBridge::variableRefresh() const { return impl_->presentation.variableRefresh; }
void QmlPlayerBridge::setVariableRefresh(bool value) {
    if (impl_->presentation.variableRefresh == value) return;
    impl_->presentation.variableRefresh = value;
    // Turning it on reads the monitor again: the driver setting may have changed.
    if (value) impl_->engine.displayStates().invalidate("vrr-switch-on");
    applyPresentation();
}
int QmlPlayerBridge::vrrState() const { return impl_->vrrState; }
int QmlPlayerBridge::displayHdr() const { return impl_->displayHdr; }
double QmlPlayerBridge::displayRefreshHz() const { return impl_->displayRefreshHz; }
bool QmlPlayerBridge::displayChecking() const { return impl_->displayChecking; }
void QmlPlayerBridge::recheckDisplay() {
    // Every stored display state becomes stale; the next status update (or the
    // presenter's next loop) queries the shown monitor in the background.
    auto& i = *impl_;
    i.displayRecheckGeneration = i.engine.displayStates().invalidate("manual");
    i.displayRecheckPending = true;
    i.displayRecheckSince = std::chrono::steady_clock::now();
    if (!i.displayChecking) { i.displayChecking = true; emit perfChanged(); }
}
QString QmlPlayerBridge::vrrHint() const { return vrrHintText(impl_->vrrState); }
QString QmlPlayerBridge::vrrSummary() const { return vrrSummaryText(impl_->vrrState); }
// The signal card's short form of vrrHintText.
QString QmlPlayerBridge::vrrSummaryText(int level) const {
    switch (level) {
    case 0: return tr("Cannot detect");
    case 1: return tr("Not supported");
    case 2: return tr("Unavailable now");
    case 3: return impl_->presentation.variableRefresh && impl_->snapshot.running ? tr("Requested") : tr("Available");
    default: return tr("Active");
    }
}
QString QmlPlayerBridge::vrrHintText(int level) const {
    // The Variable Refresh Rate switch is available (not grey) from level 2: the monitor
    // can do VRR even if the driver setting is still off.
    const bool on = impl_->presentation.variableRefresh, running = impl_->snapshot.running;
    switch (level) {
    case 0: return tr("Cannot detect whether the display supports it (NVIDIA GPUs only for now)");
    case 1: return tr("This display does not support variable refresh");
    case 2: return tr("The display supports VRR, but it is unavailable in the current display mode. Check the connection, refresh rate and driver settings.");
    case 3:
        if (!on) return tr("The display refreshes when each frame arrives: no tearing and almost no waiting");
        return running ? tr("VRR presentation requested; the driver has not confirmed activation. Check windowed/fullscreen G-SYNC settings. This does not mean the driver setting is off.")
                       : tr("Enabled. VRR will be requested during playback; activation is confirmed by the driver.");
    default: return on ? tr("Active: the display refreshes with each frame") : tr("The display is in variable refresh (enabled by the driver)");
    }
}
QString QmlPlayerBridge::fgDisplayHint() const { return impl_->fgDisplayHint; }
int QmlPlayerBridge::outputRateMode() const { return int(impl_->presentation.outputRate); }
void QmlPlayerBridge::setOutputRateMode(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.outputRate) == value) return;
    impl_->presentation.outputRate = static_cast<engine::OutputRateMode>(value);
    applyPresentation();
}
double QmlPlayerBridge::outputCustomFps() const { return impl_->presentation.customFps; }
void QmlPlayerBridge::setOutputCustomFps(double value) {
    if (!std::isfinite(value) || value < 1.0 || value > 1000.0) { emit notice(tr("The output cap must be between 1 and 1000 fps"), true); return; }
    if (std::abs(impl_->presentation.customFps - value) < 1e-6) return;
    impl_->presentation.customFps = value;
    applyPresentation();
}
bool QmlPlayerBridge::presentationOwned() const { return impl_->snapshot.presentationProviderOwned; }
QString QmlPlayerBridge::presentationStatus() const { return uiText(impl_->snapshot.presentationStatus); }
void QmlPlayerBridge::applyPresentation() {
    auto& p = impl_->presentation;
    if (!p.valid()) return;
    impl_->engine.requestPresentation(p);
    impl_->prefs[QStringLiteral("presentation")] = QVariantMap{
        {"lowQueue", p.enabled}, {"nvidiaLowLatency",int(p.nvidiaLowLatency)}, {"display", int(p.display)}, {"outputRate", int(p.outputRate)}, {"customFps", p.customFps}, {"vrr", p.variableRefresh}};
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "presentation settings not saved");
    veyra::log::info("ui-presentation", std::format("lowQueue={} nvidiaLowLatency={} display={} outputRate={} customFps={:.3f} variableRefresh={}",
        p.enabled, int(p.nvidiaLowLatency), int(p.display), int(p.outputRate), p.customFps, p.variableRefresh));
    emit presentationChanged();
    emit settingsChanged();
    emit perfChanged();
}


namespace {
double fgAutoTargetRate(const engine::PresentationSettings& effective,
                               const engine::PresentationSettings& requested,
                               double reportedRefreshHz) {
    // Prefer the engine-parsed effective value (when following the display, that is the real Hz).
    if (effective.outputRate == engine::OutputRateMode::Custom ||
        effective.outputRate == engine::OutputRateMode::FollowDisplay)
        return effective.customFps;
    // Before a source is opened the engine has not parsed yet, so the pending request value is used.
    if (requested.outputRate == engine::OutputRateMode::Custom)
        return requested.customFps;
    if (requested.outputRate == engine::OutputRateMode::FollowDisplay)
        return requested.customFps > 0.01 ? requested.customFps : reportedRefreshHz;
    // Both say "Off": calculate by the monitor's actual refresh rate.
    return reportedRefreshHz;
}
} // namespace

void QmlPlayerBridge::maybeApplyFgAutoMultiplier() {
    const auto& s = impl_->snapshot;
    if (!s.running || s.sessionId == 0) return;
    // This session has already decided: leave it alone. Recalculate only when the session number changes.
    if (impl_->fgAutoSession == s.sessionId) return;
    if (!impl_->chain.fgAutoMultiplier) { impl_->fgAutoSession = s.sessionId; return; }
    const auto backend = settings().frameGenerationBackend;
    if (!fgAutoMultiplierAvailable()) { impl_->fgAutoSession = s.sessionId; return; }
    // Give up only when the frame-generation node is missing; a node that exists but is off must still be re-decided - the previous source may have
    // turned frame generation off (source frame rate above the cap), and the next low-frame-rate source must be able to switch it back on.
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { impl_->fgAutoSession = s.sessionId; return; }

    // Target frame rate: when the output cap is Custom/Follow the display, use its value; when off, use the monitor's actual refresh rate,
    // auto-multiplier selection keeps working (user requirement 2026-10-24).
    const double targetFps = fgAutoTargetRate(s.presentationEffective, s.presentationEffective,
                                              s.displayFgRefreshHz);
    if (!(targetFps > 0.01)) {
        // The monitor refresh rate is unknown: try again next frame (never change settings on one's own).
        veyra::log::info("fg-auto", std::format("waiting for a target rate cap={} refresh={:.3f}",
            int(s.presentationEffective.outputRate), s.displayFgRefreshHz));
        return;
    }
    const double sourceFps = s.nominalSourceFps;
    if (!(sourceFps > 0.01)) {
        // The source frame rate is not ready yet: try again next frame.
        veyra::log::info("fg-auto", std::format("waiting for source rate target={:.3f}", targetFps));
        return;
    }

    impl_->fgAutoSession = s.sessionId;
    const int before = fgMultiplier();
    const uint64_t session = s.sessionId;

    // Source frame rate above or equal to the output cap: frame generation would not be visible anyway, so close it.
    if (sourceFps >= targetFps / engine::kDisplayFgFitTolerance) {
        veyra::log::info("fg-auto", std::format("session={} backend={} source={:.3f} target={:.3f} chosen=off previous={} (source at/above the cap: frame generation off)",
            session, engine::frameGenerationBackendName(backend), sourceFps, targetFps, before));
        QTimer::singleShot(0, this, [this, session] {
            if (impl_->snapshot.sessionId != session) return;
            if (!impl_->chain.fgAutoMultiplier) return;
            veyra::log::info("fg-auto", std::format("turning frame generation off session={}", session));
            applyFgMultiplier(1);
        });
        return;
    }

    // Source frame rate below the cap: pick the step by formula so it just fills the cap.
    const unsigned ceiling = unsigned(std::clamp(fgMaxMultiplier(), 2, 8));
    const unsigned wanted = engine::displayFgAutoMultiplier(backend, sourceFps, targetFps, ceiling);
    if (wanted == 0) {
        veyra::log::info("fg-auto", std::format("no usable step backend={} source={:.3f} target={:.3f} ceiling={} (multiplier kept)",
            engine::frameGenerationBackendName(backend), sourceFps, targetFps, ceiling));
        return;
    }
    const int target = int(wanted);
    veyra::log::info("fg-auto", std::format("session={} backend={} source={:.3f} target={:.3f} ceiling={} chosen={} previous={}",
        session, engine::frameGenerationBackendName(backend), sourceFps, targetFps, ceiling, target, before));
    if (before == target && node->enabled) return;
    QTimer::singleShot(0, this, [this, session, target] {
        if (impl_->snapshot.sessionId != session) return;
        if (!impl_->chain.fgAutoMultiplier) return;
        veyra::log::info("fg-auto", std::format("applying {}X session={}", target, session));
        applyFgMultiplier(target);
    });
}

// The runtime decision for Auto-select multiplier (user-settled 2026-10-24).
// Switch off: follow the upstream logic exactly, the multiplier is the user's numeric step, nothing here.
// Switch on:
//   · Source frame rate below the output cap -> pick the step by formula (ceil(cap / (sourceFps*1.02)), rounding up)
//   · Source frame rate above or equal to the output cap -> close frame generation (node enabled=false, switch follows)
//   · Output cap "Off" -> use the monitor's actual refresh rate as the target, auto-select still applies
// Calculated once per session on playback entry only: changing the multiplier rebuilds the GPU pipeline, and changing it mid-playback stalls.

void QmlPlayerBridge::setFgAutoMultiplier(bool value) {
    if (!fgAutoMultiplierAvailable()) {
        if (value) emit notice(tr("Automatic multiplier supports DLSS Frame Generation and NVIDIA VFG only"), true);
        return;
    }
    if (impl_->chain.fgAutoMultiplier == value) return;
    const auto before = impl_->chain.fgAutoMultiplier;
    impl_->chain.fgAutoMultiplier = value;
    // Both chains must change: currentSession() uses activeRuntime(), and in node mode that is acceptedChain.
    // Changing only chain would make persistSession() conclude "no change" and return early, so the auto step would fall back to manual on restart.
    // Both chains: currentSession() reads activeRuntime(), which is acceptedChain in node
    // mode; changing only `chain` made persistSession() take its early return.
    impl_->acceptedChain.fgAutoMultiplier = value;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain.fgAutoMultiplier = before;
        impl_->acceptedChain.fgAutoMultiplier = before;
        impl_->revalidate();
        emit notice(uiText(error), true); return;
    }
    // When auto-select is opened and frame generation is still off, raise it to 2X so the next playback has a step available.
    // Note: do not overwrite the user's chosen concrete step (after the switch is off, it keeps that step).
    if (value && impl_->chain.fgMultiplier < 2) impl_->chain.fgMultiplier = 2;
    // Reopening must force a re-decision: fgAutoSession records "this session already calculated a step",
    // so when the user closes and reopens mid-session, the marker must be cleared or the decision will not take effect (verified 2026-10-24).
    if (value) impl_->fgAutoSession = 0;
    veyra::log::info("ui-fg-auto", std::format("auto={} backend={} multiplier={}{}",
        value, engine::frameGenerationBackendName(settings().frameGenerationBackend), impl_->chain.fgMultiplier,
        value ? " (session decision reset)" : " (author-default multiplier logic)"));
    emit settingsChanged(); emitChainChanged();
}

// Target frame rate: the output cap equals Custom/Follow the display -> use its value; when off, the monitor's actual refresh rate.
// It must also be computable before a source is opened (snapshot has no session data then), so both entry points share it:
// one reads the engine-parsed snapshot, the other reads the pending presentation + the most recent reported refresh rate.

// Decide the multiplier before the source opens, avoiding the ~2 s stall from rebuilding the pipeline mid-playback.
// sourceFps comes from container/DASH metadata; if it cannot be obtained, return false and let the first-frame decision handle it.

void QmlPlayerBridge::applyFgMultiplier(int multiplier) {
    const auto choices=settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    const bool legal = std::find(choices.begin(),choices.end(),uint32_t(std::max(multiplier,0)))!=choices.end();
    if (!legal || multiplier > fgMaxMultiplier()) {
        emit notice(tr("The frame generation multiplier is outside the current backend's range"), true); return;
    }
    auto& chain = impl_->chain;
    auto* node = chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { if (multiplier > 1) emit notice(tr("Add a frame generation node first"), true); return; }
    const bool enabled = multiplier > 1;
    if (node->enabled == enabled && (!enabled || chain.fgMultiplier == uint32_t(multiplier))) return;
    const auto beforeMultiplier = chain.fgMultiplier; const auto beforeEnabled = node->enabled;
    node->enabled = enabled; if (enabled) chain.fgMultiplier = uint32_t(multiplier);
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        node->enabled = beforeEnabled; chain.fgMultiplier = beforeMultiplier; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged(); emitChainChanged();
}

bool QmlPlayerBridge::fgAutoMultiplierAvailable() const {
    const auto backend = settings().frameGenerationBackend;
    return backend == engine::FrameGenerationBackend::Dlss ||
           backend == engine::FrameGenerationBackend::Vfg;
}

bool QmlPlayerBridge::fgAutoMultiplier() const { return impl_->chain.fgAutoMultiplier; }

} // namespace veyra::ui
