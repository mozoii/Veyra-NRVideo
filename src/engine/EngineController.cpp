#include "veyra/engine/EngineController.h"
#include "veyra/sink/AudioGain.h"
#include "veyra/engine/BufferingGate.h"
#include "veyra/engine/PlaybackRate.h"
#include "veyra/engine/PreviewFrameReadiness.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/HdrDisplayState.h"
#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/LivePresentationTiming.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/DeadlineWait.h"
#include "veyra/engine/EnhancementDelayEstimate.h"
#include "veyra/engine/LiveGpuScheduler.h"
#include "veyra/engine/LivePresentationResetPolicy.h"
#include "veyra/engine/TimingWindow.h"
#include "veyra/engine/FrameFlowWindow.h"
#include "veyra/engine/ProviderSubmissionCounter.h"
#include "veyra/engine/AutoNrController.h"
#include "veyra/engine/AutoNrReuseController.h"
#include "veyra/engine/LiveFgAdmission.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include "veyra/engine/VfgQueueBudget.h"
#include "veyra/engine/VfgOverloadEstimate.h"
#include "veyra/engine/LivePairLatency.h"
#include "veyra/engine/RealtimePreviewScheduling.h"
#include "veyra/engine/CaptureHalfRate.h"
#include "veyra/engine/ContentDedup.h"
#include "veyra/engine/CapturePreviewClock.h"
#include "veyra/engine/DisplayFgBudget.h"
#include "veyra/engine/AdaptiveFgMultiplier.h"
#include "veyra/engine/FramePacingRepair.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/source/LiveSourceMetrics.h"
#include "veyra/xbox/DisconnectPolicy.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/FramePictureCompare.h"
#include "veyra/pipeline/ResetCoordinator.h"
#include "veyra/gfx/XessMfgUnlock.h"
#include "veyra/ngx/AmpereMfgUnlock.h"
#include "veyra/ngx/DlssgTransfusion.h"
#include "veyra/ngx/NgxCoreCache.h"
#include "veyra/diagnostics/ResetCause.h"
#include "veyra/diagnostics/CpuStallTrace.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/PresentationHooks.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/RecentGraphCache.h"
#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/PosterFrame.h"
#include <avrt.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <limits>
#include <cmath>
#include <deque>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
using Clock=std::chrono::steady_clock;
namespace {
double elapsedMs(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
int64_t monotonic100ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;}
struct OnExit {std::function<void()> action;~OnExit(){action();}};
void logFrameFlow(const diagnostics::FrameFlowMetrics& m,const char* state){
    const auto& c=m.counters;const auto& id=m.latest;
    if(c.xessSdkPresented)veyra::log::info("provider-flow",std::format("state={} session={} revision={} sdkPresented={} sdkGenerated={} sdkSubmitFps={:.2f} measurement=provider-report-not-scanout",state,id.sessionId,id.frame.settingsRevision,c.xessSdkPresented,c.xessSdkGenerated,m.xessSdkSubmitFps));
    const auto& r=m.reset; const auto ms=[&](diagnostics::ResetStage s){return r.stageMs[size_t(s)].value_or(-1.0);};
    veyra::log::info("frame-flow",std::format("state={} session={} revision={} epoch={} source={} batch={} readyFence={} consumerFence={} captureReceived={} mailboxOverwritten={} sourceAccepted={} sourceSkippedBeforeGraph={} realSubmitted={} fgCandidate={} fgSkippedBeforeEval={} fgEvaluated={} fgWarmup={} fgSkippedForReset={} fgReduced={} fgReadyValid={} fgInvalid={} realPresented={} generatedPresented={} generatedExpiredAfterEval={} cancelledBeforePresent={} commandSlots={} commandHighWater={} presentationHighWater={} slotWaitCount={} slotWaitMs={:.3f} generatedFps={:.2f} presentSubmitFps={:.2f} resetRevision={} resetEpoch={} resetReason={} resetOutcome={} resetDrainMs={:.3f} resetDestroyMs={:.3f} resetCreateMs={:.3f} resetWarmupMs={:.3f} resetFirstValidMs={:.3f} resetTotalMs={:.3f}",state,id.sessionId,id.frame.settingsRevision,id.frame.epoch,id.frame.sourceFrameId,id.batchId,id.readyFence,id.consumerFence,c.captureReceived,c.mailboxOverwritten,c.sourceAccepted,c.sourceSkippedBeforeGraph,c.realSubmitted,c.fgCandidate,c.fgSkippedBeforeEval,c.fgEvaluated,c.fgWarmup,c.fgSkippedForReset,c.fgReduced,c.fgReadyValid,c.fgInvalid,c.realPresented,c.generatedPresented,c.generatedExpiredAfterEval,c.cancelledBeforePresent,c.commandSlotsInFlight,c.commandSlotHighWater,c.presentationBatchHighWater,m.slotReuseWaitCount,m.slotReuseWaitMs.value_or(0),m.validGeneratedFps,m.presentSubmitFps,r.settingsRevision,r.epoch,unsigned(r.reason),unsigned(r.outcome),ms(diagnostics::ResetStage::Drain),ms(diagnostics::ResetStage::Destroy),ms(diagnostics::ResetStage::Create),ms(diagnostics::ResetStage::Warmup),ms(diagnostics::ResetStage::FirstValid),r.totalMs.value_or(-1.0)));
}
}
EngineController::EngineController(){
    watchdog_=std::make_unique<EngineWatchdog>([this](uint64_t stalledMs){noteEngineHang(stalledMs);});
    worker_=std::thread(&EngineController::dispatch,this);
}
EngineController::~EngineController(){
    {std::lock_guard lock(mutex_);shutdown_=true;pending_={};stop_=true;}wake_.notify_one();
    if(!worker_.joinable()){watchdog_.reset();return;}
    // Field logs(15): an owner thread blocked inside a driver call never returns,
    // and closing the app waited for it forever (the process had to be killed).
    // Wait a bounded time, then end the process instead of hanging on exit.
    const auto started=Clock::now();
    {std::unique_lock lock(mutex_);
        while(!dispatchDone_.wait_for(lock,std::chrono::milliseconds(100),[&]{return dispatchExited_;})){
            const double waited=elapsedMs(started);
            if(waited<(watchdog_->hung()?3000.0:20000.0))continue;
            veyra::log::error("engine-hang",std::format("shutdown: owner thread still blocked after {:.0f} ms; ending the process instead of waiting",waited));
            Logger::instance().flush();
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    worker_.join();
    // The watchdog callback reads snapshot_ and hangPreviousStatus_. Detach it
    // while all controller members are still alive, before reverse destruction
    // reaches hangPreviousStatus_ (declared after watchdog_).
    watchdog_.reset();
}
void EngineController::dispatch(){
    for(;;){std::function<void()> task;{std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return shutdown_||bool(pending_);});if(shutdown_)break;task=std::move(pending_);pending_={};busy_=true;stop_=false;}
        try{task();}catch(const std::exception&){status(L"The job hit an error and stopped; see diagnostics",true);}
        {std::lock_guard lock(mutex_);busy_=false;snapshot_.running=false;if(snapshot_.transport==TransportState::Stopping)snapshot_.transport=TransportState::Empty;}
    }
    prewarmed_.reset();
    {std::lock_guard lock(mutex_);dispatchExited_=true;}dispatchDone_.notify_all();
}
// Watchdog thread. try_lock: an owner stuck while holding mutex_ must not take
// the watchdog down with it.
void EngineController::noteEngineHang(uint64_t stalledMs){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return;
    if(stalledMs){if(snapshot_.status!=kEngineHangStatus){hangPreviousStatus_=snapshot_.status;snapshot_.status=kEngineHangStatus;}}
    else if(snapshot_.status==kEngineHangStatus)snapshot_.status=hangPreviousStatus_;
}
void EngineController::post(std::function<void()> task){ {std::lock_guard lock(mutex_);stop_=true;pending_=std::move(task);}wake_.notify_one(); }
bool EngineController::idle()const{std::lock_guard lock(mutex_);return !busy_&&!pending_;}
bool EngineController::prewarmEnhancement(EnhancementSettings settings,unsigned width,unsigned height){
    if(!prewarmEnabled_||!PreviewGpuSession::requestAllowed(settings))return false;
    {
        std::lock_guard lock(mutex_);if(shutdown_||busy_||pending_)return false;
        pending_=[this,settings,width,height]{
            if(!prewarmEnabled_)return;
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            auto candidate=std::make_unique<PreviewGpuSession>();prewarmed_.reset();
            log::info("prewarm","event=begin owner=engine-dispatcher");
            const bool ready=candidate->prepare(settings,width,height);
            bool keep=false;{std::lock_guard lock(mutex_);keep=ready&&prewarmEnabled_&&!shutdown_&&(!stop_||bool(pending_));}
            if(keep)prewarmed_=std::move(candidate);
            else log::info("prewarm","event=discard reason=not-ready-disabled-or-cancelled");
            candidate.reset();CoUninitialize();
        };
    }
    wake_.notify_one();return true;
}
void EngineController::setEnhancementPrewarmEnabled(bool enabled){
    prewarmEnabled_=enabled;
    if(!enabled){std::lock_guard lock(mutex_);if(!busy_&&!pending_)pending_=[this]{prewarmed_.reset();log::info("prewarm","event=disabled released=true");};}
    wake_.notify_one();
}
bool EngineController::setPlaybackRate(double rate){
    if(!validPlaybackRate(rate))return false;
    std::lock_guard lock(mutex_);
    if(!snapshot_.running||snapshot_.capture||snapshot_.image)return false;
    if(requestedPlaybackRate_==rate)return true;
    requestedPlaybackRate_=rate;snapshot_.playbackRate=rate;
    if(seekSeconds_<0){snapshot_.seekTarget=snapshot_.position;++snapshot_.seekRequested;seekSeconds_=snapshot_.position;}
    veyra::log::info("playback-rate",std::format("requested {:.2f}x at {:.3f}s",rate,snapshot_.position));
    return true;
}
void EngineController::setFullscreenMemoryProtection(bool enabled){
    fullscreenMemoryProtection_=enabled;
    if(!enabled){std::lock_guard lock(mutex_);snapshot_.vramFullscreenUnsafe=false;}
}
void EngineController::open(HWND video,const std::wstring& path,PlayerOptions opts){
    const bool captureReplay=opts.captureReplayForTest;
    // Diagnostics-only PlayerOptions fields never enter EnhancementSettings;
    // keep them across the snapshot round-trip below.
    const bool softwareDecode=opts.softwareDecode;const bool hardwareDecodeOnly=opts.hardwareDecodeOnly;
    const media::NetworkSettings network=opts.network; // #48: buffering / reconnect choices
    // Per-open state that EnhancementSettings does not carry, so the snapshot round-trip
    // below would drop it: the DASH audio address is the difference between a Bilibili
    // video that has sound and one that is silent.
    const std::wstring audioPath=opts.audioPath;
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_,&opts);}
    opts.captureReplayForTest=captureReplay;
    opts.softwareDecode=softwareDecode;opts.hardwareDecodeOnly=hardwareDecodeOnly;
    opts.network=network;
    opts.audioPath=audioPath;
    post([this,video,path,opts]{paused_=false;seekSeconds_=-1;run(video,path,opts);});
}
#ifdef VEYRA_ENABLE_MOONLIGHT
// Input goes straight to the session: the shared pointer is copied under the lock, the send is not.
#define VEYRA_MOONLIGHT_INPUT(call) std::shared_ptr<source::MoonlightSessionSource> m;{std::lock_guard lock(mutex_);m=activeMoonlight_;}if(m)m->call
void EngineController::moonlightController(const input::ControllerState& state){VEYRA_MOONLIGHT_INPUT(controller(state));}
input::ControllerFeedback EngineController::moonlightFeedback(){std::shared_ptr<source::MoonlightSessionSource> m;{std::lock_guard lock(mutex_);m=activeMoonlight_;}return m?m->takeFeedback():input::ControllerFeedback{};}
void EngineController::moonlightKey(uint32_t vk,uint32_t scan,bool extended,bool down){VEYRA_MOONLIGHT_INPUT(keyboard(vk,scan,extended,down));}
void EngineController::moonlightMouseMove(int dx,int dy){VEYRA_MOONLIGHT_INPUT(mouseMove(dx,dy));}
void EngineController::moonlightMouseButton(int button,bool down){VEYRA_MOONLIGHT_INPUT(mouseButton(button,down));}
void EngineController::moonlightScroll(int delta,bool horizontal){VEYRA_MOONLIGHT_INPUT(scroll(delta,horizontal));}
void EngineController::moonlightReleaseInput(){VEYRA_MOONLIGHT_INPUT(releaseInput());}
#undef VEYRA_MOONLIGHT_INPUT
#endif
#ifdef VEYRA_ENABLE_XBOX
void EngineController::xboxController(const input::ControllerState& state){std::shared_ptr<source::XboxSessionSource> x;{std::lock_guard lock(mutex_);x=activeXbox_;}if(x)x->controller(state);}
input::ControllerFeedback EngineController::xboxFeedback(){std::shared_ptr<source::XboxSessionSource> x;{std::lock_guard lock(mutex_);x=activeXbox_;}return x?x->takeFeedback():input::ControllerFeedback{};}
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
void EngineController::openMoonlight(HWND window,source::MoonlightConnectDesc desc,PlayerOptions opts){
    auto request=std::make_shared<source::MoonlightConnectDesc>(std::move(desc));
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;snapshot_.moonlightActive=true;snapshot_.capture=true;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_,&opts);}
    post([this,window,request,opts]{paused_=false;seekSeconds_=-1;run(window,L"moonlight:",opts,request);});
}
#endif
#ifdef VEYRA_ENABLE_XBOX
void EngineController::openXbox(HWND window,source::XboxConnectDesc desc,PlayerOptions opts){
    auto request=std::make_shared<source::XboxConnectDesc>(std::move(desc));
    {std::lock_guard lock(mutex_);snapshot_={};requestedPlaybackRate_=1;activeFlow_.reset();previewView_={};fgMultiFrameMaxCap_=0;xessMaxInterpolatedFramesCap_=0;fsrMaxGeneratedFramesCap_=0;snapshot_.sessionId=++sessionId_;snapshot_.transport=TransportState::Opening;snapshot_.xboxActive=true;snapshot_.capture=true;savePath_.clear();desired_=opts.snapshot();desiredNodeOrder_=opts.nodeOrder;desired_.revision=++nextRevision_;snapshot_.desired=desired_;opts=PlayerOptions::from(desired_,desiredNodeOrder_,&opts);}
    post([this,window,request,opts]{paused_=false;seekSeconds_=-1;run(window,L"xbox:",opts,nullptr,request);});
}
#endif
void EngineController::stop(){
    {std::lock_guard lock(mutex_);stop_=true;pending_={};snapshot_.transport=busy_?TransportState::Stopping:TransportState::Empty;
     if(!busy_)pending_=[this]{prewarmed_.reset();};}
    wake_.notify_one();
}
void EngineController::pause(bool p){paused_=p;std::lock_guard lock(mutex_);if(snapshot_.running)snapshot_.transport=p?TransportState::Paused:TransportState::Playing;}
void EngineController::setVolume(float gain,bool mute){if(!std::isfinite(gain))return;volume_=sink::clampAudioGain(gain);muted_=mute;}
bool EngineController::selectAudioTrack(uint64_t session,int streamIndex){
    std::lock_guard lock(mutex_);
    if(session!=snapshot_.sessionId||!snapshot_.running||snapshot_.failed||snapshot_.capture||snapshot_.image)return false;
    if(std::none_of(snapshot_.audioTracks.begin(),snapshot_.audioTracks.end(),[&](const auto& t){return t.streamIndex==streamIndex;}))return false;
    pendingAudioSession_=session;pendingAudioTrack_=streamIndex;return true;
}
bool EngineController::requestSettings(EnhancementSettings s,std::optional<ChainRuntimeOrder> order,uint64_t* acceptedRevision){
    if(!s.validate().empty()){veyra::log::warn("settings","invalid whole settings transaction rejected");status(L"The whole set of settings is invalid; nothing was applied",false);return false;}
    if(order){
        // Fail before changing desired state. This is a shape admission check,
        // not a GPU test; actual dimensions are compiled again at graph creation.
        StageRequest r;r.width=64;r.height=64;r.nr=s.nr;r.sr=s.sr;
        r.fg=s.multiplier>1;r.fgMultiplier=s.multiplier;r.nodeOrder=&*order;
        pipeline::EnhanceGraphDesc desc;describeStages(r,s,desc);
        if(const auto error=desc.validateFixedExecutionPlan();!error.empty()){
            veyra::log::warn("settings",std::format("node order rejected before commit: {}",error));
            status(L"The executor does not support this node order yet; the configuration is unchanged",false);return false;
        }
    }
    std::lock_guard lock(mutex_);if(snapshot_.image)s.multiplier=1;
    // Capability gate: refuse a multiplier the active GPU/runtime cannot honour
    // instead of letting the graph fail and silently turning frame generation
    // off. The previous value is preserved and the reason is surfaced.
    if(s.multiplier>1&&s.frameGenerationBackend==FrameGenerationBackend::Dlss&&fgMultiFrameMaxCap_>0&&
       int(s.multiplier)-1>fgMultiFrameMaxCap_){
        snapshot_.status=std::format(L"This GPU supports up to {}X frame generation; request not applied",fgMultiFrameMaxCap_+1);
        snapshot_.failed=false;
        veyra::log::info("settings",std::format("multiplier gate: requested={} maxGeneratedFrames={} applied=unchanged",s.multiplier,fgMultiFrameMaxCap_));
        return false;
    }
    // XeSS's current ceiling describes this context, not the next one: 2X
    // intentionally runs without the MFG unlock. Validate a higher request in
    // XessPresenter after applying the unlock; rebuild failure rolls it back.
    // The AMD 3.1.x frame-generation provider delivers exactly one generated
    // frame per presented frame; the probe (tools/fsr_probe) measured this on
    // every requested count, so anything above 2X is refused instead of
    // silently presenting 2X under a 4X label.
    if(s.multiplier>2&&twoXFrameGeneration(s.frameGenerationBackend)){
        snapshot_.status=L"AMD FSR frame generation is limited to 2X; request not applied";
        snapshot_.failed=false;
        veyra::log::info("settings",std::format("fsr multiplier gate: requested={} maxGeneratedFrames=1 applied=unchanged",s.multiplier));
        return false;
    }
    // Revision partitions GPU history and measurements. Saved bypassed-effect
    // parameters and audio-only edits must not invalidate in-flight video.
    s.revision=desired_.revision;if(s==desired_&&order==desiredNodeOrder_){if(acceptedRevision)*acceptedRevision=desired_.revision;return true;}
    if(!s.sameVideoConfiguration(desired_)||order!=desiredNodeOrder_)s.revision=++nextRevision_;
    desiredNodeOrder_=order;desired_=s;snapshot_.desired=s;snapshot_.applying=snapshot_.applied!=desired_;
    if(acceptedRevision)*acceptedRevision=s.revision;return true;
}
void EngineController::saveFrame(const std::wstring& path){std::lock_guard lock(mutex_);savePath_=path;snapshot_.savedFramePath.clear();}
void EngineController::startExport(const std::wstring& input,const std::wstring& output,PlayerOptions opts,bool hevc){
    if(opts.nodeOrder){
        veyra::log::warn("node-order","export has no runtime node-order transport; request rejected before posting");
        status(L"The node chain is not wired to the export executor yet; switch to list mode to export",false);return;
    }
    const auto frozen=opts.snapshot();const int audioStream=opts.audioStreamIndex;
    post([this,input,output,frozen,audioStream,hevc]{prewarmed_.reset();CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto options=PlayerOptions::from(frozen);options.audioStreamIndex=audioStream;bool ok=exportVideo(input,output,options,hevc,stop_,[this](double p,const std::wstring& s){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.position=p;snapshot_.duration=1;snapshot_.running=true;});{std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.failed=!ok&&!stop_;}CoUninitialize();});
}
void EngineController::requestPresentation(PresentationSettings settings){
    if(!settings.valid())return;
    std::lock_guard lock(mutex_);if(presentation_==settings)return;
    presentation_=settings;++presentationRevision_;
}
PlayerSnapshot EngineController::snapshot()const{
    // The flow window has its own mutex and percentile work; take it after
    // releasing the engine mutex so UI polling does not hold the owner thread
    // (sweep 2026-09-22 B3). Consecutive UI callers within 50 ms share one
    // flow snapshot.
    PlayerSnapshot copy;std::shared_ptr<FrameFlowWindow> flowWindow;
    // The session pointers are copied under the lock: teardown resets them on the
    // engine thread while the UI polls.
#ifdef VEYRA_ENABLE_MOONLIGHT
    std::shared_ptr<source::MoonlightSessionSource> activeMoonlight;
#endif
#ifdef VEYRA_ENABLE_XBOX
    std::shared_ptr<source::XboxSessionSource> activeXbox;
#endif
    {std::lock_guard lock(mutex_);copy=snapshot_;copy.presentation=presentation_;flowWindow=activeFlow_;
#ifdef VEYRA_ENABLE_MOONLIGHT
        activeMoonlight=activeMoonlight_;
#endif
#ifdef VEYRA_ENABLE_XBOX
        activeXbox=activeXbox_;
#endif
    }
    copy.volume=volume_;copy.muted=muted_;
    const bool playing=copy.running&&!copy.image&&copy.transport==TransportState::Playing;
    if(flowWindow){
        static std::mutex cacheMutex;static diagnostics::FrameFlowMetrics cached;static const FrameFlowWindow* cachedFor=nullptr;static int64_t cachedAt=0;
        const auto now=monotonic100ns();
        std::lock_guard lock(cacheMutex);
        if(cachedFor!=flowWindow.get()||now-cachedAt>500000){cached=flowWindow->snapshot(now);cachedFor=flowWindow.get();cachedAt=now;}
        copy.metrics.flow=cached;
    }
    copy.fgBudgetLimited=playing&&copy.metrics.flow.lastFgRejected100ns>0&&monotonic100ns()-copy.metrics.flow.lastFgRejected100ns<10000000;
#ifdef VEYRA_ENABLE_MOONLIGHT
    if(copy.moonlightActive&&activeMoonlight){
        copy.moonlight=activeMoonlight->stats();
        copy.captureAudio=activeMoonlight->audioState();copy.audioAvailable=copy.captureAudio.available;
    }
#endif
#ifdef VEYRA_ENABLE_XBOX
    if(copy.xboxActive&&activeXbox){
        copy.xbox=activeXbox->stats();
        copy.captureAudio=activeXbox->audioState();copy.audioAvailable=copy.captureAudio.available;
    }
#endif
    auto& flow=copy.metrics.flow;
    if(!playing){flow.sourceCompletedFps=flow.outputCompletedFps=flow.validGeneratedFps=flow.presentSubmitFps=flow.xessSdkSubmitFps=0;flow.presentLow1Fps.reset();}
    copy.fps=flow.sourceCompletedFps;copy.submissionFps=flow.presentSubmitFps;copy.submissionLow1Fps=flow.presentLow1Fps;
    return copy;
}
void EngineController::status(const std::wstring& s,bool failed){std::lock_guard lock(mutex_);snapshot_.status=s;snapshot_.failed=failed;if(failed)snapshot_.transport=TransportState::Failed;}
namespace {
// MMCSS registration keeps the graph owner and capture callback threads ahead
// of ordinary UI/background work; failure is logged once and ignored.
struct MmcssScope {
    HANDLE handle=nullptr;
    explicit MmcssScope(const wchar_t* task){DWORD index=0;handle=AvSetMmThreadCharacteristicsW(task,&index);
        static std::atomic<bool> reported{false};
        if(!handle&&!reported.exchange(true))veyra::log::warn("scheduler",std::format("MMCSS unavailable error={} (continuing at normal priority)",GetLastError()));}
    ~MmcssScope(){if(handle)AvRevertMmThreadCharacteristics(handle);}
};
}
void EngineController::run(HWND window,std::wstring path,PlayerOptions options,std::shared_ptr<source::MoonlightConnectDesc> moonlightRequest,std::shared_ptr<source::XboxConnectDesc> xboxRequest){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    MmcssScope mmcss(L"Pro Audio");
    status(L"Initializing the GPU and local runtimes…");
    auto extension=std::filesystem::path(path).extension().wstring();for(auto& c:extension)c=towlower(c);
    const bool ordinaryFile=!moonlightRequest&&!xboxRequest&&
        extension!=L".png"&&extension!=L".jpg"&&extension!=L".jpeg";
    if(!prewarmEnabled_||!ordinaryFile)prewarmed_.reset();
    // A prewarmed session belongs to the adapter that was preferred when it
    // started; a different compute GPU choice opens a new one.
    if(prewarmed_&&prewarmed_->context.initialized()&&gfx::preferredAdapterLuid()&&prewarmed_->context.adapter().luid!=gfx::preferredAdapterLuid())prewarmed_.reset();
    auto gpu=prewarmed_?std::move(prewarmed_):std::make_unique<PreviewGpuSession>();
    auto& ctx=gpu->context;auto& ring=gpu->ring;source::MediaFileSource source;
    // A GPU hang is reported before Windows resets the GPU (field logs: 13 s later). DRED can
    // only name the stopped command list after the removal, so teardown checks once more.
    bool gpuFailureAwaitingRemoval=false;
    sink::AudioPipeline audioPipe;sink::AudioRenderer audio;VideoPresenter presenter;
    presenter.setDisplayStates(&displayStates_);
    auto& coreCache=gpu->core;auto& recentGraphCache=gpu->recent;auto& graph=gpu->graph;
    auto& graphBytes=gpu->graphBytes;auto cacheBudgetPolledAt=Clock::now();
    const bool recentCacheAllowed=!GetEnvironmentVariableW(L"VEYRA_TEST_DISABLE_RECENT_GRAPH_CACHE",nullptr,0);
    AVFrame* imageFrame=nullptr;AVFrame* cachedFrame=nullptr;pipeline::FramePacket cachedPacket;
    // Set when a live parameter change arrives while paused (or on a still
    // image): the cached source frame must be re-rendered before the next
    // present, otherwise the new colour tables never reach the GPU.
    bool refreshPausedFrame_=false;
#ifdef VEYRA_ENABLE_MOONLIGHT
    auto moon=moonlightRequest?std::make_shared<source::MoonlightSessionSource>():nullptr;
    const bool isMoonlight=bool(moon);
    {std::lock_guard lock(mutex_);activeMoonlight_=moon;}
#else
    (void)moonlightRequest;const bool isMoonlight=false;
#endif
#ifdef VEYRA_ENABLE_XBOX
    auto xb=xboxRequest?std::make_shared<source::XboxSessionSource>():nullptr;
    xbox::ReconnectBudget xboxReconnectBudget;
    const bool isXbox=bool(xb);
    {std::lock_guard lock(mutex_);activeXbox_=xb;}
#else
    (void)xboxRequest;const bool isXbox=false;
#endif
    // Moonlight and Xbox are decoded network streams with the same live scheduling.
    const bool isStream=isMoonlight||isXbox;
    const bool isCapture=options.captureReplayForTest||isStream;
    // File replay has no hardware arrival clock; retain its continuous PTS anchor.
    // VEYRA_TEST_REPLAY_AS_CARD=1 delivers it like a live card instead (see below).
    const bool testReplayAsCard=options.captureReplayForTest&&GetEnvironmentVariableW(L"VEYRA_TEST_REPLAY_AS_CARD",nullptr,0)>0;
    const bool pairAnchoredLive=isStream||testReplayAsCard;
    source::IFrameSource* activeSource=&source;
#ifdef VEYRA_ENABLE_MOONLIGHT
    if(moon)activeSource=moon.get();
#endif
#ifdef VEYRA_ENABLE_XBOX
    if(xb)activeSource=xb.get();
#endif
    if(options.captureReplayForTest)veyra::log::info("capture-test","file replay exercises live scheduler; no physical capture device or latency measurement");
    bool audioStarted=false;bool failed=false;
    try {
        do {
            Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
            // Two bounded jobs, each up to three enhancement lists plus five
            // MFG lists. Allocators must not stall the presentation owner.
            wchar_t testSlots[16]{};unsigned commandSlots=16;
            if(GetEnvironmentVariableW(L"VEYRA_TEST_COMMAND_SLOTS",testSlots,16))commandSlots=std::clamp(unsigned(_wtoi(testSlots)),6u,24u);
            if(!gpu->initialize(commandSlots)){status(L"D3D12 initialization failed; see diagnostics",true);break;}
            auto ext=std::filesystem::path(path).extension().wstring();for(auto& c:ext)c=towlower(c);
            const bool isImage=!media::networkMediaInput(path)&&(ext==L".png"||ext==L".jpg"||ext==L".jpeg");
            sink::RgbaImage image;
            uint32_t width=0,height=0;double duration=0;
            if(isImage){
                if(!sink::loadImage(path,image)){status(L"Could not decode the PNG/JPEG",true);break;}
                if(!pipeline::Extent{image.width,image.height}.valid()||uint64_t(image.width)*image.height>16777216){
                    if(options.nodeOrder){
                        veyra::log::warn("node-order","large-image tiling has no runtime node-order transport; request rejected");
                        status(L"Node mode does not do tiled processing of large images yet; switch to list mode and open it again",true);break;
                    }
                    runLargeImage(window,image,options,ctx,ring);break;
                }
                imageFrame=av_frame_alloc();imageFrame->format=AV_PIX_FMT_RGBA;imageFrame->width=image.width;imageFrame->height=image.height;imageFrame->pts=0;imageFrame->color_range=AVCOL_RANGE_JPEG;imageFrame->colorspace=AVCOL_SPC_RGB;imageFrame->color_trc=AVCOL_TRC_IEC61966_2_1;
                if(av_frame_get_buffer(imageFrame,32)<0){status(L"Image resource allocation failed",true);break;}
                for(unsigned y=0;y<image.height;++y)memcpy(imageFrame->data[0]+size_t(y)*imageFrame->linesize[0],image.pixels.data()+size_t(y)*image.width*4,size_t(image.width)*4);
                width=image.width;height=image.height;options.fg=false;
                {std::lock_guard lock(mutex_);desired_.multiplier=1;snapshot_.image=true;snapshot_.desired=desired_;
                 snapshot_.sourceWidth=image.width;snapshot_.sourceHeight=image.height;
                 snapshot_.sourceDisplayAspect=image.height>0?double(image.width)/image.height:0.0;
                 snapshot_.sourceRotationDegrees=0;}
            }else{
                source::SourceOpenDesc od;od.path=path;
                od.cancelled=&stop_;
                od.deinterlace=options.deinterlace;
                // Network sources and files on a server (#48): buffer settings, reads that may report
                // "buffering" instead of blocking the loop, and tolerance for damaged packets. Local
                // files ignore all three.
                od.network=options.network;
                // Site-specific request headers (Bilibili's CDN checks the Referer).
                od.networkHeaders=options.network.requestHeaders;
                // DASH audio arrives as its own address, not inside the video container.
                od.audioPath=options.audioPath;
                od.nonBlocking=!isCapture;
                od.tolerateCorruption=!isCapture;
                // Files use the same shared D3D12 device as the graph; the PC and
                // Xbox streaming sources use it for their compressed-payload
                // decoder (H.264/HEVC/AV1/VP9 through D3D12VA). Each source
                // performs its own capability check and records its fallback.
                od.preferHardwareDecode=!(options.softwareDecode&&(!isCapture||options.captureReplayForTest));
                od.requireHardwareDecode=options.hardwareDecodeOnly&&!options.softwareDecode&&!isCapture&&!isImage; // stills have no hardware decoder
                od.d3d12Device=ctx.device();od.d3d12Queue=ctx.directQueue();
                od.d3d12AdapterLuid=ctx.adapter().luid;
                od.decodeDolbyEnhancementLayer=options.settings.doviP7Compose;
                veyra::log::info("decode", std::format("p7 compose setting={}", options.settings.doviP7Compose?1:0));
                if(!isCapture)veyra::log::info("decode",std::format("file decode preference={}",options.softwareDecode?"software":options.hardwareDecodeOnly?"hardware-only":"hardware-first"));
                // Diagnostic uses the production graph/presenter to validate
                // D3D12VA imports without needing a paired stream or credentials.
                if(!isCapture&&GetEnvironmentVariableW(L"VEYRA_TEST_FILE_HW_DECODE",nullptr,0)){
                    od.preferHardwareDecode=true;od.d3d12Device=ctx.device();od.d3d12Queue=ctx.directQueue();od.d3d12AdapterLuid=ctx.adapter().luid;
                }
                // Audio ingress policy is negotiated by the streaming sources
                // before the branch chain: inserting statements between
                // `}else` and the open call silently re-binds the `else` and
                // made stream sessions fall through to the file open check
                // (fixed 2026-09-16).
#ifdef VEYRA_ENABLE_MOONLIGHT
                if(moon){
                    status(L"Connecting to the streaming host…");
                    ctx.device()->AddRef();moonlightRequest->decodeDevice=std::shared_ptr<ID3D12Device>(ctx.device(),[](ID3D12Device* device){device->Release();});
                    ctx.directQueue()->AddRef();moonlightRequest->decodeQueue=std::shared_ptr<ID3D12CommandQueue>(ctx.directQueue(),[](ID3D12CommandQueue* queue){queue->Release();});
                    if(!moon->connect(std::move(*moonlightRequest))){const auto message=moon->stats().message;status(message.empty()?L"Streaming connection failed; see diagnostics":message,true);break;}
                    moonlightRequest.reset();
                    const auto firstFrameDeadline=Clock::now()+std::chrono::seconds(20);
                    while(!stop_){
                        const AVFrame* first=nullptr;const auto result=moon->read(cachedPacket,&first);
                        if(result==source::SourceReadStatus::Frame){cachedFrame=av_frame_clone(first);break;}
                        if(result==source::SourceReadStatus::Error){const auto message=moon->stats().message;status(message.empty()?L"The host sent no decodable picture; check the host and network and reconnect.":message,true);break;}
                        if(Clock::now()>firstFrameDeadline){status(L"Connected, but no picture arrived within 20 seconds. Check the host's display output and encoding settings.",true);break;}
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if(stop_||!cachedFrame)break;
                }
#endif
#ifdef VEYRA_ENABLE_XBOX
                if(xb){
                    status(L"Connecting to Xbox…");
                    ctx.device()->AddRef();xboxRequest->decodeDevice=std::shared_ptr<ID3D12Device>(ctx.device(),[](ID3D12Device* device){device->Release();});
                    ctx.directQueue()->AddRef();xboxRequest->decodeQueue=std::shared_ptr<ID3D12CommandQueue>(ctx.directQueue(),[](ID3D12CommandQueue* queue){queue->Release();});
                    xboxRequest->stopRequested=&stop_;
                    if(!xb->connect(*xboxRequest)){const auto message=xb->stats().message;status(message.empty()?L"Xbox streaming connection failed; see diagnostics":message,true);break;}
                    const auto firstFrameDeadline=Clock::now()+std::chrono::seconds(20);
                    while(!stop_){
                        const AVFrame* first=nullptr;const auto result=xb->read(cachedPacket,&first);
                        if(result==source::SourceReadStatus::Frame){cachedFrame=av_frame_clone(first);break;}
                        if(result==source::SourceReadStatus::Error){const auto message=xb->stats().message;status(message.empty()?L"The Xbox sent no decodable picture; reconnect.":message,true);break;}
                        if(Clock::now()>firstFrameDeadline){status(L"Connected to the Xbox, but no picture arrived within 20 seconds. Reconnect.",true);break;}
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if(stop_||!cachedFrame)break;
                }
#endif
                // Opening a source runs FFmpeg's demuxer, which can throw. Letting that
                // escape ends the whole session with a message that says nothing about
                // where it came from.
                bool sourceOpened=false;
                try{
                    sourceOpened=isMoonlight||isXbox||activeSource->open(od);
                }catch(const std::exception& e){
                    veyra::log::error("source-open",std::format("threw: {} (headers={} audio={})",
                        e.what(),od.networkHeaders.size(),od.audioPath.size()));
                    sourceOpened=false;
                }
                if(!isMoonlight&&!isXbox&&!sourceOpened){status(!isCapture&&!source.errorMessage().empty()?source.errorMessage():L"Could not open the video; see diagnostics",true);break;}
                // A file's actual decoded pixel format and HDR VUI are only
                // reliable on AVFrame. Prime one frame before constructing
                // the graph so AV1/MOV 10-bit and HDR inputs do not get an
                // SDR graph by mistake. The cloned frame is consumed by the
                // normal owner-thread loop below, preserving its PTS.
                if(!isCapture){
                    pipeline::FramePacket firstPacket;const AVFrame* firstFrame=nullptr;
                    auto firstStatus=activeSource->read(firstPacket,&firstFrame);
                    // A network source collects its start-up buffer before the first picture; show the
                    // progress instead of an empty window, and let the user stop at any time.
                    {
                        auto nextShown=Clock::now();
                        while(firstStatus==source::SourceReadStatus::Waiting&&!stop_){
                            if(Clock::now()>=nextShown){
                                nextShown=Clock::now()+std::chrono::milliseconds(150);
                                const auto stats=source.networkStats();
                                {std::lock_guard lock(mutex_);snapshot_.network=stats;}
                                if(!stats.message.empty())status(stats.message);
                            }
                            firstStatus=activeSource->read(firstPacket,&firstFrame);
                        }
                        if(stop_)break;
                    }
                    if(firstStatus!=source::SourceReadStatus::Frame||firstFrame==nullptr||firstPacket.pts.isUnknown()){
                        status(activeSource->info().kind==pipeline::SourceKind::File&&source.errorMessage().empty()
                            ?L"The first video frame cannot be decoded or has no valid timestamp; see diagnostics":source.errorMessage().empty()?L"Could not read the first video frame":source.errorMessage(),true);
                        break;
                    }
                    cachedFrame=av_frame_clone(firstFrame);
                    if(!cachedFrame){status(L"Could not cache the first video frame",true);break;}
                    cachedPacket=firstPacket;
                }
                width=activeSource->info().width;height=activeSource->info().height;duration=isCapture?0:activeSource->info().duration.toDouble();
                {std::lock_guard lock(mutex_);
                 const auto& si=activeSource->info();
                 snapshot_.sourceWidth=si.width;snapshot_.sourceHeight=si.height;
                 snapshot_.sourceDisplayAspect=si.displayAspect;
                 previewView_.rotationDegrees=si.rotationDegrees;
                 if(si.width&&si.height&&std::isfinite(si.displayAspect)&&si.displayAspect>0&&std::abs(si.displayAspect-double(si.width)/si.height)>.001)
                     previewView_.displayAspect=si.displayAspect;
                 snapshot_.sourceRotationDegrees=si.rotationDegrees;
                 // Poster for the minimal-mode bar. Built once, from the frame
                 // we already decoded; failure leaves it empty on purpose.
                 std::vector<uint8_t> poster;uint32_t pw=0,ph=0;
                 const AVFrame* posterSource=cachedFrame?cachedFrame:imageFrame;
                 if(posterSource&&makePosterFrame(posterSource,320,180,poster,pw,ph)){
                     snapshot_.posterRgba=std::move(poster);snapshot_.posterWidth=pw;snapshot_.posterHeight=ph;
                 }else{snapshot_.posterRgba.clear();snapshot_.posterWidth=snapshot_.posterHeight=0;}}
                {std::lock_guard lock(mutex_);snapshot_.sourceNotice=activeSource->info().dolbyVision.description();
                 const auto& si=activeSource->info();if(si.deinterlaced)snapshot_.sourceNotice+=std::format(L"{}Deinterlacing: {} · {:.3f}i → {:.3f}p",snapshot_.sourceNotice.empty()?L"":L" · ",si.deinterlacePath=="d3d12-video-processor"?L"GPU":L"software compatibility",si.averageFps,si.averageFps);}
            }
            if(!pipeline::Extent{width,height}.valid()){status(L"The image is larger than one GPU texture allows; it needs tiled processing",true);break;}
            HdrDisplayState displayHdrState;
            bool displayQueryFailed=false;
            uint64_t hdrDisplaySerial=0;
            // Windows HDR comes from the shared display state (the same background
            // query as refresh and VRR, asked again only on a display change, a
            // monitor change or refresh the monitor state). A new graph waits for a missing or
            // answer once (100ms maximum). A stale target-local cache stays
            // usable while the worker refreshes it; a settings transaction must
            // never wait for an unrelated slow VRR/driver call to finish.
            auto displayHdrActive=[&]{
                const HMONITOR target=displayStates_.monitorOf(window);
                auto known=displayStates_.state(target);
                if(!known||known->generation!=displayStates_.generation()){
                    displayStates_.request(window,"hdr-output");
                    if(!known)known=displayStates_.wait(target,std::chrono::milliseconds(100));
                }
                const std::optional<bool> queried=known?known->color.hdr:std::nullopt;
                if(known)hdrDisplaySerial=known->serial;
                const bool active=displayHdrState.update(reinterpret_cast<std::uintptr_t>(target),queried);
                if(!queried&&!displayQueryFailed)veyra::log::warn("display-color",std::format("HDR display query unavailable; using target-local hdr={} (unknown target defaults to SDR)",active));
                if(queried&&displayQueryFailed)veyra::log::info("display-color",std::format("HDR display query recovered hdr={}",active));
                displayQueryFailed=!queried.has_value();
                return active;
            };
            pipeline::EnhanceGraphDesc gd;gd.sourceWidth=width;gd.sourceHeight=height;gd.hdrInput=!isImage&&activeSource->info().color.isHdrPath();
            {const auto& sourceColor=activeSource->info().color;
    const unsigned sourceMastering=hdrMetadataNits(sourceColor.hdrMasteringPeakNits);
    const unsigned sourceCll=hdrMetadataNits(sourceColor.hdrMaxCllNits);
    const unsigned sourceFall=hdrMetadataNits(sourceColor.hdrMaxFallNits);
    std::lock_guard lock(mutex_);snapshot_.sourceHdr=gd.hdrInput;
    snapshot_.sourceMasteringPeakNits=sourceMastering;snapshot_.sourceMaxCllNits=sourceCll;
    snapshot_.sourceMaxFallNits=sourceFall;
      const auto& dvinfo=activeSource->info().dolbyVision;
      snapshot_.sourceDoviProfile=dvinfo.present?dvinfo.profile:0;
      snapshot_.sourceDoviRpu=dvinfo.present&&dvinfo.rpuDeclared;
      snapshot_.sourceDoviEnhancement=dvinfo.present&&dvinfo.enhancementLayer;
      snapshot_.sourceHdr10Plus=activeSource->info().hdr10Plus.present;}
            // Every source is resampled to the screen with the high-quality present
            // path (bounded Catmull-Rom up, Catmull-Rom widened by the shrink factor
            // down; PresentBlit.hlsl).
            // Until 2026-11-18 streaming was the only path that was: fullscreen captures and files
            // were magnified bilinearly (soft) and shrunk without filtering (jagged).
            // VEYRA_TEST_PRESENT_BILINEAR=1 restores the old sampling for comparison.
            static const bool bilinearPresent=GetEnvironmentVariableW(L"VEYRA_TEST_PRESENT_BILINEAR",nullptr,0)>0;
            gd.highQualityPresentation=!bilinearPresent;
            gd.rgbInput=isImage;
            auto ingressBitDepth=[](pipeline::SourcePixelFormat format){return format==pipeline::SourcePixelFormat::P010||format==pipeline::SourcePixelFormat::V210||format==pipeline::SourcePixelFormat::Rgb10xLE?10u:format==pipeline::SourcePixelFormat::P016?16u:8u;};
            gd.captureBitDepth=ingressBitDepth(activeSource->info().color.pixelFormat);
            gd.stillImage=isImage;
            auto captureGraphColor=activeSource->info().color;
            const bool nvidiaAdapter=ctx.adapter().isNvidia;
            // AMD adapters run NR on the lmxxf runtime. Both the normalization
            // and the stage description used to drop NR on every non-NVIDIA
            // adapter, so AMD NR never ran in the product (field log 2026-10-05,
            // RX 9070 XT: "normalized ... nr=false").
            const bool amdNrAdapter=ctx.adapter().vendorId==0x1002;
            auto stageRequest=[&](const PlayerOptions& o){
                StageRequest r;r.nr=o.nr;r.sr=o.sr;r.fg=o.fg;r.fgMultiplier=o.fgMultiplier;
                r.width=width;r.height=height;r.stillImage=isImage;r.nvidiaAdapter=nvidiaAdapter;r.amdNr=amdNrAdapter;
                r.nodeOrder=o.nodeOrder?&*o.nodeOrder:nullptr;return r;
            };
            describeStages(stageRequest(options),options.snapshot(),gd);
            gd.noFeatures=false;
            gd.hdrOutput=options.settings.useHdrPreview(gd.hdrInput,displayHdrActive());
            veyra::log::info("display-color",std::format("hdrInput={} hdrOutput={} forceSdrPreview={}",gd.hdrInput,gd.hdrOutput,options.settings.forceSdrPreview));
            gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
            gd.compatibilityPreflight=[this](const auto& desc,const auto& device){return checkFgCompatibility(desc,device,stop_);};
            std::wstring backendRecoveryWarning;
            bool nrAutoPoolUnavailable=false;
            std::wstring nrAutoStatus;
            auto openPreparedPresenter=[&](const PlayerOptions& selected,bool createViews=true){
                // NGX creation cannot be interrupted. Observe exit when it
                // returns, before touching a UI-owned HWND or creating views.
                return !stop_&&presenter.open(ctx,window,*graph,selected.settings.captureCompatible,!isCapture&&!isImage)&&
                    !stop_&&(!createViews||graph->createViews());
            };
            auto initializePreview=[&](pipeline::EnhanceGraphDesc& desc,PlayerOptions& selected,bool preserveWorkingFg=false,bool preserveWorkingNr=false){
                backendRecoveryWarning.clear();
                auto supported=selected.snapshot();
                if(disableUnsupportedNvidiaEffects(supported,nvidiaAdapter,amdNrAdapter)){
                    selected=PlayerOptions::from(supported,selected.nodeOrder,&selected);
                    // Same stage rules as every other build: AMD FSR upscaling
                    // survives the normalization, it used to be dropped here.
                    describeStages(stageRequest(selected),selected.snapshot(),desc);
                    backendRecoveryWarning=amdNrAdapter?L"This GPU does not support NVIDIA enhancements; upscaling and DLSS are off, NR uses the AMD runtime, and the XeSS choice is kept":
                        L"This GPU does not support NVIDIA enhancements; NR, super resolution and DLSS are off, the XeSS choice is kept";
                    veyra::log::warn("capability","normalized requested NVIDIA effects before graph creation; applied settings reflect actual disabled stages");
                }
                for(unsigned attempt=0;attempt<6;++attempt){
                    if(stop_)return false;
                    desc.vfgAsyncSubmission=!isImage&&selected.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&
                        GetEnvironmentVariableW(L"VEYRA_TEST_VFG_SYNC_SUBMIT",nullptr,0)==0;
                    desc.vfgFastFallback=desc.vfgAsyncSubmission&&selected.settings.vfgAutoRegulate&&testReplayAsCard&&selected.settings.multiplier==2&&
                        GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_LIVE_PACING",nullptr,0)==0;
                    // NR inference reuse belongs to the realtime preview only:
                    // export, the image path and the paused frame infer every frame.
                    desc.nrReusePreview=!isImage;
                    desc.videoHdr=selected.settings.videoHdr;
                    desc.hdrOutput=selected.settings.useHdrPreview(desc.hdrInput,displayHdrActive());
                    // Auto is optional and preview-only; fixed/export policies
                    // retain their existing contract. A prewarmed graph has no pool.
                    desc.nrAutoPoolLayer.reset();
                    const auto automatic=std::find(desc.nrLayersSizePolicy.begin(),desc.nrLayersSizePolicy.end(),pipeline::NrSizePolicy::Auto);
                    const bool autoRequested=selected.nr&&automatic!=desc.nrLayersSizePolicy.end();
                    const bool tested=GetEnvironmentVariableW(L"VEYRA_TEST_NR_AUTO_POOL",nullptr,0)>0;
                    const bool admitted=(tested||(autoRequested&&automatic==desc.nrLayersSizePolicy.begin()&&selected.settings.activeAutoNrLayerCount()==1))&&
                       !nrAutoPoolUnavailable&&selected.settings.activeNrLayerCount()<=2&&
                       !isCapture&&!isImage&&desc.enableNr&&!desc.enableSr&&!desc.enableFg&&
                       !desc.hdrWorking()&&!desc.videoHdr.enabled&&!desc.color.enabled&&!desc.additionalColorCount&&!desc.antiAliasActive()&&!desc.beautyActive()&&!desc.runtimeNodeOrder&&desc.nrHoldStrength==0&&
                       desc.nrReuse==NrReuseMode::Off&&
                       currentNrRuntime(desc.nrRuntime)==NrRuntime::Ampere&&desc.opticalFlowBackend==OpticalFlowBackend::Nvidia&&
                       desc.nrMotion==MotionSource::OpticalFlow&&
                       nvidiaAdapter&&ctx.adapter().deviceId==0x2f04&&desc.workWidth==1920&&desc.workHeight==1080&&
                       desc.sourceWidth==1920&&desc.sourceHeight==1080&&
                       !desc.nrLayersExtent.empty()&&desc.nrLayersExtent[0]==pipeline::Extent{1920,1080}&&
                       std::none_of(desc.nrLayersTemporal.begin(),desc.nrLayersTemporal.end(),[](bool value){return value;});
                    nrAutoStatus.clear();
                    if(admitted)desc.nrAutoPoolLayer=0;
                    else if(autoRequested)nrAutoStatus=nrAutoPoolUnavailable?
                        L"Auto NR instance pool unavailable; keeping 1080p NR":L"Auto NR is inactive for this configuration; keeping 1080p NR";
                    // The only graph allowed beside an inactive NR adapter is
                    // effects-off. Evict before any other NGX initialization.
                    if(recentGraphCache.hasCachedGraph()&&!RecentGraphCache::effectsOff(desc)){
                        recentGraphCache.evict("enhancement-build");
                        graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,&coreCache);
                    }
                    uint64_t buildBudget=0,buildUsage=0;const bool measuredBefore=ctx.videoMemoryInfo(buildBudget,buildUsage);
                    const bool adopted=gpu->adopt(selected.snapshot(),desc);
                    if(!adopted&&!gpu->configureQueue(desc,!isCapture&&!isImage))return false;
                    bool opened=adopted||graph->initialize(desc);
                    if(stop_){veyra::log::info("engine","preview initialization cancelled after graph creation");return false;}
                    auto failure=graph->failedBackend();
                    if(opened){
                        opened=openPreparedPresenter(selected);
                        failure=FailedBackend::Infrastructure;
                        if(opened&&graph->xessEnabled()&&!presenter.xessActive()){opened=false;failure=FailedBackend::Fg;}
                        if(opened&&graph->fsrEnabled()&&!graph->fsrActive()){opened=false;failure=FailedBackend::Fg;}
                        // Requested AMD FSR upscaling that could not be created
                        // (missing local runtime, unsupported layout) degrades
                        // to the plain scale path with a visible warning.
                        if(opened&&graph->fsrSrRequested()&&!graph->fsrSrEnabled()){opened=false;failure=FailedBackend::Sr;}
                    }
                if(stop_)return false;
                if(opened){
                    uint64_t afterBudget=0,afterUsage=0;
                    const auto newBytes=measuredBefore&&ctx.videoMemoryInfo(afterBudget,afterUsage)&&afterUsage>buildUsage?afterUsage-buildUsage:0;
                    graphBytes=adopted?graphBytes+newBytes:newBytes;
                    {std::lock_guard lock(mutex_);snapshot_.nrAutoStatus=nrAutoStatus;snapshot_.nrAutoActive=false;
                     snapshot_.nrAutoPercent=snapshot_.nrAutoWidth=snapshot_.nrAutoHeight=0;}
                    return true;
                }
                if(desc.nrAutoPoolLayer&&failure==FailedBackend::Nr&&SUCCEEDED(ctx.device()->GetDeviceRemovedReason())){
                    nrAutoPoolUnavailable=true;graph->shutdown();
                    veyra::log::warn("nr-auto","pool initialization unavailable; retrying the same NR settings at fixed 1080p");
                    continue;
                }
                // A live backend/multiplier switch must not turn a working FG
                // session into effects-off playback when the new provider fails.
                // FSR 4 ML that the provider does not offer switches to FSR 3.1
                // below rather than rolling the request back.
                if(preserveWorkingFg&&failure==FailedBackend::Fg&&selected.settings.frameGenerationBackend!=FrameGenerationBackend::Fsr4)return false;
                if(preserveWorkingNr&&failure==FailedBackend::Nr)return false;
                auto reduced=selected.snapshot();
                // Requested multiplier above this GPU's capability: clamp to what
                // the runtime supports and say so, instead of dropping frame
                // generation entirely (the old behaviour users saw as "picked 4X
                // and the frame generation just vanished").
                bool clampedOverCapability=false;
                int capabilityGeneratedFrames=-1;
                if(failure==FailedBackend::Fg&&reduced.multiplier>1){
                    if(reduced.frameGenerationBackend==FrameGenerationBackend::Dlss)capabilityGeneratedFrames=graph->fgMultiFrameCountMax();
                    else if(reduced.frameGenerationBackend==FrameGenerationBackend::XeSS){
                        const auto xessState=gfx::XessMfgUnlock::snapshot();
                        if(xessState.applied)capabilityGeneratedFrames=int(xessState.maxInterpolations);
                    }
                    else if(fsrFrameGeneration(reduced.frameGenerationBackend)){
                        capabilityGeneratedFrames=int((graph->fsrActive()?1u:0u));
                    }
                    else if(reduced.frameGenerationBackend==FrameGenerationBackend::AmdFrc){
                        capabilityGeneratedFrames=int((graph->amfFrcActive()?1u:0u));
                    }
                }
                if(capabilityGeneratedFrames>0&&int(reduced.multiplier)-1>capabilityGeneratedFrames){
                    const uint32_t clamped=uint32_t(capabilityGeneratedFrames+1);
                    veyra::log::warn("backend-recovery",std::format("requested multiplier {} exceeds capability maxGeneratedFrames={}; applying {}X with frame generation kept",reduced.multiplier,capabilityGeneratedFrames,clamped));
                    reduced.multiplier=clamped;
                    clampedOverCapability=true;
                    if(!backendRecoveryWarning.empty())backendRecoveryWarning+=L"; ";
                    backendRecoveryWarning+=std::format(L"Up to {}X frame generation is supported now; applied as {}X",clamped,clamped);
                }
                if(FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
                if(!clampedOverCapability&&!disableFailedBackend(reduced,failure))return false;
                    veyra::log::warn("backend-recovery",std::format("initialization failed component={} attempt={} revision={} -> nr={} sr={} multiplier={}; original SDK error above",unsigned(failure),attempt+1,reduced.revision,reduced.nr,reduced.sr,reduced.multiplier));
                    if(!backendRecoveryWarning.empty())backendRecoveryWarning+=L"; ";
                    const uint32_t ngxResult=uint32_t(graph->failedNgxResult());
                    if(selected.settings.frameGenerationBackend==FrameGenerationBackend::Anr&&failure==FailedBackend::Fg)
                        backendRecoveryWarning+=L"ANR frame generation could not start and has been turned off. Check its runtime, driver and processing size (at least 640×360); see the log for details.";
                    else if(selected.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&failure==FailedBackend::Fg)
                        backendRecoveryWarning+=L"NVIDIA VFG could not start (requires an Ada / Blackwell GPU, VFG runtime and a supported output format). Frame generation is off; see the log.";
                    else if(selected.settings.frameGenerationBackend==FrameGenerationBackend::AmdFrc&&failure==FailedBackend::Fg)
                        backendRecoveryWarning+=L"AMD FRC could not start (needs an AMD GPU and the AMF runtime from its driver); frame generation turned off. See the log";
                    else if(selected.settings.frameGenerationBackend==FrameGenerationBackend::Fsr4&&failure==FailedBackend::Fg)
                    {
                        backendRecoveryWarning+=L"FSR 4 ML provider unavailable (needs a supported RX 9000 and driver); switched to FSR 3.1. See the log";
                        veyra::log::warn("fsr-fg","FSR 4 ML unavailable on this adapter/driver; switching to FSR 3.1 at 2X");
                    }
                    else if((ngxResult&0xFFF00000u)==0xBAD00000u)
                        backendRecoveryWarning+=std::wstring(backendFailureName(failure))+L": "+ngxFailureHint(ngxResult)+std::format(L" (NGX 0x{:08X}, driver {}); effects that depend on it are off",ngxResult,ctx.adapter().driverVersion);
                    else if(!clampedOverCapability)
                        backendRecoveryWarning+=std::wstring(backendFailureName(failure))+L" failed to initialize; effects that depend on it are off. See the diagnostic log";
                    if(!graph->driverRequirementNotice().empty())backendRecoveryWarning+=L"; "+graph->driverRequirementNotice();
                    if(failure==FailedBackend::Fg&&(ngx::AmpereMfgUnlock::applied()||(ngx::DlssgTransfusion::applied()&&ngx::DlssgTransfusion::snapshot().target==ngx::DlssgTransfusion::Target::Ampere)))backendRecoveryWarning+=L"; the RTX 30 frame generation unlock is applied, but with the current driver/runtime the runtime does not offer frame generation. You can use AMD FSR frame generation instead";

                    if(!ring.drainQueue()||!ring.discardRecording())return false;
                    if(!presenter.prepareGraphChange())return false;
                    graph->shutdown();selected=PlayerOptions::from(reduced,selected.nodeOrder,&selected);
                    describeStages(stageRequest(selected),selected.snapshot(),desc);
                }
                return false;
            };
            auto publishFrameGenerationCapabilities=[&]{
                // Read the active provider after every rebuild or rollback;
                // an initial effects-off graph has no DLSS capability yet.
                std::lock_guard lock(mutex_);
                fgMultiFrameMaxCap_=graph->fgMultiFrameCountMax();
                snapshot_.fgMultiFrameMax=fgMultiFrameMaxCap_;
                snapshot_.fgCapabilityKnown=fgMultiFrameMaxCap_>0;
                snapshot_.dlssgLiteState=graph->dlssgLiteState();
                const auto xessState=gfx::XessMfgUnlock::snapshot();
                // Unknown until the XeSS provider actually ran: a DLSS-only
                // session must not restrict the XeSS choices in the UI.
                xessMaxInterpolatedFramesCap_=presenter.xessActive()?int(std::max(1u,xessState.maxInterpolations)):0;
                snapshot_.xessMaxInterpolatedFrames=xessMaxInterpolatedFramesCap_;
                // AMD FSR ceiling comes from the active graph provider; 0 keeps the
                // settings UI on its static 2X fallback until a session ran.
                fsrMaxGeneratedFramesCap_=graph->fsrEnabled()&&graph->fsrActive()?int((graph->fsrActive()?1u:0u)):0;
                snapshot_.fsrMaxGeneratedFrames=fsrMaxGeneratedFramesCap_;
                snapshot_.fsrProviderVersion=graph->fsrProviderVersion();
                snapshot_.hudLockState=graph->hudLockState();
                if(fgMultiFrameMaxCap_>0)veyra::log::info("settings",std::format("frame-generation capability: maxGeneratedFrames={} maxMultiplier={}",fgMultiFrameMaxCap_,fgMultiFrameMaxCap_+1));
                veyra::log::info("settings",std::format("xess capability: active={} maxInterpolatedFrames={} unlockApplied={} (active context only)",presenter.xessActive(),xessMaxInterpolatedFramesCap_,xessState.applied));
                if(fsrMaxGeneratedFramesCap_>0)veyra::log::info("settings",std::format("fsr capability: generatedPerPresent={} => maxMultiplier={}",fsrMaxGeneratedFramesCap_,fsrMaxGeneratedFramesCap_+1));
            };
            const auto initialRequested=options.snapshot();
            const bool previewInitialized=initializePreview(gd,options);
            if(stop_)break;
            publishFrameGenerationCapabilities();
            if(!previewInitialized){status(L"Video initialization failed; see the diagnostic log of the component involved",true);break;}
            if(!backendRecoveryWarning.empty()){
                std::lock_guard lock(mutex_);desired_.rejectVideoRequest(initialRequested,options.snapshot());snapshot_.desired=desired_;snapshot_.rejectedRevision=initialRequested.revision;snapshot_.backendWarning=backendRecoveryWarning;
            }
            if(!nvidiaAdapter&&nvidiaOnlyStagesRequested(options.nr,options.sr,options.fg,options.settings,amdNrAdapter)){
                veyra::log::warn("capability",std::format("non-NVIDIA adapter disabled requested features: nr={} sr={} fgBackend={} flowBackend={}",options.nr,options.sr,frameGenerationBackendName(options.settings.frameGenerationBackend),opticalFlowBackendName(options.settings.opticalFlowBackend)));
                status(L"Not an NVIDIA adapter: NR, NVIDIA super resolution and DLSS are disabled; XeSS preview and AMD optical flow are available",false);
            }
            if(gd.hdrInput)status(gd.hdrOutput?L"HDR input · HDR kept for enhancement/display":L"HDR input · tone-mapped to SDR, then enhanced/shown (1000-nit reference peak)",false);
            if(graph->xessEnabled()&&!presenter.xessActive())status(L"XeSS not enabled: runtime or device incompatible; using normal presentation",false);
            {
                std::lock_guard lock(mutex_);
                if(!nvidiaAdapter&&nvidiaOnlyStagesRequested(options.nr,options.sr,options.fg,options.settings,amdNrAdapter))snapshot_.backendWarning=L"This GPU does not support the selected NVIDIA enhancement";
                if(graph->xessEnabled()&&!presenter.xessActive())snapshot_.backendWarning=L"XeSS failed to initialize; using normal presentation";
            }
            {std::lock_guard lock(mutex_);snapshot_.duration=duration;snapshot_.nominalSourceFps=isImage?0:activeSource->info().averageFps;snapshot_.running=true;snapshot_.transport=TransportState::Playing;snapshot_.image=isImage;snapshot_.capture=isCapture;snapshot_.applied=options.snapshot();snapshot_.desired=desired_;}
            status(isImage?L"Image enhanced; it can be saved as PNG/JPEG":std::format(L"{} | input {}×{} / base {}×{} / NR {}×{} / optical flow {}×{} / FG and output {}×{} | {}",isMoonlight?L"PC streaming":isXbox?L"Xbox streaming":isCapture?L"Live stream":L"Play",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.flowWidth,gd.flowHeight,gd.workWidth,gd.workHeight,gd.nrBeforeSr?L"Low latency · NR first, then SR":gd.nrWidth<gd.workWidth?L"Processed internally in real time and written back":L"Native NR (higher performance cost)"));
            pipeline::EnhanceGraph::FrameOutputs out;bool reset=true,hasOutput=false,audioRebuffering=false,seekPreviewPending=false;
            bool initialRemoteFramePending=isStream;
            bool initialFileFramePending=!isImage&&!isCapture;
            uint64_t activeSeekId=0;
            // When the demuxer finished re-positioning; the gap to the first presented
            // frame is decode + first enhanced frame.
            Clock::time_point seekDemuxDone{};
            Clock::time_point seekStarted{};
            uint64_t seekDecoded=0;
            bool fileAwaitingVideo=true,fileAudioAlignPending=true,fileInputEnded=false;double lastFilePresentedMs=0,lastFilePresentLateness=0;
            TimingWindow presentEntryDeviation,presentReturnDeviation;
            // Network sources and files on a server share ONE connection between picture and sound:
            // the audio pipeline reads the same demux session instead of opening the address again.
            const auto demuxSession=isImage||isCapture?std::shared_ptr<media::DemuxSession>{}:source.sharedSession();
            // A DASH source (Bilibili) has no audio in its video container: the audio
            // arrives as its own address, so that address wins over the shared video
            // session, which carries no audio stream at all.
            const bool separateAudio=!options.audioPath.empty();
            const std::wstring audioTarget=separateAudio?options.audioPath:path;
            // Opening the audio address runs FFmpeg's demuxer, which can throw. Without
            // this guard a failure there escapes into the engine's outer catch and ends
            // the whole session, which is exactly what "bad allocation" was reporting.
            bool audioOpened=false;
            try{
                // The DASH audio address needs the same Referer/UA the video one uses.
                audioOpened=!isImage&&!isCapture&&((demuxSession&&!separateAudio)
                    ?audioPipe.open(demuxSession)
                    :audioPipe.open(audioTarget,&stop_,options.network.requestHeaders,options.network.userAgent));
            }catch(const std::exception& e){
                veyra::log::error("audio",std::format("open failed: {} (separateAudio={} targetLen={})",
                                                      e.what(),separateAudio?1:0,audioTarget.size()));
                audioOpened=false;
            }
            if(audioOpened){
                {std::lock_guard lock(mutex_);snapshot_.audioTracks=audioPipe.tracks();snapshot_.selectedAudioTrack=audioPipe.selectedTrack();}
                audioPipe.holdForVideo();audioPipe.startThread(&audio,true);audioStarted=true;
                std::lock_guard lock(mutex_);snapshot_.audioAvailable=true;
                veyra::log::info("audio",std::format("pipeline started separateAudio={} tracks={}",separateAudio?1:0,audioPipe.tracks().size()));
            }else if(demuxSession){
                demuxSession->selectAudioStream(-1);   // nobody reads audio: stop queueing it
                veyra::log::info("net-session","no audio pipeline for this source; audio packets are dropped");
            }
            auto holdFileAudio=[&]{if(audioStarted)audioPipe.holdForVideo();fileAwaitingVideo=true;fileInputEnded=false;};
            TimingWindow captureAges,scheduleWaits,processTimes,presentTimes,decodeTimes,gpuReadyTimes;
            std::array<TimingWindow,size_t(diagnostics::GpuStage::Count)> gpuStageTimes;
            std::array<uint64_t,size_t(diagnostics::GpuStage::Count)> lastGpuSampleEnd{};
            auto nextTimingLog=Clock::now()+std::chrono::seconds(1);
            // Video memory watchdog. A field log (RTX 5060 Ti, 2026-10-01) grew +3 GB a minute while
            // fullscreen, kept it after leaving fullscreen, and every pass slowed down once usage
            // passed the budget (5 fps). Unreproduced here. On growth beyond max(2 GiB, budget/4)
            // with no settings change the log names every third-party module, and the loop
            // rebuilds first the presentation swapchain, then (if that released less than half)
            // the processing graph too, logging what each released: the swapchain step points
            // at the driver or an overlay hooked on it, the graph step at our own passes.
            uint64_t vramFloor=0,vramRevision=~0ull,vramRecoverBefore=0,vramRecoverGrowth=0;auto vramSettleUntil=Clock::now(),vramRecoverCheckAt=Clock::now();
            bool vramWarned=false;int vramRecoverStep=0,vramRecoverDone=0,vramRecoveries=0;
            bool vramFullscreenBlocked=false;unsigned vramGrowthSamples=0;
            bool vramProtectionEnabled=fullscreenMemoryProtection_.load();
            // VEYRA_TEST_VRAM_LEAK_MIB=n: hold on to n MiB more of video memory every second;
            // VEYRA_TEST_VRAM_LEAK_RELEASE=1 lets the swapchain rebuild free it (as a leak tied
            // to the swapchain would), otherwise nothing frees it.
            const uint64_t testLeakMiB=[]{wchar_t v[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_MIB",v,16)?uint64_t(_wtoi(v)):0ull;}();
            const uint64_t testLeakLimitMiB=[]{wchar_t v[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_LIMIT_MIB",v,16)?uint64_t(std::max(0,_wtoi(v))):0ull;}();
            const bool testLeakReleases=GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_RELEASE",nullptr,0)>0;
            const bool testLeakFullscreenOnly=GetEnvironmentVariableW(L"VEYRA_TEST_VRAM_LEAK_FULLSCREEN_ONLY",nullptr,0)>0;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> testLeak;
            auto watchVram=[&](uint64_t usage,uint64_t budget){
                (void)recentGraphCache.checkBudget();
                bool fullscreen=false;
                {std::lock_guard lock(mutex_);fullscreen=presentation_.fullscreen;}
                if(testLeakMiB&&(!testLeakLimitMiB||uint64_t(testLeak.size()+1)*testLeakMiB<=testLeakLimitMiB)&&(!testLeakFullscreenOnly||fullscreen)){
                    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
                    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=testLeakMiB<<20;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    Microsoft::WRL::ComPtr<ID3D12Resource> r;
                    const auto hr=ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r));
                    if(SUCCEEDED(hr))testLeak.push_back(r);
                    log::info("vram-test",std::format("allocationMiB={} heldMiB={} limitMiB={} hr=0x{:X}",testLeakMiB,uint64_t(testLeak.size())*testLeakMiB,testLeakLimitMiB,unsigned(hr)));
                }
                const bool protection=fullscreenMemoryProtection_.load();
                if(protection!=vramProtectionEnabled){
                    vramProtectionEnabled=protection;vramFullscreenBlocked=false;vramGrowthSamples=0;vramFloor=0;
                    vramSettleUntil=Clock::now()+std::chrono::seconds(10);
                }
                if(!usage)return;
                // Leaving fullscreen stopped further growth in both field logs. Preserve
                // the source/effects and avoid repeated failed graph rebuilds. This only
                // contains the observed trigger; it does not identify the allocation owner
                // or promise that already retained memory is returned.
                if(vramFullscreenBlocked)return;
                if(vramRecoverDone){
                    if(Clock::now()<vramRecoverCheckAt)return;
                    const int64_t released=(int64_t(vramRecoverBefore)-int64_t(usage))/(1<<20);
                    veyra::log::warn("vram-watch",std::format("{} rebuild released {} MiB of {} MiB growth (before {} MiB, now {} MiB)",
                        vramRecoverDone==1?"swapchain":"graph+swapchain",released,vramRecoverGrowth>>20,vramRecoverBefore>>20,usage>>20));
                    // The graph is rebuilt only with room for it: a rebuild that runs out of budget
                    // stops the session, which is worse than running slowly.
                    if(vramRecoverDone==1&&released<int64_t(vramRecoverGrowth>>21)){
                        if(budget&&usage<budget/100*85)vramRecoverStep=2;
                        else veyra::log::warn("vram-watch","graph rebuild skipped: too close to the budget to rebuild safely");
                    }
                    vramRecoverDone=0;vramFloor=0;vramSettleUntil=Clock::now()+std::chrono::seconds(5);
                    return;
                }
                if(vramRecoverStep)return;
                if(options.settings.revision!=vramRevision){vramRevision=options.settings.revision;vramFloor=0;vramGrowthSamples=0;vramSettleUntil=Clock::now()+std::chrono::seconds(10);}
                if(Clock::now()<vramSettleUntil)return;
                if(!vramFloor||usage<vramFloor)vramFloor=usage;
                const uint64_t growth=usage-vramFloor;
                const uint64_t fullscreenLimit=std::max<uint64_t>(512ull<<20,std::min<uint64_t>(2048ull<<20,budget/8));
                if(vramProtectionEnabled&&fullscreen&&growth>fullscreenLimit){
                    if(++vramGrowthSamples<3)return;
                    vramFullscreenBlocked=true;vramWarned=true;
                    const std::wstring injected=gfx::riskyInjections();
                    {
                        std::lock_guard lock(mutex_);
                        snapshot_.vramRunawayMiB=growth>>20;snapshot_.vramInjected=injected;snapshot_.vramFullscreenUnsafe=true;
                    }
                    veyra::log::warn("vram-watch",std::format("sustained fullscreen growth {} MiB (usage {} / budget {} MiB); requesting windowed playback, preserving source/effects; allocation owner unconfirmed; modules: {}",
                        growth>>20,usage>>20,budget>>20,gfx::thirdPartyModules()));
                    return;
                }
                vramGrowthSamples=0;
                const uint64_t limit=std::max<uint64_t>(2048ull<<20,budget/4);
                if(growth<=limit)return;
                // Module presence is diagnostic context, not proof of allocation ownership.
                // Evaluate a bounded recovery by the measured memory result, regardless of
                // whether NVIDIA's plug-in (also present in stable local runs) is loaded.
                const std::wstring injected=gfx::riskyInjections();
                if(!vramWarned){
                    vramWarned=true;
                    veyra::log::warn("vram-watch",std::format("video memory grew {} MiB with no settings change (now {} of {} MiB budget); third-party modules: {}",
                        growth>>20,usage>>20,budget>>20,gfx::thirdPartyModules()));
                    std::lock_guard lock(mutex_);snapshot_.vramRunawayMiB=growth>>20;snapshot_.vramInjected=injected;
                }else veyra::log::warn("vram-watch",std::format("video memory grew {} MiB again (now {} MiB)",growth>>20,usage>>20));
                if(vramRecoveries<3){++vramRecoveries;vramRecoverStep=1;vramRecoverGrowth=growth;}
                else{vramFloor=usage;}   // give up rebuilding; report again only on further growth
            };
            double playbackRate=1.0;
            auto anchor=Clock::now(),statsStart=anchor;double anchorMs=0;uint64_t frames=0,sourceFrames=0;bool wasPaused=false;double discardBefore=0;
            double lastAudioClockMs=0;bool audioClockExhausted=false;auto audioTailAnchor=anchor;
            bool publishedAudioRecovery=false;HRESULT publishedAudioError=S_OK;uint64_t publishedAudioRecoveries=0;
            const bool injectFileEndpointLoss=GetEnvironmentVariableW(L"VEYRA_TEST_FILE_ENDPOINT_LOSS",nullptr,0)>0;
            bool fileEndpointLossInjected=false;
            auto publishAudioStatus=[&]{
                const bool recovering=audioPipe.endpointRecovering();const auto error=audioPipe.endpointError();const auto count=audioPipe.endpointRecoveries();
                {std::lock_guard lock(mutex_);snapshot_.audioRebuffering=audioPipe.waitingForVideo();snapshot_.audioVideoWaits=audioPipe.videoWaitCount();}
                if(recovering!=publishedAudioRecovery||error!=publishedAudioError||count!=publishedAudioRecoveries){
                    publishedAudioRecovery=recovering;publishedAudioError=error;publishedAudioRecoveries=count;
                    std::lock_guard lock(mutex_);snapshot_.audioInputChannels=audioPipe.pcmFormat().channels;snapshot_.audioOutputChannels=audio.outputFormat().channels;snapshot_.audioEndpointRecovering=recovering;snapshot_.audioEndpointError=error;snapshot_.audioEndpointRecoveries=count;
                }
            };
            PresentationScheduler liveTimeline;uint64_t submitted=0,expired=0;std::deque<int64_t> submissionTimes;
            DeadlineWait deadlineWait;
            // Live capture: the GPU fence of a submitted frame wakes this thread
            // directly. The 0.2 ms ready poll alone left a finished frame waiting
            // up to one timer slice (plus timer jitter) before its Present.
            // Realtime file preview scheduling (P2/P3): expired decoded
            // candidates lose their enhancement opportunity against the audio
            // master clock; FG admission predicts pair completion; XeSS-FG
            // generation is suppressed over stable intervals when sustained
            // late. Export/images/paused frames never enter these paths.
            uint64_t previewSkippedTotal=0,previewSkippedSinceSubmit=0,previewSkipRetained=0;int64_t nextPreviewSkipLog=0;
            // Media PTS of the last frame the graph actually processed. The
            // temporal stages care about the gap to that frame, not about how
            // many candidates were dropped on the way (see boundedSkip below).
            double lastProcessedPtsMs=std::numeric_limits<double>::quiet_NaN();
            bool previewSkipSinceProcess=false;
            // Cumulative provider-submission totals already fed to the frame
            // flow. The AMD provider reports presentation from its own thread,
            // so per-call deltas systematically miss updates that land just
            // after a submission; feeding the cumulative difference at the
            // next submission cannot lose them. Reset with the presenter.
            ProviderSubmissionCounter xessSubmissions;
            double playbackSpeedLastPts=0;Clock::time_point playbackSpeedLastWall{};
            CaptureHalfRate captureSampler;
            // Content cadence for live sources (ContentDedup.h): in the auto / 30 / 50 /
            // 60 fps content modes, repeated pictures (every component within the
            // capture-noise band, PictureCompare.h) are dropped before any GPU work and
            // kept frames get content timestamps. "60->30" is the plain
            // every-second-sample CaptureHalfRate again (user 2026-10-12); since
            // 2026-10-27 the callback's picture numbers move its phase onto the
            // first copies of a 30 fps game, and since 2026-11-18 (issue #152) a due
            // sample repeating the last kept picture waits for the next new one, so a
            // game below 30 is not fed repeats (CaptureHalfRate.h).
            // VEYRA_TEST_CONTENT_DEDUP_LEGACY=1 restores the report-only content modes
            // for A/B runs.
            ContentDedup contentDedup;uint64_t dedupRevision=0;
            // Frame pacing repair (FramePacingRepair.h, from PushFPS): even times for the kept
            // frames of a locked game whose frames the console shows unevenly.
            FramePacingRepair pacingRepair;int64_t pacingLastPts=0;bool pacingHaveLast=false;int64_t nextPacingLog=0;
            ContentDedup::Stats dedupLogged{};
            static const bool legacyCadence=GetEnvironmentVariableW(L"VEYRA_TEST_CONTENT_DEDUP_LEGACY",nullptr,0)>0;
            const auto dedupContent=[](ContentRate content){return content!=ContentRate::Transport&&content!=ContentRate::Capture60To30;};
            // CPU frames are compared here.
            pipeline::PictureReference enginePictures{2};
            unsigned dedupRepeatMax8=0;uint64_t dedupRepeatMaxOver=0,dedupNewMinOver=UINT64_MAX;
            FrameLineageTracker lineageTracker;
            auto completedProcessing=[&](pipeline::FrameIdentity id){std::lock_guard lock(mutex_);if(snapshot_.applied.revision==id.settingsRevision)++snapshot_.processedCompleted;};
            auto host100ns=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;};
            // Live ingress stays latest-frame; file preview keeps the audio clock
            // and may skip expired enhancement opportunities, never PCM.
            // Graph, query collection and presentation remain on this owner.
            graph->recordGpuTimings();presenter.recordGpuTimings();
            struct LiveStats {uint64_t submitted=0,expired=0;double waitMs=0,presentMs=0,readyMs=0,ageMs=0,fps=0,ageP95=0,waitP95=0,presentP95=0,readyP95=0;diagnostics::GpuSample blit;};
            LiveStats liveStats;std::deque<int64_t> liveSubmissions;
            TimingWindow liveAges,liveWaits,liveReady;
            TimingWindow livePresent{std::chrono::seconds(1)};
            TimingWindow livePresentGpu{std::chrono::seconds(1)};
            FgRecoveryBudget fgBudget;uint64_t fgBudgetRevision=options.settings.revision;
            const bool vfgMeasuredReducedQueue=GetEnvironmentVariableW(L"VEYRA_TEST_VFG_FULL_REDUCED_QUEUE_COST",nullptr,0)==0;
            int64_t nextVfgQueueLog=0;
            VfgOverloadEstimate vfgOverload;
            const bool vfgRetainOverload=GetEnvironmentVariableW(L"VEYRA_TEST_VFG_NO_OVERLOAD_ESTIMATE",nullptr,0)==0;
            int64_t nextVfgOverloadLog=0;
            unsigned previewFgMultiplier=options.fgMultiplier;
            // Live output hint (DisplayFgBudget.h): a status-line suggestion only.
            // VEYRA_TEST_DISPLAY_HZ=<hz> replaces the measured refresh for it.
            DisplayFgBudgetGate displayFgGate;
            // Adaptive multiplier (AdaptiveFgMultiplier.h, from PushFPS): per-pair multiplier from
            // the measured content rate, the configured one being the ceiling.
            AdaptiveFgGate adaptiveFgGate;bool adaptiveFgWas=false;int64_t nextAdaptiveFgLog=0;
            static const double displayFgTestHz=[]{wchar_t v[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_DISPLAY_HZ",v,16)?std::clamp(_wtof(v),0.0,1000.0):0.0;}();
            // Live graph FG presents on a smoothed anchor (LiveArrivalSmoother).
            // VEYRA_TEST_LEGACY_LIVE_PACING=1 restores the callback anchor, the
            // 2 ms capacity wait and the lone VFG probe for A/B runs.
            static const bool legacyLivePacing=GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_LIVE_PACING",nullptr,0)>0;
            // A/B: the capacity wait before it followed the measured callback jitter.
            static const bool fixedCapacityWait=GetEnvironmentVariableW(L"VEYRA_TEST_FIXED_CAPACITY_WAIT",nullptr,0)>0;
            LiveArrivalSmoother liveArrival;int64_t liveGeneratedGrace=kLiveGeneratedGrace100ns,nextLiveAnchorLog=0;
            FgCostSettling vfgCostSettling;
            double vfgChosenFgMs=0,vfgFastFgMs=0;uint64_t vfgFastPairs=0,vfgFastBudgetPairs=0,vfgChosenPairs=0;int64_t vfgChosenAt=0,nextVfgFastLog=0;
            uint64_t generatedDropDeadline=0,generatedDropCapacity=0,generatedDropCap=0;
            LivePairLatency pairLatency;
            // Live FG pairs: automatic deadline margin (0-1 ms) on top of the phase.
            LiveDeadlineMargin deadlineMargin;
            // Live FG pairs rejected for a second: real frames without the FG phase.
            LiveFgRejectionHold fgRejectionHold;uint64_t fgHeldPairs=0,fgHeldProbes=0;
            const bool adaptiveCapturePhase=testReplayAsCard&&GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_CAPTURE_PHASE",nullptr,0)==0;
            static const double testArrivalJitterMs=[]{wchar_t v[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_ARRIVAL_JITTER_MS",v,16)?std::clamp(_wtof(v),0.0,10.0):0.0;}();
            uint32_t testArrivalSeed=7;int64_t testCardStart=0;double testCardFirstPtsMs=0;
            const unsigned testReplayDropEvery=[]{wchar_t value[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_REPLAY_DROP_EVERY",value,16)?unsigned(std::clamp(_wtoi(value),0,120)):0u;}();
            bool testReplayDropPending=false;
            int64_t nextFgAdmissionLog=0,nextFgRejectionLog=0;
            uint64_t fgAdmittedPairs=0,fgRejectedPairs=0,fgReducedPairs=0;int64_t nextFgReducedLog=0;
            std::atomic<uint64_t> historyResets=0,presentationDrains=0,presentationCompletedReal=0,presentationSkippedGenerated=0,presentationCancelledJobs=0;
            std::atomic<uint64_t> presentationGeneration{0};
            std::unique_ptr<LiveGpuScheduler> liveScheduler;
            struct CompletionWatch {
                uint64_t reflexFrame=0;
                int64_t captureArrival=0;
                int64_t phaseAnchor=0;
                uint64_t presentationGeneration=0;
                pipeline::EnhanceGraph::FrameOutputs output;
                std::shared_ptr<FrameFlowWindow> flow;
                Clock::time_point processStart;
                int64_t predictedGpuStart100ns=0;
                std::optional<double> gpuExecutionMs,fgExecutionMs,firstFgExecutionMs;
                bool real=false;
                // B1: presented before the GPU finished; the completion poll
                // records the GPU-ready time (from jobStart) instead of the job.
                bool presentedBeforeReady=false;
                Clock::time_point jobStart{};
                std::atomic<bool> ready=false;
                Clock::time_point readyObserved{};
                std::array<int64_t,pipeline::FrameBatch::Capacity> frameReadyObserved{};
            };
            int64_t nextFgDeadlineLog=0;
            std::vector<std::shared_ptr<CompletionWatch>> pendingCompletions;
            std::shared_ptr<FrameFlowWindow> frameFlow;
            const uint64_t runSessionId=[&]{std::lock_guard lock(mutex_);return snapshot_.sessionId;}();
            pipeline::ResetReason pendingResetCause=pipeline::ResetReason::Open;
            auto traceFrame=[&](diagnostics::TraceKind kind,pipeline::FrameIdentity identity,uint64_t batch,
                uint64_t fence,int64_t pts,uint32_t detail,uint32_t count,double ms){
                Logger::instance().recordFrame({host100ns(),runSessionId,identity,batch,fence,pts,kind,detail,count,ms});
            };
            std::optional<diagnostics::FrameFlowMetrics::ResetRecord> resetRecord;
            std::optional<diagnostics::FrameFlowMetrics::ResetRecord> lastResetRecord;
            bool rollbackWarmupPending=false;
            Clock::time_point resetStart{},resetStageStart{};
            auto markResetStage=[&](diagnostics::ResetStage stage){
                if(!resetRecord)return;
                const auto now=Clock::now();
                resetRecord->stageMs[size_t(stage)]=std::chrono::duration<double,std::milli>(now-resetStageStart).count();
                resetStageStart=now;
                if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
            };
            auto finishReset=[&](diagnostics::ResetOutcome outcome){
                if(!resetRecord)return;
                resetRecord->outcome=outcome;resetRecord->totalMs=elapsedMs(resetStart);
                lastResetRecord=resetRecord;
                if(outcome==diagnostics::ResetOutcome::RolledBack)rollbackWarmupPending=true;
                if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
                const auto& r=*resetRecord;const auto ms=[&](diagnostics::ResetStage s){return r.stageMs[size_t(s)].value_or(-1.0);};
                traceFrame(diagnostics::TraceKind::Reset,{r.epoch,r.settingsRevision,r.sourceFrameId},0,0,0,r.reason,unsigned(outcome),*r.totalMs);
                veyra::log::info("reset-lifecycle",std::format("session={} revision={} epoch={} source={} reason={} outcome={} drainMs={:.3f} destroyMs={:.3f} createMs={:.3f} warmupSubmitMs={:.3f} firstValidObserveMs={:.3f} totalMs={:.3f} (CPU observed; -1=not measured)",r.sessionId,r.settingsRevision,r.epoch,r.sourceFrameId,unsigned(r.reason),unsigned(r.outcome),ms(diagnostics::ResetStage::Drain),ms(diagnostics::ResetStage::Destroy),ms(diagnostics::ResetStage::Create),ms(diagnostics::ResetStage::Warmup),ms(diagnostics::ResetStage::FirstValid),*r.totalMs));
                resetRecord.reset();
            };
            auto completeReset=[&](pipeline::FrameIdentity identity){
                if(!resetRecord||resetRecord->settingsRevision!=identity.settingsRevision||resetRecord->epoch!=identity.epoch||resetRecord->sourceFrameId!=identity.sourceFrameId)return;
                markResetStage(diagnostics::ResetStage::FirstValid);
                finishReset(diagnostics::ResetOutcome::Completed);
            };
            OnExit resetExit{[&]{finishReset(stop_?diagnostics::ResetOutcome::Cancelled:diagnostics::ResetOutcome::Failed);}};
            auto collectTimings=[&]{
                auto record=[&](const diagnostics::GpuFrameTiming& sample){
                    const auto& fgCost=sample.gpu[size_t(diagnostics::GpuStage::FgBatch)];
                    for(auto& watch:pendingCompletions){
                        const auto& id=watch->output.batch.identity;
                        if(id.epoch==sample.identity.epoch&&id.settingsRevision==sample.identity.settingsRevision&&id.sourceFrameId==sample.identity.sourceFrameId){
                            if(const auto span=diagnostics::graphExecutionSpanMs(sample))watch->gpuExecutionMs=span;
                            if(fgCost.state==diagnostics::SampleState::Measured)watch->fgExecutionMs=fgCost.milliseconds;
                            const auto& first=sample.gpu[size_t(diagnostics::GpuStage::Fg1)];
                            if(first.state==diagnostics::SampleState::Measured)watch->firstFgExecutionMs=first.milliseconds;
                        }
                    }
                    const auto& blit=sample.gpu[size_t(diagnostics::GpuStage::Blit)];
                    if(sample.identity.settingsRevision==fgBudgetRevision&&blit.state==diagnostics::SampleState::Measured&&blit.milliseconds)livePresentGpu.add(*blit.milliseconds);
                    if(frameFlow)frameFlow->gpuFrame(sample,host100ns());
                    for(size_t i=0;i<sample.gpu.size();++i){const auto& gpu=sample.gpu[i];
                        if(gpu.state==diagnostics::SampleState::Measured&&gpu.milliseconds){
                            traceFrame(diagnostics::TraceKind::Gpu,sample.identity,0,0,0,unsigned(i),1,*gpu.milliseconds);
                            if(*gpu.milliseconds>=80.0)veyra::log::info("gpu-stage-stall",std::format("source={} epoch={} revision={} stage={} gpuMs={:.3f}",sample.identity.sourceFrameId,sample.identity.epoch,sample.identity.settingsRevision,i,*gpu.milliseconds));
                        }
                    }
                };
                // The graph object is replaced on rebuilds (core reuse, recent-graph
                // cache, prewarm adoption). Re-arm recording on whichever graph is
                // current; arming only the first one left every later graph
                // unmeasured ("not measured" after any effect change, 2026-10-05).
                graph->recordGpuTimings();
                for(const auto& sample:graph->takeGpuTimings())record(sample);
                for(const auto& sample:presenter.takeGpuTimings(ctx.fence()))record(sample);
            };
            const bool traceSubframes=GetEnvironmentVariableW(L"VEYRA_TEST_TRACE_SUBFRAMES",nullptr,0)>0;
            // Shared with the presenter: resolve each batch on this single
            // object so SDK status and generated counts are consumed once.
            auto pollCompletions=[&]{
                diagnostics::CpuStallTrace trace("completion-stall");
                collectTimings();
                for(auto it=pendingCompletions.begin();it!=pendingCompletions.end();){
                    auto& watch=**it;auto& batch=watch.output;
                    for(unsigned i=0;i<batch.batch.count;++i){const auto& item=batch.batch.frames[i];
                        if(!watch.frameReadyObserved[i]&&item.lease&&item.lease->ready()){
                            watch.frameReadyObserved[i]=host100ns();
                            if(traceSubframes)traceFrame(diagnostics::TraceKind::FrameReady,item.identity,batch.batch.batchId,item.lease->readyFence,item.pts100ns,item.subframe,1,elapsedMs(watch.processStart));
                        }
                    }
                    if(!graph->resolveGeneration(batch)){++it;continue;}
                    // The GPU may finish between the first poll and resolve.
                    // Collect its timestamps before removing this watch.
                    if(!watch.gpuExecutionMs)collectTimings();
                    unsigned valid=0,invalid=0,failed=0;
                    for(unsigned i=0;i<batch.batch.count;++i)if(batch.batch.frames[i].kind==pipeline::FrameKind::Generated){
                        if(batch.batch.frames[i].validity==pipeline::GenerationValidity::Valid)++valid;else ++invalid;
                        failed+=batch.batch.frames[i].validity==pipeline::GenerationValidity::Failed;
                    }
                    watch.flow->ready(batch.batch.batchId,watch.real,valid,invalid,host100ns());
                    traceFrame(diagnostics::TraceKind::Ready,batch.batch.identity,batch.batch.batchId,std::max(batch.videoFenceValue,batch.genFenceValue),batch.batch.b100ns,invalid,valid,elapsedMs(watch.processStart));
                    if(watch.real)completedProcessing(batch.batch.identity);
                    // A reduced (2X) group must not lower the full-group FG
                    // cost estimate, but its base cost is a normal sample -
                    // see FgRecoveryBudget::complete. A history reset, an FG
                    // recovery and a live VFG pair settling after a long skip
                    // (FgCostSettling) are warmup samples.
                    const bool currentCost=batch.batch.identity.settingsRevision==fgBudgetRevision&&
                        watch.presentationGeneration==presentationGeneration.load();
                    const auto vfgHeldUntil=vfgCostSettling.holdUntil();
                    const bool costSettling=currentCost&&isCapture&&!legacyLivePacing&&options.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&
                        vfgCostSettling.observe((batch.fgCandidates>0&&batch.fgEvaluated==0)||batch.fgFastForBudget,batch.fgEvaluated>0&&!batch.fgFast,host100ns());
                    // Each model's own time. The steady time is the low envelope: an
                    // evaluation after skips can cost 1.7x (see FgCostSettling).
                    if(currentCost&&watch.fgExecutionMs&&batch.fgEvaluated>0&&!batch.fgReduced&&!batch.historyReset&&!batch.fgRecovery){
                        auto& cost=batch.fgFast?vfgFastFgMs:vfgChosenFgMs;const double ms=*watch.fgExecutionMs;
                        cost=cost>0&&ms>=cost?cost+.1*(ms-cost):ms;
                        if(!batch.fgFast){vfgChosenAt=host100ns();++vfgChosenPairs;}
                    }
                    if(vfgCostSettling.holdUntil()!=vfgHeldUntil)veyra::log::info("vfg-probe",std::format("revision={} retryInMs={} (a VFG probe after a long skip stayed over budget; failed probes retry after 2, 4, then every 8 s)",options.settings.revision,(vfgCostSettling.holdUntil()-host100ns())/10000));
                    if(currentCost)fgBudget.complete(watch.gpuExecutionMs,batch.fgEvaluated>0,batch.historyReset||batch.fgRecovery||costSettling,host100ns(),watch.fgExecutionMs,!batch.fgReduced&&!batch.fgFast);
                    if(options.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&currentCost){
                        if(batch.historyReset)vfgOverload.reset();
                        if(!batch.historyReset&&!batch.fgRecovery&&!batch.fgReduced&&batch.fgEvaluated+1==options.fgMultiplier)
                            vfgOverload.observe(watch.fgExecutionMs,watch.firstFgExecutionMs,host100ns());
                    }
                    // A group whose frames were all evaluated but then marked a content
                    // duplicate or provider-disabled did the same GPU work, so its
                    // readiness is a valid phase sample. Learning only from displayed
                    // groups let samples expire during static/duplicate content (60->30:
                    // B held 33 ms) and re-converge for ~1 s once motion resumed.
                    // VEYRA_TEST_PAIR_PHASE_VALID_ONLY=1 restores the old rule for A/B.
                    static const bool phaseValidOnly=GetEnvironmentVariableW(L"VEYRA_TEST_PAIR_PHASE_VALID_ONLY",nullptr,0)>0;
                    // resolveGeneration clears hasGenerated when no frame stayed valid,
                    // so the evaluated group is recognised by its generated frames.
                    const bool phaseSample=phaseValidOnly?valid==batch.fgCandidates&&batch.hasGenerated:
                        batch.fgCandidates>0&&valid+invalid==batch.fgCandidates&&!failed;
                    if(adaptiveCapturePhase&&watch.phaseAnchor>0&&phaseSample&&
                       !batch.historyReset&&!batch.fgRecovery&&!costSettling&&!batch.fgFast&&
                       batch.batch.identity.settingsRevision==fgBudgetRevision&&watch.presentationGeneration==presentationGeneration.load()){
                        int64_t requiredDelay=0;
                        const auto observed=host100ns();
                        for(unsigned i=0;i<batch.batch.count;++i){const auto& item=batch.batch.frames[i];
                            // resolveGeneration can observe a fence after the
                            // first poll. Its current host time is a safe upper bound.
                            const auto ready=watch.frameReadyObserved[i]?watch.frameReadyObserved[i]:observed;
                            requiredDelay=std::max(requiredDelay,ready-watch.phaseAnchor+batch.batch.b100ns-item.pts100ns);
                        }
                        pairLatency.observe(observed,requiredDelay);
                    }
                    completeReset(batch.batch.identity);
                    watch.readyObserved=Clock::now();
                    if(watch.presentedBeforeReady&&watch.flow==frameFlow&&liveScheduler){
                        const double readyMs=std::max(0.0,std::chrono::duration<double,std::milli>(watch.readyObserved-watch.jobStart).count());
                        watch.flow->cpu(diagnostics::CpuStage::ReadyWait,readyMs,host100ns());
                        liveReady.add(readyMs);liveStats.readyMs=readyMs;liveStats.readyP95=liveReady.p95();
                    }
                    watch.ready=true;it=pendingCompletions.erase(it);
                }
                collectTimings();
            };
            if(!isImage){liveScheduler=std::make_unique<LiveGpuScheduler>();veyra::log::info("scheduler",std::format("single GPU owner thread={} capacity=2 source={} (bounded enhanced lookahead)",GetCurrentThreadId(),isCapture?"live":"file"));}
            PresentationCadence cadence;
            PresentationSettings presentationEffective;
            // presentationPaced: VSync keeps a one-deep display queue (latency
            // object). A failed wait turns pacing off for this configuration.
            // VEYRA_TEST_LEGACY_VSYNC_QUEUE=1 restores the unwaited VSync queue
            // (no read gate, waits only with the toggle/cap) for A/B runs.
            bool presentationPaced=false;
            static const bool legacyVsyncQueue=GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_VSYNC_QUEUE",nullptr,0)>0;
            // XeSS live presentation (latency-min B1): the presenter records on the
            // graph's own queue, so a real frame can be tagged and presented right
            // after its graph submission; GPU order keeps it after the enhancement.
            // One loop is then one XeLL cycle: sleep -> read -> submit -> present.
            // Waiting for the CPU fence first let the next source enter the same
            // cycle, so the following present opened the next cycle and slept
            // inside Present. VEYRA_TEST_XESS_PRESENT_AFTER_READY=1 restores the
            // fence wait; VEYRA_TEST_XESS_STACKED_READS=1 lets XeSS read the next
            // source while the previous one is still on the GPU, as before.
            static const bool xessPresentAfterReady=GetEnvironmentVariableW(L"VEYRA_TEST_XESS_PRESENT_AFTER_READY",nullptr,0)>0;
            static const bool xessStackedReads=GetEnvironmentVariableW(L"VEYRA_TEST_XESS_STACKED_READS",nullptr,0)>0;
            uint64_t pacingRevision=0,presenterGeneration=0;double pacingRefreshHz=0;
            auto applyPresentation=[&]{
                PresentationSettings requested;uint64_t revision;
                {std::lock_guard lock(mutex_);requested=presentation_;revision=presentationRevision_;}
                // Poll completed display/VRR queries before comparing policy.
                // It must not make synchronous driver calls on the GPU owner.
                const double refreshHz=presenter.displayRefreshHz();
                const bool capRefreshChanged=requested.outputRate==OutputRateMode::FollowDisplay&&std::abs(refreshHz-pacingRefreshHz)>0.001;
                // Variable refresh: the monitor's VRR availability is read again on events
                // (another monitor, a display change, re-detection, the VRR
                // confirmation); a change re-applies.
                const bool vrrChanged=presenter.vrrSync()!=VideoPresenter::vrrSyncWanted(requested,presenter.displayVrr());
                if(revision==pacingRevision&&presenterGeneration==presenter.generation()&&!vrrChanged&&!capRefreshChanged){
                    // Reflex dropped out at runtime: the queue-depth part of the
                    // low-latency setting stays, so only the status line changes.
                    if(presentationEffective.nvidiaLowLatency!=NvidiaLowLatency::Off&&!presenter.reflexActive()&&presenter.reflexFailed()){
                        presentationEffective.nvidiaLowLatency=NvidiaLowLatency::Off;
                        std::lock_guard lock(mutex_);snapshot_.presentationEffective=presentationEffective;snapshot_.presentationStatus=presenter.reflexDisablePending()?L"NVIDIA Low Latency call and disable failed; close the video and retry":
                            presenter.reflexStalled()?L"NVIDIA Low Latency: the driver waited too long every frame and was turned off for this session; using low queue depth":L"NVIDIA Low Latency call failed; using low queue depth. See logs.";
                    }
                    if(presentationPaced&&!presenter.pacingActive()){
                        // The sink already stopped waiting; reconfiguring would arm it again.
                        presentationPaced=false;cadence.reset();
                        std::lock_guard lock(mutex_);snapshot_.presentationStatus=L"Display queue wait failed; the VSync display queue is no longer limited; see the log";
                    }
                    return;
                }
                if(isImage){requested.enabled=false;requested.nvidiaLowLatency=NvidiaLowLatency::Off;}
                std::wstring message;
                presentationEffective=presenter.configurePresentation(ctx,requested,options.fg,message);
                presentationPaced=presenter.pacingActive()&&!legacyVsyncQueue;
                pacingRevision=revision;presenterGeneration=presenter.generation();pacingRefreshHz=refreshHz;cadence.reset();
                {std::lock_guard lock(mutex_);snapshot_.presentationEffective=presentationEffective;snapshot_.presentationRevision=revision;snapshot_.presentationStatus=message;snapshot_.presentationProviderOwned=presenter.providerOwnedPresentation();}
                veyra::log::info("pacing",std::format("revision={} requested={}/{}/{} effective={}/{}/{} displayQueuePaced={} variableRefresh={} vrrLevel={} vrrSync={}",revision,requested.enabled,unsigned(requested.mode),unsigned(requested.display),presentationEffective.enabled,unsigned(presentationEffective.mode),unsigned(presentationEffective.display),presentationPaced,requested.variableRefresh,gfx::vrrLevel(presenter.displayVrr()),presenter.vrrSync()));
            };
            auto drainLivePresentation=[&]{cadence.reset();if(liveScheduler){++presentationDrains;liveScheduler->cancel();pollCompletions();pendingCompletions.clear();}pairLatency.reset();deadlineMargin.reset();fgRejectionHold.reset();};
            const bool preciseDeadlines=GetEnvironmentVariableW(L"VEYRA_TEST_LEGACY_DEADLINE_WAIT",nullptr,0)==0;
            // A ready frame whose deadline has come goes out before the completion
            // poll: the poll collects GPU timings and FG budget samples, and a frame
            // due meanwhile waited for it (the P99 present interval is the 1% low).
            // The step checks its own fence; it does not need the poll.
            auto advanceLive=[&]{applyPresentation();if(liveScheduler){
                const auto halted=[&]{return stop_||(paused_&&!seekPreviewPending)||seekSeconds_>=0;};
                if(!halted()&&preciseDeadlines&&liveScheduler->preciseWake()){const auto now=host100ns(),due=liveScheduler->wakeAt();if(due>0&&due<=now)liveScheduler->advance(now);}
                pollCompletions();if(halted()){liveScheduler->cancel();cadence.reset();}else liveScheduler->advance(host100ns());}};
            FrameFlowWindow* waitingFlow=nullptr;uint64_t waitingPresented=0;size_t waitingPending=0;
            auto publishWaitingProgress=[&](bool force=false){
                if(!frameFlow)return;
                const auto presented=presentationCompletedReal.load();const auto pending=pendingCompletions.size();
                if(!force&&waitingFlow==frameFlow.get()&&waitingPresented==presented&&waitingPending==pending)return;
                waitingFlow=frameFlow.get();waitingPresented=presented;waitingPending=pending;
                // A source can stop delivering after submitting its last batch.
                // Publish actual completion/presentation progress without waiting
                // for another input. No GPU work, timer change or frame replay.
                const auto flow=frameFlow->snapshot(host100ns());
                std::lock_guard lock(mutex_);snapshot_.metrics.flow=flow;
            };
            const bool checkpointPresentation=GetEnvironmentVariableW(L"VEYRA_TEST_NO_PROCESS_PRESENT",nullptr,0)==0;
            const bool capturePresentAfterReady=false;
            bool processingGraph=false;
            auto waitLive=[&]{
                const auto due=liveScheduler?liveScheduler->wakeAt():0;
                if(preciseDeadlines&&liveScheduler&&liveScheduler->preciseWake()){
                    deadlineWait.until(due,nullptr,nullptr,[&]{return stop_||(paused_&&!seekPreviewPending)||seekSeconds_>=0;});
                }else deadlineWait.slice(due>host100ns()?std::min(1.0,double(due-host100ns())/10000):1.0,nullptr,nullptr);
            };
            // No full advanceLive here: it collects the graph's timing queries
            // and can apply presentation settings. Only older leased outputs
            // may run, on the presenter's independent ring; never XeSS/file.
            // Never wait here for a frame that is not due yet. Holds of up to 0.3 ms
            // (2026-11-13) sent real frames on time but started this batch's GPU work as
            // much later: learned, the pair phase and capture-to-present grew 0.3-0.4 ms;
            // left out of the learning, the generated frames lost their margin and the
            // result swung between runs (DLSS 2X 1% low 113-118). The single owner thread
            // cannot do both; a frame due here goes out at the next checkpoint.
            auto presentDuringProcess=[&]{
                if(!checkpointPresentation||!isCapture||!options.fg||graph->hdrOutput()||!presenter.independentQueue()||
                   !liveScheduler||stop_||(paused_&&!seekPreviewPending)||seekSeconds_>=0)return;
                const auto due=liveScheduler->wakeAt(),now=host100ns();
                if(due>0&&due<=now)liveScheduler->advance(now);
            };
            uint64_t metricsRevision=options.settings.revision,metricsEpoch=0,metricsWindowEpoch=0,statsSourceBase=0;
            uint64_t slotWaitBase=ring.cpuWaitCount(),submitBase=ring.submitCount();double slotWaitMsBase=ring.cpuWaitMilliseconds();
            std::shared_ptr<FrameCompletionRates> completionRates;
            source::CaptureMetrics captureFlowBase{},captureFlowLast{};
            uint64_t rateSkippedBase=0;
            // Queued steps capture these locals; cancel before their destruction.
            const bool injectSourceGap=options.captureReplayForTest&&GetEnvironmentVariableW(L"VEYRA_TEST_REPLAY_SOURCE_GAP",nullptr,0)>0;
            std::optional<Clock::time_point> sourceGapUntil;
            // Master media clock for file playback: the audio device clock when
            // mapped, a monotonic anchor without audio, and its frozen value
            // while an endpoint recovers. Steady-state video scheduling reads
            // this clock; it never waits on audio coverage.
            // Whether a network source is being held by buffering. nowMs() uses it to tell "the audio really played out" from
            // "the network simply has no data for a moment": the latter must not hand the media clock to the wall clock.
            std::atomic<bool> networkHolding{false};
            // The buffering phase flaps between buffering and playing several times a second; using it directly to pause audio
            // makes the sound break up. Only an uninterrupted half second of buffering really stops the sound; recovery applies immediately.
            // The buffering phase flaps between buffering and playing several times a
            // second; pausing the audio straight from it makes the sound stutter. Only a
            // stretch of half a second really stops it, while resuming applies at once.
            Clock::time_point audioHoldSince{};
            bool audioHoldPushed=false;
            auto nowMs=[&](){
                if(!audioStarted)return anchorMs+std::chrono::duration<double,std::milli>(Clock::now()-anchor).count()*playbackRate;
                publishAudioStatus();const double a=audio.mediaTimeMs();
                // Audio is waiting for a network retry: freeze the media clock and hold the picture until the sound is back. Without this
                // the picture runs ahead and the sound lands seconds behind once it catches up, out of sync with the mouths - field
                // 2026-10-25: leadMs reached -1081 and -7495.
                // The audio is between read attempts: freeze the media clock so the picture
                // waits for the sound. Otherwise the picture runs ahead and the sound comes
                // back seconds behind (field log 2026-10-25: leadMs -1081, -7495).
                if(audioPipe.retryingRead()){audioClockExhausted=false;return lastAudioClockMs;}
                if(std::isfinite(a)){lastAudioClockMs=a;audioClockExhausted=false;return a;}
                // A disconnected endpoint freezes the shared media clock. Only a
                // fully exhausted audio stream may hand its tail to the wall clock.
                if(audioPipe.clockExhausted()){
                    // But while the network is buffering the freeze must continue: the source's data is "not here yet", not
                    // "played out". If the wall clock advanced here, the media clock would run ahead of the picture;
                    // after buffering recovers the video waits a long time to catch up - the visible result is a picture that freezes and never recovers
                    // (field 2026-10-24: clockMs advanced 10 s in 8 s, coverage stalled in place).
                    if(networkHolding.load()){
                        audioClockExhausted=false;
                        return lastAudioClockMs;
                    }
                    if(!audioClockExhausted){audioClockExhausted=true;audioTailAnchor=Clock::now();}
                    return lastAudioClockMs+std::chrono::duration<double,std::milli>(Clock::now()-audioTailAnchor).count()*playbackRate;
                }
                return lastAudioClockMs;
            };
            // Network sources and files on a server: statistics for the signal panel, and the buffering /
            // reconnect explanation on the status line while playback is held. The previous status comes
            // back when the hold ends, unless something else replaced the text in the meantime.
            std::wstring networkSavedStatus,networkShownStatus;
            bool networkClockFrozen=false;double networkFrozenMs=0;
            auto nextNetworkPublish=Clock::now();
            auto publishNetwork=[&]{
                const auto stats=source.networkStats();
                std::lock_guard lock(mutex_);
                snapshot_.network=stats;
                const bool explain=!stats.message.empty()&&(stats.holding()||stats.state==media::NetworkStats::State::Reconnecting);
                if(explain){
                    if(networkShownStatus.empty())networkSavedStatus=snapshot_.status;
                    snapshot_.status=networkShownStatus=stats.message;
                }else if(!networkShownStatus.empty()){
                    if(snapshot_.status==networkShownStatus)snapshot_.status=networkSavedStatus;
                    networkShownStatus.clear();
                }
            };
            // Runs every loop turn and inside the presentation back-pressure wait: while playback is held
            // for buffering the frozen clock keeps queued pictures from presenting, and that wait must
            // neither look like a hang to the watchdog nor stop the statistics.
            // P0 buffering gate: the video side's watermark comes from the demux phase, and the audio side has no equivalent signal -
            // when a Bilibili-style source (audio track opened separately, session_ empty) drops data, the demux does not know,
            // so the picture runs on while the sound breaks. Here the two audio watermarks are used to cover it: the PCM buffer runs dry,
            // or the read-ahead queue runs dry. The thresholds carry hysteresis (300→1000, 500→1500), otherwise
            // the sound flaps several times a second around the threshold, breaking up.
            // P0 buffering gate: the video side reports through the demux phase, the audio
            // side has no such signal - for a separately opened track (Bilibili) the demux
            // knows nothing when the sound runs dry, so the picture kept going in silence.
            // Two audio water levels fill that gap, with hysteresis so it cannot flap.
            // P2: the thresholds are read from an environment variable, so they can be adjusted per network condition without recompiling.
            // P2: thresholds come from the environment so a field can be tuned without
            // rebuilding; anything unset or <= 0 falls back to the built-in value.
            auto gateEnvMs=[](const char* name,double fallback){
                const char* value=std::getenv(name);
                if(!value||!*value)return fallback;
                const double parsed=std::atof(value);
                return parsed>0.0?parsed:fallback;
            };
            engine::BufferingGateLimits gateLimits;
            gateLimits.enterPcmMs=gateEnvMs("VEYRA_GATE_ENTER_PCM_MS",gateLimits.enterPcmMs);
            gateLimits.enterQueuedMs=gateEnvMs("VEYRA_GATE_ENTER_QUEUED_MS",gateLimits.enterQueuedMs);
            gateLimits.exitPcmMs=gateEnvMs("VEYRA_GATE_EXIT_PCM_MS",gateLimits.exitPcmMs);
            gateLimits.exitQueuedMs=gateEnvMs("VEYRA_GATE_EXIT_QUEUED_MS",gateLimits.exitQueuedMs);
            gateLimits.warmupMs=static_cast<long long>(gateEnvMs("VEYRA_GATE_WARMUP_MS",static_cast<double>(gateLimits.warmupMs)));
            // The decision lives in BufferingGate, guarded by unit tests (tests/unit/BufferingGateTests.cpp):
            // threshold hysteresis, the 5 s window, the reconnect budget, giving up.
            // The decision lives in BufferingGate, covered by unit tests: the hysteresis,
            // the 5 s window, the reconnect budget and giving up.
            engine::BufferingGate bufferingGate(gateLimits);
            auto gateNow=[]{
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            };
            bool audioGateNotified=false,audioGateWasHolding=false;
            Clock::time_point nextGateLog{};
            auto networkTick=[&]{
                if(!demuxSession)return;
                const bool videoHolding=demuxSession->holding();
                long long gateNowMs=0;
                // A shared demux session already gates both tracks. Its audio has
                // no independent read-ahead queue, so queuedMs() is always zero.
                if(audioStarted&&separateAudio){
                    const double pcmMs=audioPipe.bufferedMs();
                    const double queuedMs=audioPipe.queuedMs();
                    // When the stream has already reached its end, the watermark naturally falls to 0 - that is not a break. Treating it as one would
                    // trigger a pointless reconnect (the address is already played out) and, on reconnect failure, mark the session failed,
                    // and the Bilibili layer would then "switch mirror and restart" - the visible result is a re-buffer after the end
                // and a replay of the last few seconds.
                    // A finished stream drains its queue: that is not dryness. Treating it as
                    // such fired a pointless reconnect, marked the session failed, and made the
                    // Bilibili layer reopen on another mirror - replaying the last few seconds.
                    const bool streamEnded = audioPipe.decodingComplete() || audioPipe.clockExhausted() ||
                                             (demuxSession && demuxSession->eof());
                    gateNowMs=gateNow();
                    const auto gateAction=bufferingGate.update(!streamEnded,pcmMs,queuedMs,gateNowMs);
                    const bool gateHolding=bufferingGate.holding();
                    if(gateHolding&&!audioGateWasHolding)
                        veyra::log::warn("buffering-gate",std::format(
                            "audio running dry: pcmMs={:.0f} queuedMs={:.0f}; holding the picture with the sound",
                            pcmMs,queuedMs));
                    else if(!gateHolding&&audioGateWasHolding)
                        veyra::log::info("buffering-gate",std::format(
                            "audio recovered: pcmMs={:.0f} queuedMs={:.0f}",pcmMs,queuedMs));
                    audioGateWasHolding=gateHolding;
                    // P1: still not back after 5 s is not "slow", it is a dead connection. Reopen once at the original address (the Bilibili audio
                    // address stays valid during playback), continuing from the current media moment. Deliberately the **audio** is the
                    // reference: the picture may step one frame for it, which is better than the mouths drifting.
                    // P1: five seconds means the connection is dead, not slow. Reopen the same
                    // audio address and continue from the current media time. The sound stays
                    // the reference on purpose - the picture takes one small step instead of
                    // drifting out of sync.
                    if(gateAction==engine::BufferingGate::Action::Reconnect){
                        const double resumeMs=lastAudioClockMs;
                        veyra::log::warn("buffering-gate",std::format(
                            "reopening the audio address (attempt {}) at {:.0f} ms",bufferingGate.reconnects(),resumeMs));
                        audioPipe.stopThread();
                        bool reopened=false;
                        try{
                            reopened=audioPipe.open(audioTarget,&stop_,options.network.requestHeaders,options.network.userAgent);
                        }catch(const std::exception& e){
                            veyra::log::error("buffering-gate",std::format("audio reopen threw: {}",e.what()));
                        }
                        if(reopened){
                            audioPipe.holdForVideo();audioPipe.startThread(&audio,true);
                            {
                                std::lock_guard lock(mutex_);
                                snapshot_.audioTracks=audioPipe.tracks();
                                snapshot_.selectedAudioTrack=audioPipe.selectedTrack();
                                snapshot_.audioAvailable=true;
                            }
                            if(std::isfinite(resumeMs)&&resumeMs>0.0)(void)audioPipe.requestSeek(resumeMs);
                            audioGateWasHolding=false;
                            watchdog_->beat();
                            veyra::log::info("buffering-gate",std::format(
                                "audio reopened; continuing at {:.0f} ms (the picture follows the sound)",resumeMs));
                        }else{
                            veyra::log::error("buffering-gate","audio address could not be reopened");
                            // The reconnect also failed: this session has no usable sound. Remove the audio from the clock and the gate,
                            // otherwise the watermark stays 0, the gate never clears, and the user sees a frozen picture on play.
                            // Reopening failed too: this session has no usable sound left. Take the
                            // audio out of the clock and the gate, or a zero water level holds the
                            // picture for ever and pressing play changes nothing.
                            audioStarted=false;
                            bufferingGate.abandon();
                            {
                                std::lock_guard lock(mutex_);
                                snapshot_.audioAvailable=false;   // also stop the UI from claiming there is sound
                            }
                        }
                    }
                }
                const bool holding=videoHolding||bufferingGate.holding();
                networkHolding.store(holding);
                // P2: write one line per second with both watermarks and the decision, so a buffering drop can be reconstructed from the log.
                // P2: one line a second with both water levels and the verdict, so a single
                // buffering episode can be reconstructed from the log alone.
                if(Clock::now()>=nextGateLog){
                    // One line per five seconds: enough to reconstruct a buffering drop, without blowing up the log
                    // (one per second would add seven thousand lines for a two-hour film).
                    // One line every five seconds: enough to reconstruct an episode,
                    // few enough not to flood a two-hour session.
                    nextGateLog=Clock::now()+std::chrono::seconds(5);
                    veyra::log::info("buffering-gate",std::format(
                        "pcmMs={:.0f} queuedMs={:.0f} videoHolding={} audioHolding={} holding={} reconnects={}",
                        audioStarted?audioPipe.bufferedMs():-1.0,
                        audioStarted?audioPipe.queuedMs():-1.0,
                        videoHolding,bufferingGate.holding(),holding,bufferingGate.reconnects()));
                }
                // Bilibili-style "audio track opened separately" sources: the audio pipeline cannot see the video session's phase,
                // so the buffering status must be pushed in from here - otherwise the picture stalls on buffering while the sound keeps playing.
                // For a source whose audio is opened separately (Bilibili) the pipeline
                // cannot see the video session's phase, so the buffering state is pushed
                // here - otherwise the picture stalls while the sound keeps going.
                // The sound stops after half a second of debounce (see the audioHoldSince note above).
                // Half a second of debounce before the sound is actually stopped.
                if(holding){
                    if(audioHoldSince==Clock::time_point{})audioHoldSince=Clock::now();
                    if(!audioHoldPushed&&Clock::now()-audioHoldSince>std::chrono::milliseconds(500)){
                        audioPipe.setNetworkHold(true);
                        audioHoldPushed=true;
                    }
                }else{
                    audioHoldSince=Clock::time_point{};
                    if(audioHoldPushed){audioPipe.setNetworkHold(false);audioHoldPushed=false;}
                }
                if(holding)watchdog_->beat();
                // Without an audio device the media clock is the wall clock: stand it still while the
                // session pauses playback for buffering (with audio, pausing the endpoint does that).
                if(!audioStarted){
                    if(holding){
                        if(!networkClockFrozen){networkClockFrozen=true;networkFrozenMs=nowMs();}
                        anchor=Clock::now();anchorMs=networkFrozenMs;
                    }else networkClockFrozen=false;
                }
                // Where playback is decides how far ahead the session reads and when it pauses for buffering.
                source.setPlaybackPositionMs(nowMs());
                if(Clock::now()>=nextNetworkPublish){nextNetworkPublish=Clock::now()+std::chrono::milliseconds(100);publishNetwork();}
            };
            uint64_t hdrDisplayCheck=0;
            bool xessPresentationRecovery=false;
            auto recoverXessPresentation=[&]{
                if(!presenter.xessFailed()||FAILED(ctx.device()->GetDeviceRemovedReason()))return false;
                auto reduced=options.snapshot();
                if(!disableFailedBackend(reduced,FailedBackend::Fg))return false;
                holdFileAudio();fileAudioAlignPending=true;
                drainLivePresentation();
                if(!ring.drainQueue()||!ring.discardRecording())return false;
                out={};hasOutput=false;
                // A failed scheduler stays failed after cancel. Recreate it on
                // its owner, then rebuild the SDK swapchain via settings below.
                if(liveScheduler)liveScheduler=std::make_unique<LiveGpuScheduler>();
                {
                    std::lock_guard lock(mutex_);
                    desired_.rejectVideoRequest(options.snapshot(),reduced);
                    desired_.revision=++nextRevision_;snapshot_.desired=desired_;snapshot_.applying=true;
                    snapshot_.backendWarning=L"XeSS presentation failed; returning to basic playback. Error code in the log";
                }
                veyra::log::warn("backend-recovery",std::format("{} presentation failed; drained commands, requesting swapchain rebuild without FG","XeSS"));
                xessPresentationRecovery=true;
                reset=true;pendingResetCause=pipeline::ResetReason::Settings;
                return true;
            };
            // Disarmed after stopPresentation's teardown, so a teardown that blocks is
            // reported too.
            OnExit leaveWatchdog{[&]{watchdog_->disarm();}};
            // Test hook: block the owner once, after 120 loop frames, for the given ms
            // (-1 = forever), to exercise the hang watchdog and bounded shutdown.
            wchar_t testHangText[16]{};
            int testHangMs=GetEnvironmentVariableW(L"VEYRA_TEST_ENGINE_HANG_MS",testHangText,16)?_wtoi(testHangText):0;
            // Test hook: VEYRA_TEST_GPU_FAILURE=<frames>:<removeAfterMs> fails live presentation
            // once, as a GPU frame-ready timeout does, and removes this device that many ms
            // later as the driver reset does (-1 = never), for the recovery paths.
            wchar_t testGpuFailureText[32]{};int testGpuFailureFrames=0,testGpuRemoveMs=-1;bool testGpuFailure=false;
            if(GetEnvironmentVariableW(L"VEYRA_TEST_GPU_FAILURE",testGpuFailureText,32)){
                testGpuFailureFrames=_wtoi(testGpuFailureText);
                if(const wchar_t* colon=wcschr(testGpuFailureText,L':'))testGpuRemoveMs=_wtoi(colon+1);
            }
            // Declared LAST before the loop on purpose: it is destroyed first at
            // scope exit, so the scheduler (whose queued step/finish lambdas
            // capture the locals above by reference) is torn down before any of
            // those locals (sweep 2026-09-22 B1). Do not move declarations that
            // the lambdas reference below this line.
            AutoNrController autoNrControl;autoNrControl.reset(host100ns());
            uint64_t autoNrRevision=options.settings.revision;int64_t autoNrLogAt=0;
            AutoNrReuseController nrReuseControl;nrReuseControl.reset(host100ns());
            uint64_t nrReuseRevision=options.settings.revision;int64_t nrReuseLogAt=0;
            // A simultaneous seek/speed change may discard the cached frame
            // after a successful rebuild. Keep its status acknowledgement
            // until the first successfully processed frame, across loop turns.
            bool pendingSettingsAck=false;
            OnExit stopPresentation{[&]{liveScheduler.reset();finishReset(stop_?diagnostics::ResetOutcome::Cancelled:diagnostics::ResetOutcome::Failed);std::lock_guard lock(mutex_);if(snapshot_.sessionId==runSessionId){if(frameFlow)snapshot_.metrics.flow=frameFlow->snapshot(host100ns());activeFlow_.reset();}}};
            while(!stop_){
                watchdog_->beat();
                if(testHangMs&&frames>=120){veyra::log::warn("engine-test",std::format("blocking owner thread for {} ms",testHangMs));Sleep(testHangMs<0?INFINITE:DWORD(testHangMs));testHangMs=0;}
                if(Clock::now()-cacheBudgetPolledAt>=std::chrono::milliseconds(500)){
                    (void)recentGraphCache.checkBudget();cacheBudgetPolledAt=Clock::now();
                }
                diagnostics::CpuStallTrace loopTrace("engine-stall",frames,45.0,watchdog_->stage());
                collectTimings();
                loopTrace.mark("collect");
                networkTick();
                // P0 buffering gate: once the audio has not come back in 5 s, stop waiting together - pause and tell the user explicitly to reopen,
                // rather than freeze the picture and the sound together with no hint at all.
                // P0: once the sound has been gone for five seconds, stop waiting together:
                // pause and say so, instead of freezing picture and sound in silence.
                if(bufferingGate.gaveUp()){
                    if(!audioGateNotified){
                        audioGateNotified=true;
                        status(L"Audio stalled for over 5 seconds. Playback paused; reopen the video", true);
                        pause(true);
                    }
                }else if(audioGateNotified)audioGateNotified=false;
                // Windows HDR flips with a newer display answer (a display or monitor
                // change, refresh the monitor state, a session start). Reading it calls no driver; the
                // owner used to enumerate DXGI outputs here every 2 s (0.25-0.64 ms each).
                if((gd.hdrInput||options.settings.videoHdr.enabled)&&GetTickCount64()-hdrDisplayCheck>250){
                    hdrDisplayCheck=GetTickCount64();
                    const HMONITOR target=displayStates_.monitorOf(window);
                    if(const auto known=displayStates_.state(target);known&&known->serial!=hdrDisplaySerial){
                        hdrDisplaySerial=known->serial;
                        const bool active=displayHdrState.update(reinterpret_cast<std::uintptr_t>(target),known->color.hdr);
                        const bool native=options.settings.useHdrPreview(gd.hdrInput,active);
                        if(native!=gd.hdrOutput){std::lock_guard lock(mutex_);if(desired_.revision==options.settings.revision){desired_.revision=++nextRevision_;snapshot_.desired=desired_;snapshot_.applying=true;}}
                    }
                }
                if(audioStarted)publishAudioStatus();
                if(audioStarted&&injectFileEndpointLoss&&!fileEndpointLossInjected&&frames>=20){audioPipe.requestEndpointLossForTest();fileEndpointLossInjected=true;}
                advanceLive();
                if(testGpuFailureFrames>0&&frames>=uint64_t(testGpuFailureFrames)&&liveScheduler){
                    testGpuFailureFrames=0;testGpuFailure=true;
                    veyra::log::warn("engine-test",std::format("simulating a GPU frame-ready timeout; device removal after {} ms",testGpuRemoveMs));
                    if(testGpuRemoveMs>=0){
                        Microsoft::WRL::ComPtr<ID3D12Device5> device;
                        if(SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&device))))
                            std::thread([device,delay=testGpuRemoveMs]{Sleep(DWORD(delay));device->RemoveDevice();veyra::log::warn("engine-test","device removed");}).detach();
                    }
                }
                if((liveScheduler&&liveScheduler->failed())||testGpuFailure){
                    if(!testGpuFailure&&recoverXessPresentation())continue;
                    testGpuFailure=false;
                    bool alive=ctx.reportDeviceFailure("live-presentation-failed");
                    // A GPU hang is noticed here about two seconds before Windows resets the
                    // GPU, and DRED only says which command list stopped once the device is
                    // removed (4070S field log 2026-10-01: every report still read alive).
                    // Wait for that, bounded, and report again.
                    for(int i=0;alive&&i<50;++i){Sleep(100);uint32_t removed=0;alive=ctx.checkDeviceAlive(removed);}
                    if(!alive)ctx.reportDeviceFailure("live-presentation-failed-after-reset");
                    gpuFailureAwaitingRemoval=alive;
                    {std::lock_guard lock(mutex_);snapshot_.gpuFailed=true;}
                    status(alive?L"Video queue or presentation failed; the GPU did not report a device removal. See diagnostics":L"The GPU was removed or the driver was reset; reconnect in a moment, or switch the NR runtime and retry",true);break;
                }
                const float gain=muted_?0.0f:volume_.load();audio.setGain(gain);
#ifdef VEYRA_ENABLE_MOONLIGHT
                if(moon){moon->setAudioGain(gain);moon->setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                    const auto state=moon->audioState();
                    std::lock_guard lock(mutex_);snapshot_.audioAvailable=state.available;snapshot_.captureAudio=state;}
#endif
#ifdef VEYRA_ENABLE_XBOX
                if(xb){xb->setAudioGain(gain);xb->setAudioSync(unsigned(options.settings.audioSync),options.settings.audioOffsetMs);
                    const auto state=xb->audioState();
                    std::lock_guard lock(mutex_);snapshot_.audioAvailable=state.available;snapshot_.captureAudio=state;}
#endif
                // Save the latest processed real frame before a settings transaction
                // invalidates it (live rendering can be one batch behind processing).
                std::wstring save;EnhancementSettings requested;std::optional<ChainRuntimeOrder> requestedOrder;
                {std::lock_guard lock(mutex_);requested=desired_;requestedOrder=desiredNodeOrder_;if(hasOutput)save.swap(savePath_);}
                if(!save.empty()){try{sink::RgbaImage result;const auto e=std::filesystem::path(save).extension().wstring();
                    const auto saved=[this](const std::wstring& path,const std::wstring& message){
                        {std::lock_guard lock(mutex_);snapshot_.savedFramePath=path;}
                        status(message);
                    };
                    drainLivePresentation();
                    if(graph->hdrOutput()){auto hdrPath=std::filesystem::path(save);hdrPath.replace_extension(L".jxr");if(sink::saveHdrScreenshot(hdrPath.wstring(),ctx,ring,graph->videoFrameResource(out.videoSlot)))saved(hdrPath.wstring(),L"HDR screenshot saved: "+hdrPath.wstring());else status(L"Could not save the HDR screenshot; see the log",false);}else if(!sink::readRgba8(ctx,ring,graph->videoFrameResource(out.videoSlot),result)||!sink::saveImage(save,result,e==L".jpg"||e==L".jpeg",isImage?sink::ImageAlpha::Preserve:sink::ImageAlpha::Opaque))status(L"Save failed (the file may already exist); playback kept",false);else{saved(save,L"Image saved: "+save);veyra::log::info("image-save",std::format("saved extent={}x{} revision={}",gd.workWidth,gd.workHeight,options.settings.revision));}}
                    catch(const std::exception& e){veyra::log::warn("image-save",std::format("save exception; retaining session: {}",e.what()));status(L"Save error; playback kept and can be saved again",false);}}
                if(isImage)requested.multiplier=1;
                if(requested.revision==options.settings.revision&&requested!=options.settings){
                    options.settings.audioSync=requested.audioSync;options.settings.audioOffsetMs=requested.audioOffsetMs;
                    std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;
                    veyra::log::info("settings",std::format("Audio applied videoRevision={} mode={} offsetMs={} (video history retained)",requested.revision,unsigned(requested.audioSync),requested.audioOffsetMs));
                }
                const auto previous=options.snapshot();const auto previousDesc=gd;const auto previousOrder=options.nodeOrder;bool transaction=false;
                if(requested.revision!=previous.revision){
                    holdFileAudio();
                    finishReset(diagnostics::ResetOutcome::Cancelled);
                    resetStart=resetStageStart=Clock::now();
                    resetRecord=diagnostics::FrameFlowMetrics::ResetRecord{};
                    resetRecord->sessionId=runSessionId;resetRecord->settingsRevision=requested.revision;
                    resetRecord->reason=static_cast<uint8_t>(pipeline::ResetReason::Settings);
                    if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
                    drainLivePresentation();
                    // Rebuilding the video graph must not tear down a healthy
                    // stream audio endpoint; only the new video mapping is
                    // invalidated here. Audio transport reset is reserved for
                    // an actual pause/stop or input discontinuity.
#ifdef VEYRA_ENABLE_MOONLIGHT
                    if(moon)moon->videoReset(false);
#endif
#ifdef VEYRA_ENABLE_XBOX
                    if(xb)xb->videoReset(false);
#endif

                    // A drain may outlive several slider notifications. Build
                    // only the latest pending configuration at this boundary.
                    {std::lock_guard lock(mutex_);requested=desired_;requestedOrder=desiredNodeOrder_;}
                    if(requested.revision==previous.revision){finishReset(diagnostics::ResetOutcome::Cancelled);continue;}
                    resetRecord->settingsRevision=requested.revision;
                    auto next=PlayerOptions::from(requested,requestedOrder,&options);auto nextDesc=gd;
                    const bool nvidiaAdapter=ctx.adapter().isNvidia;
                    describeStages(stageRequest(next),next.snapshot(),nextDesc);
                    nextDesc.hdrOutput=requested.useHdrPreview(nextDesc.hdrInput,displayHdrActive());
                    // Rebuild decision.
                    //  - requiresGraphRebuild(): everything derivable from the
                    //    settings (stages, order, sizes inputs, master switches,
                    //    LUT reference). Verified against the legacy
                    //    field-by-field expression over six settings smoke cases
                    //    (master, settings, fg-only, view, output-cap, rollback)
                    //    with zero disagreements before the legacy list was
                    //    removed.
                    //  - the rest cannot come from the settings alone: the
                    //    effective HDR output depends on the display, the
                    //    extents come from the resolution plan, and a non-NVIDIA
                    //    adapter resolves requested effects differently than the
                    //    settings record.
                    bool rebuild=engine::requiresGraphRebuild(previous,requested)||previousOrder!=requestedOrder;
                    if(gd.hdrOutput!=nextDesc.hdrOutput||
                       (!nvidiaAdapter&&nvidiaOnlyStagesRequested(next.nr,next.sr,next.fg,next.settings,amdNrAdapter))||
                       gd.enableNr!=nextDesc.enableNr||gd.enableFg!=nextDesc.enableFg||
                       gd.nrBeforeSr!=nextDesc.nrBeforeSr||
                       gd.workWidth!=nextDesc.workWidth||gd.workHeight!=nextDesc.workHeight||
                       gd.nrWidth!=nextDesc.nrWidth||gd.nrHeight!=nextDesc.nrHeight||
                       gd.flowWidth!=nextDesc.flowWidth||gd.flowHeight!=nextDesc.flowHeight||
                       gd.dualSr!=nextDesc.dualSr||gd.dualSrQuality!=nextDesc.dualSrQuality||gd.firstSrWidth!=nextDesc.firstSrWidth||gd.firstSrHeight!=nextDesc.firstSrHeight||
                       gd.srInputWidth!=nextDesc.srInputWidth||gd.srInputHeight!=nextDesc.srInputHeight||
                       gd.srOutputWidth!=nextDesc.srOutputWidth||gd.srOutputHeight!=nextDesc.srOutputHeight)rebuild=true;
                    bool accepted=ring.drainQueue();out={};hasOutput=false;
                    resetRecord->rebuilt=rebuild;
                    markResetStage(diagnostics::ResetStage::Drain);
                    if(stop_)break;
                    if(!accepted){finishReset(diagnostics::ResetOutcome::Failed);status(L"Could not drain while switching settings; stopped",true);break;}
                    backendRecoveryWarning.clear();
                    const bool preserveWorkingFg=previous.multiplier>1&&requested.multiplier>1&&
                        (previous.frameGenerationBackend!=requested.frameGenerationBackend||previous.multiplier!=requested.multiplier||previous.vfgQuality!=requested.vfgQuality||previous.vfgAutoRegulate!=requested.vfgAutoRegulate);
                    const bool preserveWorkingNr=previous.nr&&requested.nr&&previous.nrRuntime!=requested.nrRuntime;
                    if(accepted&&rebuild){
                        if(!presenter.prepareGraphChange()){finishReset(diagnostics::ResetOutcome::Failed);status(L"Could not drain presentation for the settings change. Playback stopped.",true);break;}
                        bool reused=false;
                        const bool mayRetain=recentCacheAllowed&&!isCapture&&!isImage&&
                            RecentGraphCache::eligible(gd)&&RecentGraphCache::effectsOff(nextDesc);
                        if(mayRetain&&recentGraphCache.retain(graph,RecentGraphCache::key(previous,previousOrder,gd),graphBytes)){
                            graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,nullptr);
                            graphBytes=0;
                        }else{
                            graph->shutdown();
                            const auto cachedBytes=recentGraphCache.bytes();
                            auto cached=RecentGraphCache::effectsOff(nextDesc)?std::unique_ptr<pipeline::EnhanceGraph>{}:
                                recentGraphCache.take(RecentGraphCache::key(requested,requestedOrder,nextDesc));
                            if(cached){graph=std::move(cached);graphBytes=cachedBytes;reused=true;}
                            else{
                                if(!RecentGraphCache::effectsOff(nextDesc))recentGraphCache.evict("new-enhancement-key");
                                graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,recentGraphCache.hasCachedGraph()?nullptr:&coreCache);
                            }
                        }
                        markResetStage(diagnostics::ResetStage::Destroy);
                        accepted=reused?(graph->applySettings(requested)&&openPreparedPresenter(next)):
                            initializePreview(nextDesc,next,preserveWorkingFg,preserveWorkingNr);
                        markResetStage(diagnostics::ResetStage::Create);
                        if(stop_){veyra::log::info("engine","settings rebuild cancelled; skipping rollback on exit");break;}
                        if(!accepted){
                            if(!presenter.prepareGraphChange()){status(L"Could not drain presentation to restore settings. Playback stopped.",true);break;}
                            graph->shutdown();recentGraphCache.evict("build-rollback");
                            graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,&coreCache);graphBytes=0;
                            if(!gpu->configureQueue(gd,!isCapture&&!isImage)||!graph->initialize(gd)||!openPreparedPresenter(options)){if(!stop_)status(L"Settings failed and the old resources could not be restored; stopped",true);break;}
                        }
                    }else if(accepted)accepted=graph->applySettings(requested);
                    if(rebuild)publishFrameGenerationCapabilities();
                    if(accepted){
                        if(xessPresentationRecovery){backendRecoveryWarning=L"XeSS presentation failed; frame generation turned off and playback resumed. Error code in the log";xessPresentationRecovery=false;}
                        if(!backendRecoveryWarning.empty()){
                            std::lock_guard lock(mutex_);desired_.rejectVideoRequest(requested,next.snapshot());snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.backendWarning=backendRecoveryWarning;
                        }
                        options=next;gd=nextDesc;transaction=true;pendingSettingsAck=true;reset=true;
                        resetRecord->epoch=0;
                        if(!nvidiaAdapter&&nvidiaOnlyStagesRequested(next.nr,next.sr,next.fg,next.settings,amdNrAdapter))veyra::log::warn("capability",std::format("non-NVIDIA adapter disabled requested settings revision={} nr={} sr={} fgBackend={} flowBackend={}",requested.revision,next.nr,next.sr,frameGenerationBackendName(requested.frameGenerationBackend),opticalFlowBackendName(requested.opticalFlowBackend)));
                    }
                    else {
                        finishReset(diagnostics::ResetOutcome::RolledBack);
                        std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desiredNodeOrder_=previousOrder;desired_.rejectVideoRequest(requested,previous);snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_!=previous;snapshot_.status=L"Settings failed to apply; previous settings restored";snapshot_.backendWarning=requested.nrRuntime!=previous.nrRuntime?L"Could not switch the NR runtime; previous settings restored":requested.frameGenerationBackend==FrameGenerationBackend::XeSS?L"XeSS could not be enabled; previous settings restored":requested.frameGenerationBackend==FrameGenerationBackend::AmdFrc?L"AMD FRC could not start (needs an AMD GPU and the AMF runtime from its driver); the previous settings were restored":
                            requested.frameGenerationBackend==FrameGenerationBackend::Fsr4?L"FSR 4 ML could not be enabled (needs a supported RX 9000 and driver); previous settings restored":fsrFrameGeneration(requested.frameGenerationBackend)?L"FSR 3.1 frame generation could not be enabled; previous settings restored":L"Could not switch the backend; previous settings restored";}
                }
                // Live uniform update. The revision identifies the graph *shape*
                // (NR/SR/FG/resolution and the colour master switch); colour
                // parameters keep the same revision on purpose, so they must be
                // pushed into the running graph here - no rebuild, no history
                // reset. Without this the panel only took effect after an
                // unrelated rebuild (toggling NR): the "sliders do nothing" report.
                if(requested.revision==previous.revision&&!(requested==previous)){
                    const bool videoChanged=!requested.sameActiveVideoParameters(previous);
                    // applySettings updates descriptors and resets NR history.
                    // A stored-only edit must not call it, even while paused.
                    if(!videoChanged||graph->applySettings(requested)){
                        options=PlayerOptions::from(requested,requestedOrder,&options);
                        {std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;}
                        veyra::log::info("settings",std::format("{} revision={} colourEnabled={} exposure={:.2f} contrast={:.2f} temperature={:.2f}",videoChanged?"live parameter update":"stored inactive/non-video parameters; GPU untouched",requested.revision,requested.color.enabled?1:0,requested.color.exposure,requested.color.contrast,requested.color.temperature));
                        // Colour lives inside the ingest dispatch, so a paused
                        // frame has no process() call to upload the new tables or
                        // re-run the chain: remember that the cached frame must
                        // be re-rendered before the next present.
                        if(videoChanged&&(paused_||isImage))refreshPausedFrame_=true;
                    }else{
                        std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desiredNodeOrder_=previousOrder;desired_.rejectVideoRequest(requested,previous);snapshot_.desired=desired_;snapshot_.applying=desired_!=previous;snapshot_.status=L"These settings need a pipeline rebuild; went back to the previous values";
                    }
                }
                if(stop_)break;
                int track=-1;double trackPosition=0;
                {std::lock_guard lock(mutex_);if(pendingAudioSession_==snapshot_.sessionId)track=pendingAudioTrack_;pendingAudioTrack_=-1;trackPosition=snapshot_.position;}
                bool switchedAudio=false;
                if(track>=0&&audioStarted&&track!=audioPipe.selectedTrack()){
                    const double clock=audio.mediaTimeMs();if(std::isfinite(clock))trackPosition=clock/1000;
                    holdFileAudio();audioPipe.stopThread();
                    const bool changed=audioPipe.selectTrack(track);
                    audioPipe.setPaused(paused_);audioPipe.startThread(&audio,true,std::max(0.0,trackPosition*1000));
                    switchedAudio=true;
                    {std::lock_guard lock(mutex_);snapshot_.selectedAudioTrack=audioPipe.selectedTrack();snapshot_.audioInputChannels=audioPipe.pcmFormat().channels;
                        if(!changed)snapshot_.backendWarning=L"Could not switch the audio track; the previous one is kept";
                        if(seekSeconds_.load()<0){snapshot_.seekTarget=trackPosition;++snapshot_.seekRequested;seekSeconds_=trackPosition;}}
                    log::info("audio-track",std::format("requested={} selected={} changed={} positionSeconds={:.3f}",track,audioPipe.selectedTrack(),changed,trackPosition));
                    bufferingGate.rearm(gateNow());   // after a track switch the buffer empties and refills; do not treat that as a break
                }
                if(!isImage&&!isCapture&&playbackRate!=requestedPlaybackRate_.load()){
                    playbackRate=requestedPlaybackRate_.load();audioPipe.setPlaybackRate(playbackRate);
                    playbackSpeedLastWall={};cadence.reset();
                }
                double seek;{std::lock_guard lock(mutex_);seek=seekSeconds_.exchange(-1);if(seek>=0)activeSeekId=snapshot_.seekRequested;}
                if (const auto session=source.sharedSession();session&&session->timeshiftEnabled()) {
                    const bool evicted=!paused_&&session->takeTimeshiftOverrun();
                    if (seek>=0||evicted) {
                        const auto window=session->stats();
                        if (window.timeshiftEndSeconds>window.timeshiftStartSeconds) {
                            const double edge=std::max(window.timeshiftStartSeconds,
                                window.timeshiftEndSeconds-std::max(0.25,session->plan().startSeconds));
                            seek=evicted&&seek<0?window.timeshiftStartSeconds:std::clamp(seek,window.timeshiftStartSeconds,edge);
                        } else seek=-1;
                    }
                }
                if(seek>=0&&!isImage&&!isCapture){
                    seekStarted=Clock::now();seekDecoded=0;
                    holdFileAudio();fileAudioAlignPending=true;
                    drainLivePresentation();
                    if(!ring.drainQueue()){status(L"Seek failed",true);break;}
                    if(!activeSource->seek({static_cast<int64_t>(seek*1000000),1000000})){
                        const auto session=source.sharedSession();
                        if(!session||!session->timeshiftEnabled()){status(L"Seek failed",true);break;}
                        // The live window can lose its last keyframe at the memory cap.
                        // Keep the receiver alive and let the next keyframe recover it.
                        audioRebuffering=false;
                        continue;
                    }
                    seekDemuxDone=Clock::now();
                    veyra::log::info("seek-latency",std::format("request={} targetSeconds={:.3f} drainAndDemuxMs={:.3f}",activeSeekId,seek,elapsedMs(seekStarted)));
                    initialFileFramePending=false;av_frame_free(&cachedFrame);out={};hasOutput=false;
                    discardBefore=seek*1000;seekPreviewPending=true;reset=true;pendingResetCause=pipeline::ResetReason::Seek;anchorMs=discardBefore;anchor=Clock::now();lastAudioClockMs=discardBefore;audioClockExhausted=false;audioRebuffering=false;if(audioStarted){audioPipe.setPaused(paused_);if(!switchedAudio||std::abs(seek-trackPosition)>.001)audioPipe.requestSeek(discardBefore);}
                    bufferingGate.rearm(gateNow());   // after a seek the same: empty first, then fill
                }
                if(!transaction&&((paused_&&!seekPreviewPending)||(isImage&&hasOutput))){
                    // A parameter change while paused (or on a still image) only
                    // reaches the picture if the cached source frame is pushed
                    // through the graph again: the grade is fused into the ingest
                    // dispatch, so nothing else would upload the new tables.
                    // NR inference reuse is a realtime-preview strategy only: the
                    // paused picture must be a fully inferred frame, so a reused
                    // last frame is re-rendered once when the pause begins.
                    if(!wasPaused&&graph->nrReuseActive()){
                        const bool reusedLast=graph->nrReuseLastFrameReused();
                        if(reusedLast)refreshPausedFrame_=true;
                        veyra::log::info("nr-reuse",std::format("pause began lastFrameReused={}{}",reusedLast,reusedLast?"; re-rendering it with full inference":""));
                    }
                    if(refreshPausedFrame_){
                        refreshPausedFrame_=false;
                        if(cachedFrame){
                            pipeline::EnhanceGraph::FrameOutputs refreshed;
                            const double refreshPtsMs=cachedPacket.pts.toDouble()*1000.0;
                            if(graph->process(cachedFrame,refreshPtsMs,true,refreshed,cachedPacket.sequence,&cachedPacket.colorInfo,&cachedPacket.hardwareSurface,false,{},0,true,{},cachedPacket.doviEl,&cachedPacket.doviP7)){
                                out=refreshed;
                                hasOutput=true;
                                // Custom: the still frame is re-presented below, so
                                // keep its Dolby Vision level 1 alongside it.
                                presenter.setFrameDovi(cachedPacket.dovi);
                                {std::lock_guard lock(mutex_);++snapshot_.pausedFrameRefreshes;}
                                veyra::log::info("settings",std::format("paused frame re-rendered with the new parameters ptsMs={:.3f}",refreshPtsMs));
                            }else{
                                veyra::log::warn("settings","paused frame re-render failed; keeping the previous output");
                            }
                        }
                    }
                    drainLivePresentation();
                    if(!wasPaused){holdFileAudio();
#ifdef VEYRA_ENABLE_MOONLIGHT
                    if(moon)moon->videoReset();
#endif
#ifdef VEYRA_ENABLE_XBOX
                    if(xb)xb->videoReset();
#endif
}
                    presenter.cancelReflexInput();
                    if(audioStarted)audioPipe.setPaused(true);wasPaused=true;
                    const bool referencesValid=hasOutput&&out.batch.count&&out.batch.frames[out.batch.count-1].lease&&out.batch.frames[out.batch.count-1].lease->referencesValid;
                    if(hasOutput&&!presenter.present(ctx,ring,*graph,out.videoSlot,false,referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,out.batch.identity,previewView(),-1,true)){if(recoverXessPresentation())continue;status(L"Picture presentation failed",true);break;}
                    if(hasOutput&&graph->resolveGeneration(out))completeReset(out.batch.identity);
                    // Poll live view/size changes at 50 ms; unchanged flip
                    // buffers are retained without commands or another Present.
                    std::this_thread::sleep_for(std::chrono::milliseconds(refreshPausedFrame_?1:50));continue;
                }
                if(wasPaused&&!paused_){holdFileAudio();audioRebuffering=false;if(audioStarted)audioPipe.setPaused(false);anchor=Clock::now();anchorMs=out.ptsMs;reset=true;pendingResetCause=pipeline::ResetReason::PauseResume;wasPaused=false;}
                // Backpressure before reading the capacity-one source mailbox:
                // when a lease frees we consume the newest available sample.
                if(liveScheduler&&!transaction){
                    const auto enhancementPending=[&]{
                        const bool fgRunning=options.fg;
                        if(presentationEffective.enabled&&!fgRunning&&(isCapture||!fileAwaitingVideo)&&liveScheduler->occupancy()>=1)return true;
                        // Explicit VSync, live, no generation: throttle only before the
                        // read. A frame read while the display queue is full would wait
                        // there a refresh; waiting first lets the mailbox hand over a
                        // newer frame instead (latency-min B3).
                        if(isCapture&&presentationPaced&&!fgRunning&&(liveScheduler->occupancy()>=1||!presenter.presentationReady()))return true;
                        // Live sources: never stack a second frame behind one the GPU
                        // is still enhancing. When NR/SR cost more than one source
                        // interval, a second queued frame doubled capture-to-present
                        // latency (3 NR layers at 1440p60: 49 ms for 20 ms of GPU
                        // work); waiting and then taking the newest mailbox sample
                        // keeps it at one GPU pass. No effect while the GPU keeps up.
                        return isCapture&&!(fgRunning&&presentSinkFrameGeneration(options.settings.frameGenerationBackend)&&(xessStackedReads||xessPresentAfterReady))&&!pendingCompletions.empty();
                    };
                    const auto leaseWaitStart=Clock::now();
                    while((liveScheduler->occupancy()>=2||!graph->nextFrameSlotAvailable()||enhancementPending())&&!stop_&&(!paused_||seekPreviewPending)&&!liveScheduler->failed()){
                        networkTick();
                        advanceLive();if(seekSeconds_>=0)break;
                        if(liveScheduler->occupancy()==0&&!graph->nextFrameSlotAvailable()&&elapsedMs(leaseWaitStart)>2000){status(L"Timed out waiting for the output texture to be released",true);stop_=true;break;}
                        if(liveScheduler->occupancy()>=2||!graph->nextFrameSlotAvailable()||enhancementPending()){
                            // Only the display queue is outstanding: wake on its latency object
                            // (acquiring it) instead of a 1 ms poll.
                            if(isCapture&&presentationPaced&&liveScheduler->occupancy()==0&&pendingCompletions.empty()&&graph->nextFrameSlotAvailable())presenter.waitPresentationReady(1);
                            else waitLive();
                        }
                        std::lock_guard lock(mutex_);if(desired_.revision!=options.settings.revision)break;
                    }
                    if(stop_||seekSeconds_>=0||(paused_&&!seekPreviewPending)||liveScheduler->failed()||liveScheduler->occupancy()>=2||!graph->nextFrameSlotAvailable()||enhancementPending())continue;
                }
                if(graph->autoNrPoolReady()&&frameFlow&&!transaction&&!paused_){
                    const auto now=host100ns();
                    if(autoNrRevision!=options.settings.revision||autoNrControl.level()!=graph->autoNrLevel()){
                        autoNrControl.reset(now,graph->autoNrLevel());autoNrRevision=options.settings.revision;
                    }
                    if(reset)autoNrControl.discardObservations(now);
                    const auto timing=frameFlow->snapshot(now).enhancementProcessing;
                    const double transportFps=activeSource->info().averageFps*playbackRate;
                    const double budget=std::isfinite(transportFps)&&transportFps>0?800.0/transportFps:0;
                    const auto next=autoNrControl.observe(now,timing.p95,timing.samples,budget);
                    if(now>=autoNrLogAt){
                        veyra::log::info("nr-auto",std::format("level={} budgetMs={:.3f} p95Ms={:.3f} samples={} reason=observe",
                            AutoNrController::percent(graph->autoNrLevel()),budget,timing.p95.value_or(0),timing.samples));
                        autoNrLogAt=now+10000000;
                    }
                    if(next){
                        const auto started=Clock::now();
                        // Finish submitted pictures in order. Cancel would drop
                        // real frames and conceal the switch gap.
                        while(liveScheduler&&liveScheduler->occupancy()&&!stop_&&!paused_&&seekSeconds_<0&&!liveScheduler->failed()){
                            advanceLive();if(liveScheduler->occupancy())waitLive();
                        }
                        if(stop_||paused_||seekSeconds_>=0)continue;
                        if((liveScheduler&&liveScheduler->failed())||!ring.drainQueue()){
                            status(L"Failed to drain pending work for an Auto NR switch",true);break;
                        }
                        pollCompletions();pendingCompletions.clear();out={};hasOutput=false;
                        if(!graph->selectAutoNrLevel(*next)){
                            veyra::log::error("nr-auto","normal playback switch refused after draining presentation leases");
                            status(L"Auto NR level switch failed",true);break;
                        }
                        autoNrControl.discardObservations(host100ns());
                        veyra::log::info("nr-auto",std::format("level={} budgetMs={:.3f} p95Ms={:.3f} reason={} boundaryMs={:.3f} cancelledJobs={}",
                            AutoNrController::percent(*next),budget,timing.p95.value_or(0),timing.p95.value_or(0)>budget?"over-budget":"sustained-headroom",
                            elapsedMs(started),presentationCancelledJobs.load()));
                    }
                }
                // NR inference reuse, Auto: a level change is a scheduling choice
                // inside the graph, so nothing drains or rebuilds (AutoNrReuseController).
                if(graph->nrReuseAutomatic()&&frameFlow&&!transaction&&!paused_){
                    const auto now=host100ns();
                    if(nrReuseRevision!=options.settings.revision||nrReuseControl.level()!=graph->nrReuseLevel()){
                        nrReuseControl.reset(now,graph->nrReuseLevel());nrReuseRevision=options.settings.revision;
                    }
                    if(reset)nrReuseControl.discardObservations(now);
                    const auto flow=frameFlow->snapshot(now);
                    std::optional<double> nrMs;
                    for(auto stage:{diagnostics::GpuStage::NrLayer0,diagnostics::GpuStage::NrLayer1,diagnostics::GpuStage::NrLayer2,diagnostics::GpuStage::NrLayer3}){
                        const auto& a=flow.gpuTiming[size_t(stage)];
                        // Reused frames add zero-length samples, so the mean is per
                        // source frame; the P95 is the cost of one inference.
                        if(a.samples&&a.p95&&std::isfinite(*a.p95))nrMs=nrMs.value_or(0)+*a.p95;
                    }
                    const double transportFps=activeSource->info().averageFps*playbackRate;
                    const double budget=std::isfinite(transportFps)&&transportFps>0?800.0/transportFps:0;
                    const auto& timing=flow.enhancementProcessing;
                    if(const auto next=nrReuseControl.observe(now,timing.mean,timing.samples,nrMs,budget)){
                        graph->setNrReuseLevel(*next);
                        veyra::log::info("nr-reuse",std::format("auto level={} framesPerReuse={} meanMs={:.3f} nrMs={:.3f} budgetMs={:.3f} samples={}",
                            *next,pipeline::NrReuseScheduler::framesPerReuse(*next),timing.mean.value_or(0),nrMs.value_or(0),budget,timing.samples));
                    }else if(now>=nrReuseLogAt){
                        veyra::log::info("nr-reuse",std::format("auto observe level={} meanMs={:.3f} nrMs={:.3f} budgetMs={:.3f} samples={} reused={}",
                            graph->nrReuseLevel(),timing.mean.value_or(0),nrMs.value_or(0),budget,timing.samples,graph->metrics().nrReuseFrames));
                        nrReuseLogAt=now+100000000;
                    }
                }
                // Wait before selecting the latest sample, not after enhancement.
                // A driver Sleep beyond this (once per accepted live source; the GPU
                // already finished the last one here) is a stall, see ReflexSession.
                const auto reflexStallUs=isCapture?uint64_t(std::max(6000.0,0.6e6/std::clamp(activeSource->info().averageFps,10.0,500.0))):0;
                if(!presenter.beginSourceInput(reflexStallUs)){status(L"XeSS input timing failed",true);break;}
                const auto decodeStart=Clock::now();
                loopTrace.mark("controlAndSchedule");
                if(injectSourceGap&&frames>=12){
                    if(!sourceGapUntil){sourceGapUntil=Clock::now()+std::chrono::milliseconds(400);veyra::log::info("capture-test","inject 400ms Waiting before next source read");}
                    if(Clock::now()<*sourceGapUntil){advanceLive();publishWaitingProgress();waitLive();continue;}
                }
                pipeline::FramePacket pkt;const AVFrame* frame=imageFrame;
                if(cachedFrame&&((initialRemoteFramePending||initialFileFramePending)||(transaction&&(!isCapture||paused_)))){frame=cachedFrame;pkt=cachedPacket;initialRemoteFramePending=false;initialFileFramePending=false;}
                else if(!isImage){
                    auto rs=activeSource->read(pkt,&frame);
                    // Keep the accepted transaction and rollback state alive until
                    // the next real sample arrives. Never bind old PTS to now.
                    while(transaction&&isCapture&&rs==source::SourceReadStatus::Waiting&&!stop_){
                        if(paused_&&cachedFrame){frame=cachedFrame;pkt=cachedPacket;rs=source::SourceReadStatus::Frame;break;}
                        advanceLive();publishWaitingProgress();waitLive();rs=activeSource->read(pkt,&frame);
                    }
                    if(stop_)break;
                    if(rs==source::SourceReadStatus::Waiting){advanceLive();publishWaitingProgress();waitLive();continue;}
                    if(rs==source::SourceReadStatus::Eos){
                        fileInputEnded=true;
                        while(liveScheduler&&liveScheduler->occupancy()&&!stop_&&(!paused_||seekPreviewPending)&&!liveScheduler->failed()){advanceLive();if(liveScheduler->occupancy())waitLive();}
                        if(stop_)break;
                        if(liveScheduler&&liveScheduler->failed()){status(L"Could not present the last video frame; see diagnostics",true);break;}
                        if(paused_)continue;
                        collectTimings();seekPreviewPending=false;status(L"The video has finished playing");paused_=true;
                        publishWaitingProgress(true);{std::lock_guard lock(mutex_);snapshot_.transport=TransportState::Ended;}
                        continue;
                    }
                    if(rs!=source::SourceReadStatus::Frame||pkt.pts.isUnknown()){
#ifdef VEYRA_ENABLE_MOONLIGHT
                        if(moon){const auto message=moon->stats().message;status(message.empty()?L"Stream interrupted; check the host and network and reconnect.":message,true);break;}
#endif
#ifdef VEYRA_ENABLE_XBOX
                        if(xb){
                            const auto failure=xb->stats();
                            if(failure.reconnectable && xboxRequest && !stop_){
                                xboxReconnectBudget.disconnected();
                                drainLivePresentation();
                                if(!ring.drainQueue()){status(L"Could not drain the GPU queue during Xbox reconnection.",true);break;}
                                xb->videoReset();av_frame_free(&cachedFrame);out={};hasOutput=false;
                                bool recovered=false;
                                while(!stop_ && xboxReconnectBudget.take()){
                                    const auto attempt=xboxReconnectBudget.attempts();
                                    status(std::format(L"Xbox stream interrupted ({}); reconnecting {}/3…",std::wstring(failure.disconnectReason.begin(),failure.disconnectReason.end()),attempt));
                                    veyra::log::warn("xbox-reconnect",std::format("attempt={} reason={}",attempt,failure.disconnectReason));
                                    for(unsigned wait=0;wait<attempt*10&&!stop_;++wait)std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                    if(stop_)break;
                                    if(!xb->connect(*xboxRequest))continue;
                                    const auto deadline=Clock::now()+std::chrono::seconds(20);
                                    while(!stop_&&Clock::now()<deadline){
                                        const AVFrame* first=nullptr;
                                        const auto result=xb->read(cachedPacket,&first);
                                        if(result==source::SourceReadStatus::Frame){cachedFrame=av_frame_clone(first);recovered=cachedFrame!=nullptr;break;}
                                        if(result==source::SourceReadStatus::Error)break;
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                    }
                                    if(recovered)break;
                                    xb->close();
                                }
                                if(recovered){
                                    initialRemoteFramePending=true;reset=true;pendingResetCause=pipeline::ResetReason::SourceSwitch;
                                    status(L"Xbox reconnected");veyra::log::info("xbox-reconnect","first decoded frame received; reset all graph history");
                                    continue;
                                }
                                if(stop_)break;
                                status(L"Xbox reconnection failed: "+xb->stats().message,true);break;
                            }
                            status(failure.message.empty()?L"Xbox streaming was interrupted; reconnect.":failure.message,true);break;
                        }
#endif
                        status(isCapture?L"Live source interrupted; reconnect":source.errorMessage().empty()?L"Video decoding or timestamp error":source.errorMessage(),true);break;}
                }
                if(isStream&&frame&&(uint32_t(frame->width)!=width||uint32_t(frame->height)!=height)){
                    drainLivePresentation();if(!ring.drainQueue()){status(L"Could not drain before switching the stream size",true);break;}
                    width=uint32_t(frame->width);height=uint32_t(frame->height);
                    describeStages(stageRequest(options),options.snapshot(),gd);
                    gd.hdrInput=!isImage&&activeSource->info().color.isHdrPath();gd.hdrOutput=options.settings.useHdrPreview(gd.hdrInput,displayHdrActive());
                    const auto resizeSourceColor=activeSource->info().color;
                    const unsigned resizeMastering=hdrMetadataNits(resizeSourceColor.hdrMasteringPeakNits);
                    const unsigned resizeCll=hdrMetadataNits(resizeSourceColor.hdrMaxCllNits);
                    const unsigned resizeFall=hdrMetadataNits(resizeSourceColor.hdrMaxFallNits);
                    if(!presenter.prepareGraphChange()){status(L"Could not drain presentation for the input size change.",true);break;}
                    graph->shutdown();recentGraphCache.evict("source-resize");
                    graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,&coreCache);graphBytes=0;out={};hasOutput=false;
                    if(!graph->initialize(gd)||!openPreparedPresenter(options)){if(!stop_)status(L"Could not switch the stream size",true);break;}
                    reset=true;pendingResetCause=pipeline::ResetReason::Resize;
                }
                if(vramRecoverStep){
                    // Video memory watchdog (see watchVram): rebuild between frames, measure later.
                    const int step=vramRecoverStep;vramRecoverStep=0;
                    drainLivePresentation();if(!ring.drainQueue()){status(L"Could not drain while reclaiming VRAM",true);break;}
                    recentGraphCache.evict("memory-recovery");
                    uint64_t budget=0;ctx.videoMemoryInfo(budget,vramRecoverBefore);
                    veyra::log::warn("vram-watch",std::format("rebuilding the {} to release video memory (usage {} MiB)",step==1?"presentation swapchain":"processing graph and swapchain",vramRecoverBefore>>20));
                    presenter.close();
                    if(step==2){graph->shutdown();graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,&coreCache);graphBytes=0;out={};hasOutput=false;}
                    if(step==1&&testLeakReleases)testLeak.clear();
                    if((step==2&&!graph->initialize(gd))||!openPreparedPresenter(options,step==2)){if(!stop_)status(L"Rebuild after reclaiming VRAM failed; open the source again",true);break;}
                    reset=true;pendingResetCause=pipeline::ResetReason::Resize;
                    vramRecoverDone=step;vramRecoverCheckAt=Clock::now()+std::chrono::seconds(3);
                }
                const bool rereadCached=transaction&&frame==cachedFrame;
                if(testReplayAsCard&&testReplayDropEvery>1&&!rereadCached){
                    if(pkt.sequence%testReplayDropEvery==0){
                        testReplayDropPending=true;
                        veyra::log::info("capture-replay-drop",std::format("source={} (test-only missed mailbox input)",pkt.sequence));
                        continue;
                    }
                    if(testReplayDropPending){pkt.flags|=static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);testReplayDropPending=false;}
                }
                if(!isImage&&!isCapture&&seekSeconds_>=0)continue;
                const bool halfRate=isCapture&&options.settings.content==ContentRate::Capture60To30&&
                    CaptureHalfRate::supported(activeSource->info().averageFps);
                const bool dedupActive=isCapture&&!isImage&&!legacyCadence&&dedupContent(options.settings.content);
                const int64_t driverPts100ns=pkt.pts.isUnknown()?0:pkt.pts.to100ns();
                const auto capturePeriod=liveSourceInterval100ns(pipeline::Rational::unknown(),activeSource->info().averageFps);
                bool captureClockCorrected=false;
                int64_t dedupHoldInterval100ns=0;
                // Drop is a late capacity-one mailbox reader, not a source clock
                // boundary. Resetting the half-rate sampler here accepted each
                // dropped packet as a new first frame, defeating 60->30 under load.
                const bool cadenceBoundary=presentationGenerationResetRequired(reset,pkt.flags);
                if(cadenceBoundary)captureSampler.reset();
                const int64_t samplePts100ns=captureClockCorrected?pkt.pts.to100ns():driverPts100ns;
                if(dedupActive&&!rereadCached&&!pkt.pts.isUnknown()){
                    const auto interval=capturePeriod;
                    pipeline::PictureReference::Result picture;
                    const auto compareStart=Clock::now();
                    const bool pictured=pipeline::captureFramePicture(frame,enginePictures,picture);
                    if(pictured&&picture.repeat){dedupRepeatMax8=std::max(dedupRepeatMax8,picture.max8);dedupRepeatMaxOver=std::max(dedupRepeatMaxOver,picture.over);}
                    else if(pictured&&picture.over)dedupNewMinOver=std::min(dedupNewMinOver,picture.over);
                    const bool boundary=cadenceBoundary||dedupRevision!=options.settings.revision;
                    dedupRevision=options.settings.revision;
                    const auto content=options.settings.content;
                    const int64_t hint=content==ContentRate::Fps30?333333:content==ContentRate::Fps50?200000:content==ContentRate::Fps60?166667:0;
                    const auto decision=contentDedup.observe(pictured?&picture.id:nullptr,pkt.pts.to100ns(),interval,boundary,hint,0);
                    const double dedupUs=elapsedMs(compareStart)*1000;
                    if(veyra::log::verboseFrameLogs())veyra::log::info("content-dedup-frame",std::format("source={} pts100ns={} keep={} reason={} picture={:x} withinBand={} over={} maxDiff={} contentPts100ns={} intervalMs={:.3f} cpuUs={:.1f}",
                        pkt.sequence,pkt.pts.to100ns(),decision.keep,decision.reason,pictured?picture.id:0,picture.repeat,picture.over,picture.max8,decision.contentPts,decision.interval/1e4,dedupUs));
                    if(!decision.keep){std::lock_guard lock(mutex_);++snapshot_.captureRateSkipped;continue;}
                    dedupHoldInterval100ns=decision.holdInterval;
                    int64_t contentPts=decision.contentPts,contentInterval=decision.interval;
                    // Frame pacing repair: frame generation shows a real frame one content
                    // interval after it arrives, which is the room the repair uses; without
                    // frame generation there is none, so it only runs with it.
                    if(options.settings.pacingRepair&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                        const bool restart=boundary||decision.holdInterval>0;
                        const auto repaired=pacingRepair.apply(pkt.pts.to100ns(),contentPts,capturePeriod,restart);
                        contentPts=repaired.pts;
                        if(pacingHaveLast&&!restart&&contentPts>pacingLastPts)contentInterval=contentPts-pacingLastPts;
                        const auto now100=host100ns();
                        if(now100>=nextPacingLog){nextPacingLog=now100+50000000;const auto& st=pacingRepair.stats();
                            veyra::log::info("pacing-repair",std::format("revision={} frames={} repaired={} restarts={} variable={} maxLateMs={:.2f} maxEarlyMs={:.2f} contentFps={:.2f}",
                                options.settings.revision,st.frames,st.repaired,st.restarts,st.variable,st.maxLate/1e4,st.maxEarly/1e4,contentDedup.contentFps()));}
                        {std::lock_guard lock(mutex_);snapshot_.pacingFrames=pacingRepair.stats().frames;snapshot_.pacingRepaired=pacingRepair.stats().repaired;}
                    }else pacingRepair.reset();
                    pacingLastPts=contentPts;pacingHaveLast=true;
                    pkt.pts={contentPts,10000000};
                    if(contentInterval>0)pkt.duration={contentInterval,10000000};
                }else if(halfRate&&!rereadCached){
                    // 60->30: every second sample on the effective capture clock; a due
                    // repeat of the last kept picture waits for the next new picture.
                    // Only a number the capture callback already computed is used, so the
                    // engine thread never compares a whole picture for this.
                    const auto interval=capturePeriod;
                    const auto shifts=captureSampler.shifts();
                    if(!pkt.pts.isUnknown()&&!captureSampler.accept(pkt.pts.to100ns(),interval,false,nullptr,pkt.sequence)){
                        std::lock_guard lock(mutex_);++snapshot_.captureRateSkipped;continue;
                    }
                    if(captureSampler.shifts()!=shifts||captureSampler.gridKept()%600==0)
                        veyra::log::info("capture-rate",std::format("60->30 phase: shifts={} keptOnGrid={} secondCopiesKept={} repeatsSkipped={} repeatsHeld={} (a 30 fps game moves onto first copies; below 30 a due repeat waits for the next picture; 60 fps content never repeats)",
                            captureSampler.shifts(),captureSampler.gridKept(),captureSampler.secondCopiesKept(),captureSampler.repeatsSkipped(),captureSampler.repeatsHeld()));
                    pkt.duration={interval*2,10000000};
                }
                if(!halfRate)captureSampler.reset();
                if(isCapture&&!rereadCached&&veyra::log::verboseFrameLogs())
                    veyra::log::info("cadence-input",std::format("source={} driverPts100ns={} samplePts100ns={} pts100ns={} clockCorrected={} dedup={} halfRate={}",pkt.sequence,driverPts100ns,samplePts100ns,pkt.pts.isUnknown()?0:pkt.pts.to100ns(),captureClockCorrected,dedupActive,halfRate));
                if(isImage)pkt.sequence=1;
#ifdef VEYRA_ENABLE_XBOX
                if(xb&&!rereadCached)xboxReconnectBudget.decoded(monotonic100ns());
#endif
                if(!rereadCached)++sourceFrames;
                const double decodeMs=elapsedMs(decodeStart);decodeTimes.add(decodeMs);
                loopTrace.mark("read");
                double pts=isImage?0:pkt.pts.toDouble()*1000;
                if(isCapture&&frames==0){anchor=Clock::now();anchorMs=pts;}
                if(seekPreviewPending)++seekDecoded;
                if(pts+0.1<discardBefore)continue;
                auto retainCandidate=[&](){
                    if(frame==cachedFrame)return true;
                    AVFrame* owned=av_frame_clone(frame);
                    if(!owned)return false;
                    av_frame_free(&cachedFrame);cachedFrame=owned;cachedPacket=pkt;
                    // The enhancement layer is a borrowed pointer whose owner
                    // (MediaFileSource::elFrame_) frees it on the next pairing or on
                    // open failure; a cached packet can outlive that. A replay of the
                    // cached candidate renders the base layer for one frame instead
                    // of dereferencing a freed frame.
                    cachedPacket.doviEl=nullptr;
                    frame=cachedFrame;
                    return true;
                };
                // Realtime preview candidates: decode stays ordered and
                // reference-lossless; an expired candidate loses only its
                // enhancement/display opportunity (plan §4.3). The open/seek
                // anchor, paused seek preview and settings replay never drop.
                if(!isCapture&&!isImage&&!transaction&&!fileAwaitingVideo&&!seekPreviewPending){
                    const double previewIntervalMs=double(liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps))/10000.0;
                    bool previewSourceFailed=false;
                    while(!stop_&&previewCandidateExpired(nowMs(),pts,previewIntervalMs)){
                        // read() can clear the decoder's borrowed AVFrame even
                        // on EOS/EAGAIN. Own this candidate before looking ahead.
                        if(!retainCandidate()){previewSourceFailed=true;break;}
                        pipeline::FramePacket next;const AVFrame* raw=nullptr;
                        const auto rs=activeSource->read(next,&raw);
                        if(rs==source::SourceReadStatus::Eos)break; // final decoded candidate is kept
                        if(rs==source::SourceReadStatus::Waiting)break; // network buffering: nothing newer yet, keep this candidate
                        if(rs!=source::SourceReadStatus::Frame||next.pts.isUnknown()){previewSourceFailed=true;break;}
                        const double nextPts=next.pts.toDouble()*1000;
                        if(nextPts+0.1<discardBefore)continue; // post-seek decode residue keeps dropping
                        pkt=next;frame=raw;
                        pts=nextPts;
                        ++previewSkippedTotal;++previewSkippedSinceSubmit;previewSkipSinceProcess=true;
                    }
                    if(previewSourceFailed){status(source.errorMessage().empty()?L"Could not read the video or keep a candidate frame":source.errorMessage(),true);break;}
                }
                if(!retainCandidate()){status(L"Could not keep the decoded frame; playback stopped",true);break;}
                if(testReplayAsCard){
                    double sum=0;for(int i=0;i<12;++i){testArrivalSeed=testArrivalSeed*1664525u+1013904223u;sum+=double(testArrivalSeed>>8)/double(1u<<24);}
                    // A live card's clock: 800 ppm slow against the host, as in the field log.
                    if(testReplayAsCard&&!testCardStart){testCardStart=host100ns();testCardFirstPtsMs=pkt.pts.toDouble()*1000;}
                    const auto line=testReplayAsCard?testCardStart+int64_t((pkt.pts.toDouble()*1000-testCardFirstPtsMs)*10000*1.0008):pkt.arrivalHost100ns;
                    const auto until=line+int64_t(std::max(0.0,sum-3)*testArrivalJitterMs*10000);
                    while(host100ns()<until){advanceLive();std::this_thread::yield();}
                    pkt.arrivalHost100ns=host100ns();
                }
                const auto processStart=Clock::now();
                const auto captureArrival=pkt.arrivalHost100ns?pkt.arrivalHost100ns:host100ns();
                // A streaming callback is a compressed AU, not a decoded video frame.
                // Its decode baseline must not consume the entire FG lookahead
                // budget. Still measure full ingress latency from captureArrival.
                const auto liveInputReady=isStream&&pkt.decodedHost100ns>0?pkt.decodedHost100ns:captureArrival;
                const auto sourceArrival=pkt.arrivalHost100ns?pkt.arrivalHost100ns:std::chrono::duration_cast<std::chrono::nanoseconds>(decodeStart.time_since_epoch()).count()/100;
                // A skipped preview candidate breaks the temporal span the
                // motion/NR/FG history was built on: the next processed frame
                // is an explicit history reset (plan §4.4). It is a SOFT
                // break: metrics windows, completion predictions and reset
                // lifecycle records continue (only hard resets — open/seek/
                // pause/discontinuity/settings — reopen them).
                const bool hardFrameReset=reset||pipeline::breaksHistory(pkt.flags&~static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop));
                // A bounded preview skip (at most two dropped candidates, no
                // source discontinuity) keeps NR/FG/XeSS history: the graph's
                // A is still the last PROCESSED frame with its real PTS, so
                // motion, generated-frame PTS and temporal blending stay on
                // the true timeline, merely spanning 2-3 source periods.
                // Clearing history here made 25% of presented real frames
                // reset frames under load (review 2026-09-22): XeSS skipped
                // generation, NR reseeded, DLSS lost the whole group. Larger
                // skips still reset. VEYRA_TEST_PREVIEW_SKIP_RESET restores
                // the unconditional reset for A/B runs.
                static const bool skipAlwaysResets=GetEnvironmentVariableW(L"VEYRA_TEST_PREVIEW_SKIP_RESET",nullptr,0)>0;
                // Retain temporal history across a skip when the gap to the
                // last processed frame is short enough that optical flow can
                // still track it. Two earlier criteria were wrong:
                //   - the count bound (<=2) used previewSkippedSinceSubmit,
                //     which is cleared on SUBMIT, not on process, so skips
                //     accumulated across frames that were processed but not
                //     submitted and tripped the bound spuriously;
                //   - a count says nothing about the actual temporal gap.
                // Measured 2026-09-22 on a user report of NR flicker under
                // load: when the scheduler falls behind it skips 4-10
                // candidates per second, so the bound never held, every burst
                // forced kReset=1 into the NR feature, and the denoiser
                // re-converged several times a second - visible as flicker at
                // exactly the drop rate. A gap this small is just a briefly
                // lower frame rate; it is not new content.
                // Capture drops land here too. Drop means the mailbox discarded
                // stale frames because WE were slow; the content is continuous
                // and the next frame is simply further along - the same
                // situation as a preview skip. It is already excluded from
                // hardFrameReset above and from the presentation reset policy,
                // but it still reached temporalBreak below, so every capture
                // drop reset NR and handed the XeSS/FSR provider reset=1. That
                // re-warm at the capture drop rate is the flicker reported for
                // live capture (2026-09-22). Real breaks still arrive as Seek,
                // Cut, Discontinuity, Resize, PauseResume or DeviceLost.
                constexpr double kRetainHistoryGapMs=250.0;
                const auto flagsWithoutDrop=pkt.flags&~static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);
                const bool captureDropped=pipeline::hasFrameFlag(pkt.flags,pipeline::FrameFlagBits::Drop);
                const bool gapCandidate=previewSkipSinceProcess||captureDropped;
                const double skipGapMs=gapCandidate&&std::isfinite(lastProcessedPtsMs)?pts-lastProcessedPtsMs:0.0;
                const bool boundedSkip=gapCandidate&&!skipAlwaysResets&&
                    std::isfinite(lastProcessedPtsMs)&&skipGapMs>0&&skipGapMs<=kRetainHistoryGapMs&&
                    !pipeline::breaksHistory(flagsWithoutDrop);
                if(boundedSkip){++previewSkipRetained;
                    if(host100ns()>=nextPreviewSkipLog){nextPreviewSkipLog=host100ns()+10000000;
                        veyra::log::info("preview-skip",std::format("retained history across a {:.1f} ms gap source={} retainedTotal={} skippedTotal={} (bounded skip; no NR/FG/XeSS reset)",skipGapMs,pkt.sequence,previewSkipRetained,previewSkippedTotal));}
                }
                const bool temporalBreak=pipeline::breaksHistory(flagsWithoutDrop)||(gapCandidate&&!boundedSkip);
                const bool previewOnlyReset=!hardFrameReset&&temporalBreak;
                if(hardFrameReset)++metricsWindowEpoch;
                const bool historyReset=hardFrameReset||temporalBreak;
                if(historyReset){++historyResets;liveArrival.reset();}
                if(presentationGenerationResetRequired(reset,pkt.flags))++presentationGeneration;
                // A mailbox Drop must reset SR/NR/flow/FG history, but draining
                // the two-batch presenter here discarded completed real frames.
                if(liveScheduler&&presentationDrainRequired(reset,pkt.flags))drainLivePresentation();
                const bool injectedReject=transaction&&!options.nr&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_DISABLE",nullptr,0)>0;
                if(injectedReject)veyra::log::error("settings-test","test-only reject NR-disable transaction before graph process; no driver failure");
                pipeline::EnhanceGraph::FgAdmission admitFg;
                // This capture pair's FG decision, for LiveFgRejectionHold.
                std::optional<pipeline::EnhanceGraph::FgDecision> capturePairDecision;
                const auto logAdmission=[&](const pipeline::FrameBatch& batch,int64_t now,int64_t deadline,double elapsed,double blit,bool admitted,bool warmingHistory,const char* timeline,int64_t firstDeadline=0,double queuedMs=0){
                    if(admitted)++fgAdmittedPairs;else ++fgRejectedPairs;
                    // Keep the first rejection even if the periodic sample just
                    // logged a success. Both channels remain rate bounded.
                    if(now<nextFgAdmissionLog&&(admitted||now<nextFgRejectionLog))return;
                    nextFgAdmissionLog=now+10000000;if(!admitted)nextFgRejectionLog=now+10000000;
                    veyra::log::info("live-fg-admission",std::format("timeline={} revision={} source={} multiplier={} requestedMultiplier={} admitted={} admittedPairs={} rejectedPairs={} remainingDeadlineMs={:.3f} firstDeadlineMs={:.3f} queuedGpuMs={:.3f} predictedMs={:.3f} elapsedMs={:.3f} blitGpuP95Ms={:.3f} presentCpuP95Ms={:.3f} warmingHistory={} (CPU Present is backpressure, not added GPU cost; firstDeadlineMs=-1 when unused)",timeline,batch.identity.settingsRevision,batch.identity.sourceFrameId,previewFgMultiplier,options.fgMultiplier,admitted,fgAdmittedPairs,fgRejectedPairs,double(deadline-now)/10000,firstDeadline?double(firstDeadline-now)/10000:-1,queuedMs,fgBudget.predicted(now,warmingHistory).value_or(-1),elapsed,blit,livePresent.p95(),warmingHistory));
                };
                if(fgBudgetRevision!=options.settings.revision||hardFrameReset){fgBudget.reset();vfgOverload.reset();vfgCostSettling.reset();vfgChosenFgMs=vfgFastFgMs=0;liveArrival.reset();pairLatency.reset();deadlineMargin.reset();fgRejectionHold.reset();fgBudgetRevision=options.settings.revision;displayFgGate.reset();}
                previewFgMultiplier=options.fgMultiplier;
                // An output cap used to only drop candidates at present time, so
                // the GPU still generated frames that were thrown away: at 6X on
                // a 100 Hz panel we generated ~360/s, submitted ~276/s, and the
                // panel could show at most 100 (measured 2026-09-22). Derive the
                // smallest multiplier whose output still reaches the cap and use
                // that instead. Only ever lowers, never raises, the multiplier the
                // user asked for; the present-time cap below stays as the residual
                // after this integer rounding.
                // The rate of new pictures, not the capture transport: a 30 FPS game in a 60 Hz
                // signal gives pairs 33 ms apart once duplicates are removed, and dividing the
                // cap by the 60 Hz transport ran a 120 FPS cap at 2X (60 FPS). Unknown while
                // duplicate removal is still measuring (0): no reduction until then.
                // (Fix ported from PushFPS 9d4e61a2.)
                const double pairContentFps=isImage?0.0:dedupActive?contentDedup.contentFps():
                    activeSource->info().averageFps*(halfRate?0.5:1.0);
                // Adaptive multiplier: the chosen multiplier becomes a ceiling and every pair uses
                // the one whose output lands closest to the target (the output cap when one is set,
                // otherwise the display refresh). Only the per-pair multiplier changes, never the
                // graph. Not for backends that pace their own frames (FSR, AMD FRC, XeSS).
                const bool adaptiveFg=options.fg&&options.settings.fgAdaptive&&!isImage&&options.fgMultiplier>2&&
                    !presentSinkFrameGeneration(options.settings.frameGenerationBackend);
                if(adaptiveFg!=adaptiveFgWas){adaptiveFgGate.reset();adaptiveFgWas=adaptiveFg;}
                if(adaptiveFg){
                    const double refresh=displayFgTestHz>0?displayFgTestHz:presenter.displayRefreshHz();
                    const double target=presentationEffective.outputRate==OutputRateMode::Custom&&std::isfinite(presentationEffective.customFps)&&
                        presentationEffective.customFps>0?presentationEffective.customFps:refresh;
                    if(pairContentFps>0.01&&target>0.01){
                        bool changed=false;
                        const auto now100=host100ns();
                        previewFgMultiplier=adaptiveFgGate.update(adaptiveFgMultiplier(options.settings.frameGenerationBackend,options.fgMultiplier,pairContentFps,target),now100,&changed);
                        if(changed||now100>=nextAdaptiveFgLog){nextAdaptiveFgLog=now100+50000000;
                            veyra::log::info("adaptive-fg",std::format("revision={} backend={} ceiling={}X chosen={}X contentFps={:.2f} targetFps={:.2f} outputFps={:.2f} changed={}",
                                options.settings.revision,frameGenerationBackendName(options.settings.frameGenerationBackend),options.fgMultiplier,previewFgMultiplier,pairContentFps,target,pairContentFps*previewFgMultiplier,changed));}
                    }else if(adaptiveFgGate.valid())previewFgMultiplier=adaptiveFgGate.applied();
                }
                if(!adaptiveFg&&options.fg&&previewFgMultiplier>2&&presentationEffective.outputRate==OutputRateMode::Custom&&
                   !presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                    const double sourceFps=pairContentFps;
                    const double cap=presentationEffective.customFps;
                    if(sourceFps>0.01&&std::isfinite(cap)&&cap>0.0){
                        const auto needed=unsigned(std::ceil(cap/sourceFps-1e-6));
                        previewFgMultiplier=std::clamp(needed,2u,options.fgMultiplier);
                    }
                }
                // Live output hint: the panel cannot show more than one new picture
                // per refresh, so suggest a multiplier that fits (DisplayFgBudget.h).
                // The multiplier stays the user's choice (2026-10-14: hint, never
                // an automatic reduction). Tearing shows partial frames by choice.
                {
                    const bool hinted=isCapture&&!isImage&&options.fg&&options.fgMultiplier>1&&
                        presentationEffective.display!=DisplaySync::Tearing;
                    double contentFps=0,refreshHz=0;
                    // The monitor refresh is reported for EVERY source, not only
                    // capture ones: the automatic multiplier needs a target rate when
                    // the output cap is off (2026-10-24). It used to stay 0 for files
                    // because the hint below is capture-only.
                    if(!isImage)refreshHz=displayFgTestHz>0?displayFgTestHz:presenter.displayRefreshHz();
                    if(hinted){
                        // Content rate: 60->30 halves the transport; dedup uses its
                        // measured cadence and suggests nothing until it has one.
                        contentFps=dedupActive?contentDedup.contentFps():activeSource->info().averageFps*(halfRate?.5:1.0);
                        if(contentFps>0&&refreshHz>0&&displayFgGate.update(displayFgSuggestion(options.settings.frameGenerationBackend,options.fgMultiplier,contentFps,refreshHz,presentationVsync(presentationEffective)),host100ns())){
                            const auto hint=displayFgGate.applied();
                            veyra::log::info("display-fg-hint",std::format("revision={} backend={} requested={}X suggestion={} outputFps={:.3f} contentFps={:.3f} refreshHz={:.3f}{} dedup={} halfRate={} display={} (hint only: the multiplier is never changed automatically)",
                                options.settings.revision,frameGenerationBackendName(options.settings.frameGenerationBackend),options.fgMultiplier,hint==0?std::string("none"):hint==1?std::string("off"):std::format("{}X",hint),contentFps*options.fgMultiplier,contentFps,refreshHz,displayFgTestHz>0?" (test override)":"",dedupActive,halfRate,unsigned(presentationEffective.display)));
                        }
                    }else displayFgGate.reset();
                    std::lock_guard lock(mutex_);
                    snapshot_.displayFgSuggestion=hinted&&displayFgGate.valid()?displayFgGate.applied():0u;
                    snapshot_.displayFgRefreshHz=refreshHz;snapshot_.displayFgContentFps=contentFps;
                }
                {std::lock_guard lock(mutex_);snapshot_.previewFgMultiplier=previewFgMultiplier;}
                // A deduplicated packet spans the last kept frame. After OUR
                // mailbox miss that duration includes missing inputs, so it must
                // not stretch the next pair's scheduling phase and cause another
                // miss. The A/B timestamps used by interpolation stay untouched.
                const auto phaseDuration=dedupHoldInterval100ns>0?
                    pipeline::Rational{dedupHoldInterval100ns,10000000}:gapCandidate&&dedupActive?
                    pipeline::Rational{contentDedup.intervalHint(capturePeriod),10000000}:pkt.duration;
                const auto liveInterval=testReplayAsCard?
                    // A span across frames WE skipped is not the source cadence. Using it
                    // stretched the next pair's presentation over the gap, which made
                    // the mailbox drop again: one background hiccup locked live capture
                    // at ~10 pairs/s (field log 2026-10-04, 60 -> 22 fps until refocus).
                    capturePairInterval100ns(phaseDuration,activeSource->info().averageFps,lastProcessedPtsMs,pts,historyReset||gapCandidate||dedupHoldInterval100ns>0):
                    livePhaseInterval100ns(pkt.duration,activeSource->info().averageFps,false);
                const auto processingAllowance=isCapture&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)?
                    fgBudget.processingAllowance(host100ns(),liveInterval):0;
                // Present-sink providers own subframe pacing after receiving B.
                // Graph FG needs the A/B presentation phase; provider FG does not.
                const auto legacyPairDelay=presentSinkFrameGeneration(options.settings.frameGenerationBackend)?0:liveInterval+processingAllowance;
                // Dedup corrects the content PTS, but the kept callbacks can still arrive
                // 17/33 ms apart for a 40 fps game. Map both times through the capture clock.
                const bool smoothAnchor=pairAnchoredLive&&options.fg&&!isStream&&!legacyLivePacing&&
                    !presentSinkFrameGeneration(options.settings.frameGenerationBackend);
                const bool smoothTransportAnchor=smoothAnchor&&!dedupActive;
                const bool smoothContentAnchor=smoothAnchor&&dedupActive&&testReplayAsCard&&!pkt.pts.isUnknown();
                const auto transportInterval=smoothContentAnchor?
                    liveSourceInterval100ns(pipeline::Rational::unknown(),activeSource->info().averageFps):0;
                const bool marginPair=adaptiveCapturePhase&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend);
                const int64_t pairExtra=marginPair?deadlineMargin.extra():0;
                // Room for the generated frames' own interpolation past interval + base cost.
                // Deduplicated pairs keep only their content-clock allowance: it already
                // covered the FG cost (auto + VFG high/high+low, 2026-11-13: no GPU-late
                // generated frames before this change), so their bound stays as it was;
                // the FG allowance is for 60->30 and transport pairs.
                const int64_t fgPhaseAllowance=marginPair&&!smoothContentAnchor?fgBudget.phaseAllowance(host100ns(),liveInterval):0;
                const auto pairDelay=marginPair?
                    pairLatency.select(host100ns(),legacyPairDelay,liveInterval,previewFgMultiplier,std::max(transportInterval/2,fgPhaseAllowance))+pairExtra:legacyPairDelay;
                std::optional<int64_t> pairPresentAnchor;
                const auto presentAnchorFor=[&](int64_t source){
                    if(!pairPresentAnchor){
                        if(smoothContentAnchor)
                            pairPresentAnchor=liveArrival.contentAnchor(source,samplePts100ns,liveInputReady,transportInterval);
                        else
                            pairPresentAnchor=smoothTransportAnchor?liveArrival.anchor(source,liveInputReady,liveInterval):liveInputReady;
                    }
                    return *pairPresentAnchor;
                };
                // DLSS can reseed after a skipped pair. XeSS
                // owns generation inside its presenter and has no graph admission.
                if(isCapture&&!rereadCached&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                    const auto presentP95=livePresentGpu.p95();
                    admitFg=[&,presentP95](const pipeline::FrameBatch& batch,bool warmingHistory){
                        // Physical capture and streaming delivery clocks are not
                        // guaranteed to match the host clock. Anchor each new
                        // input pair before enhancement, so a 59.94-vs-60Hz
                        // mismatch cannot accumulate into a stale deadline.
                        // Never extend it based on processing/ready completion.
                        // Admission asks whether this pair's own frames can make
                        // it, so it keeps the callback; LiveArrivalSmoother moves
                        // only the presentation anchor set below.
                        if(pairAnchoredLive)liveTimeline.resetPair(batch.identity.epoch,batch.b100ns,liveInputReady,pairDelay);
                        else if(!liveTimeline.anchored(batch.identity.epoch))liveTimeline.reset(batch.identity.epoch,batch.b100ns,liveInputReady,liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps));
                        const auto interval=liveInterval;
                        const auto a=(historyReset||batch.b100ns<=batch.a100ns||batch.b100ns-batch.a100ns>10000000)?batch.b100ns-interval:batch.a100ns;
                        const auto lastGenerated=pipeline::FrameBatch::interpolate(a,batch.b100ns,previewFgMultiplier-1,previewFgMultiplier);
                        const auto now=host100ns(),deadline=liveTimeline.deadline(lastGenerated);
                        const double elapsed=elapsedMs(processStart);
                        const bool admitted=!vfgCostSettling.holding(now)&&fgBudget.admit(now,deadline,elapsed,presentP95,warmingHistory);
                        logAdmission(batch,now,deadline,elapsed,presentP95,admitted,warmingHistory,pairAnchoredLive?(isStream?"decoded-pair":"capture-pair"):"continuous");
                        auto decision=admitted?pipeline::EnhanceGraph::FgDecision::Evaluate:pipeline::EnhanceGraph::FgDecision::Skip;
                        if(smoothTransportAnchor&&options.settings.vfgAutoRegulate&&previewFgMultiplier==2&&!warmingHistory&&graph->vfgFastReady()){
                            const double base=fgBudget.baseCost(now).value_or(0),progress=std::clamp(elapsed,0.0,base),blit=std::max(0.0,presentP95);
                            if(!admitted){
                                if(double(deadline-now)/10000+10>=std::max(0.0,base+vfgFastFgMs-progress)+blit)decision=pipeline::EnhanceGraph::FgDecision::FastForBudget;
                            }else if(vfgChosenFgMs>0&&presentationEffective.display!=DisplaySync::Tearing&&!presenter.vrrSync()){
                                const auto grid=presenter.refreshGrid();
                                // A time too high would never be corrected without running the
                                // model: after 4 s without it, try where 30% less would make it.
                                // 1 ms keeps clear of the refresh edge: the faster model costs nothing there.
                                const double chosen=now-vfgChosenAt>=40000000?vfgChosenFgMs*.7:vfgChosenFgMs;
                                const auto ready=now+int64_t((std::max(0.0,base+chosen-progress)+blit+1)*10000);
                                if(now-grid.lastVblank100ns<10000000&&liveFramesShareRefresh(ready,presentAnchorFor(batch.b100ns)+pairDelay,grid.lastVblank100ns,grid.period100ns,grid.latch100ns))decision=pipeline::EnhanceGraph::FgDecision::Fast;
                            }
                            if(decision==pipeline::EnhanceGraph::FgDecision::Fast)++vfgFastPairs;
                            else if(decision==pipeline::EnhanceGraph::FgDecision::FastForBudget)++vfgFastBudgetPairs;
                            if(now>=nextVfgFastLog&&(vfgFastPairs||vfgFastBudgetPairs)){
                                nextVfgFastLog=now+10000000;
                                veyra::log::info("vfg-fast",std::format("revision={} refreshPairs={} budgetPairs={} chosenPairs={} chosenFgMs={:.2f} fastFgMs={:.2f} latchMs={:.2f} (session totals: pairs run with the next faster model because the chosen one would miss the refresh or the budget, and pairs run with the chosen one)",options.settings.revision,vfgFastPairs,vfgFastBudgetPairs,vfgChosenPairs,vfgChosenFgMs,vfgFastFgMs,presenter.refreshGrid().latch100ns/10000));
                            }
                        }
                        capturePairDecision=decision;
                        return decision;
                    };
                }
                // File playback checks both the first output and whole-group
                // deadlines against the audio clock, including older queued
                // GPU work. The real frame retains its original media PTS.
                double queuedGpuMs=0;
                if(!isCapture&&!isImage&&!rereadCached&&!transaction&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                    {
                        const auto start=std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100;
                        int64_t finish=0;
                        for(const auto& previous:pendingCompletions){
                            if(previous->output.gpuComplete())continue;
                            const auto began=std::chrono::duration_cast<std::chrono::nanoseconds>(previous->processStart.time_since_epoch()).count()/100;
                            auto cost=previous->output.fgEvaluated?fgBudget.predicted(start,previous->output.historyReset||previous->output.fgRecovery):fgBudget.baseCost(start);
                            if(vfgMeasuredReducedQueue&&options.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&previous->output.fgReduced){
                                const auto first=frameFlow->snapshot(start).gpuTiming[size_t(diagnostics::GpuStage::Fg1)].p95;
                                const auto measured=vfgQueuedGroupCost(cost,fgBudget.baseCost(start),first,true,previous->output.fgEvaluated,previous->output.historyReset||previous->output.fgRecovery);
                                if(cost&&measured&&*measured<*cost&&start>=nextVfgQueueLog){nextVfgQueueLog=start+10000000;
                                    veyra::log::info("vfg-queue-budget",std::format("source={} queuedReduced=2X requested={} fullEstimateMs={:.3f} singleEstimateMs={:.3f} (base plus measured first interpolation; deadlines unchanged)",previous->output.batch.identity.sourceFrameId,options.fgMultiplier,*cost,*measured));}
                                cost=measured;
                            }
                            finish=std::max({finish,began,previous->predictedGpuStart100ns})+int64_t(cost.value_or(0)*10000);
                        }
                        queuedGpuMs=double(std::max<int64_t>(0,finish-start))/10000;
                    }
                    admitFg=[&,historyReset,queuedGpuMs](const pipeline::FrameBatch& batch,bool warmingHistory){
                        if(fileAwaitingVideo)return pipeline::EnhanceGraph::FgDecision::Evaluate; // Bounded startup lookahead; audio has not started.
                        const auto interval=liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps);
                        const auto a=(historyReset||batch.b100ns<=batch.a100ns||batch.b100ns-batch.a100ns>10000000)?batch.b100ns-interval:batch.a100ns;
                        const auto lastGenerated=pipeline::FrameBatch::interpolate(a,batch.b100ns,previewFgMultiplier-1,previewFgMultiplier);
                        const auto now=host100ns();
                        const auto media=int64_t(nowMs()*10000.0);
                        const auto firstPts=pipeline::FrameBatch::interpolate(a,batch.b100ns,1,previewFgMultiplier);
                        const auto firstDeadline=now+int64_t((firstPts-media)/playbackRate),deadline=now+int64_t((lastGenerated-media)/playbackRate);
                        const auto first=frameFlow->snapshot(now).gpuTiming[size_t(diagnostics::GpuStage::Fg1)].p95;
                        const auto elapsed=elapsedMs(processStart),blit=livePresentGpu.p95();
                        const auto outputInterval=int64_t(interval/playbackRate)/previewFgMultiplier;
                        bool retainedFits=true;
                        if(vfgRetainOverload&&options.settings.frameGenerationBackend==FrameGenerationBackend::Vfg&&!warmingHistory){
                            const auto base=fgBudget.baseCost(now),whole=fgBudget.predicted(now,false);
                            // Only replace the missing FG component; a fresh full
                            // sample still uses the original shared admission.
                            if(base&&whole&&*whole<=*base+0.000001){
                                const auto retained=vfgOverload.severeTotal(base,first,double(interval)/10000/playbackRate);
                                if(retained){
                                    retainedFits=VfgOverloadEstimate::fits(*retained,*base,double(deadline-now)/10000,double(outputInterval)/10000,elapsed,queuedGpuMs,blit);
                                    if(!retainedFits&&now>=nextVfgOverloadLog){nextVfgOverloadLog=now+10000000;
                                        veyra::log::info("vfg-overload-budget",std::format("source={} requested={} fullCostExpired=1 retainedTotalEstimateMs={:.3f} currentFirstGpuP95Ms={:.3f} (retained estimate, not new GPU sample; deadlines unchanged)",batch.identity.sourceFrameId,options.fgMultiplier,*retained,*first));}
                                }
                            }
                        }
                        const auto originalAdmission=fgBudget.admitFile(now,firstDeadline,deadline,outputInterval,elapsed,blit,first,warmingHistory,queuedGpuMs,options.settings.fgStrictAdmission);
                        const bool overloadProbe=originalAdmission&&!retainedFits&&vfgOverload.allowProbe(now);
                        if(overloadProbe)veyra::log::info("vfg-overload-probe",std::format("source={} requested={} probeIntervalMs=30000 (one full group using original admission; no deadline extension)",batch.identity.sourceFrameId,options.fgMultiplier));
                        const auto admitted=originalAdmission&&(retainedFits||overloadProbe);
                        logAdmission(batch,now,deadline,elapsed,blit,admitted,warmingHistory,"file-audio",firstDeadline,queuedGpuMs);
                        if(admitted)return pipeline::EnhanceGraph::FgDecision::Evaluate;
                        // Over budget for the full group: emit a presentable 2X
                        // midpoint instead of a whole-group hole.
                        // ON by default. Measured 2026-09-22, same slot,
                        // interleaved, 2 reps (tests/.../b10/gaps-ab-*), 100 Hz
                        // panel, per DXGI GetFrameStatistics:
                        //   native 6X   displayed 100.3/s both ways,
                        //               intervals >16.67 ms  34 -> 0
                        //   video SR 6X displayed 100.7/s both ways,
                        //               intervals >16.67 ms 150 -> 90,
                        //               gpuReadyP95 25.5 -> 22.7 ms
                        // It submits ~10 fewer frames per second, but every one
                        // of those was discarded before scanout (notDisplayed
                        // 192 vs 183), and refreshesWithoutNewFrame is 0 either
                        // way. Judge this by displayed frames and interval
                        // evenness, never by submitted frames: at 6X on a
                        // 100 Hz panel submission oversupplies by ~3x, so a
                        // submitted-frame count says nothing about what is seen.
                        // VEYRA_TEST_FG_NO_REDUCED=1 disables it for A/B runs.
                        static const bool reducedGroups=GetEnvironmentVariableW(L"VEYRA_TEST_FG_NO_REDUCED",nullptr,0)==0;
                        if(reducedGroups&&!warmingHistory&&previewFgMultiplier>2){
                            const auto midpoint=now+int64_t((pipeline::FrameBatch::interpolate(a,batch.b100ns,1,2)-media)/playbackRate);
                            if(fgBudget.canAdmitReduced(now,midpoint,int64_t(interval/playbackRate)/2,elapsed,blit,first,queuedGpuMs)){
                                ++fgReducedPairs;
                                if(now>=nextFgReducedLog){nextFgReducedLog=now+10000000;
                                    veyra::log::info("live-fg-admission",std::format("timeline=file-audio revision={} source={} reduced=2X requestedMultiplier={} reducedPairs={} remainingMidpointMs={:.3f} predictedFullMs={:.3f} (history kept; presentable midpoint instead of whole-group hole)",batch.identity.settingsRevision,batch.identity.sourceFrameId,options.fgMultiplier,fgReducedPairs,double(midpoint-now)/10000,fgBudget.predicted(now,false).value_or(-1)));}
                                return pipeline::EnhanceGraph::FgDecision::Reduced;
                            }
                        }
                        if(!warmingHistory&&fgBudget.canAdmit(now,deadline,elapsed,blit,true,queuedGpuMs))
                            return pipeline::EnhanceGraph::FgDecision::Seed;
                        return pipeline::EnhanceGraph::FgDecision::Skip;
                    };
                }
                uint64_t processWaitBase=0,processSubmitBase=0;double processWaitMsBase=0,processSlotWaitMs=0;
                if(resetRecord&&resetRecord->epoch==0)resetStageStart=Clock::now();
                // Bounded integration-test delay on the owner thread. It
                // exercises missed deadlines and recovery without changing
                // settings/history or inventing GPU execution timestamps.
                wchar_t testWork[16]{};
                const auto currentReflexFrame=(!paused_&&!isImage)?presenter.beginReflex():0;
                if(!isImage&&GetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",testWork,16))std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(_wtoi(testWork),0,150)));
                loopTrace.mark("prepare");
                if(!presenter.beginSourceProcessing()){status(L"XeSS processing timing failed",true);break;}
                bool processed=false;{
                    processWaitBase=ring.cpuWaitCount();processWaitMsBase=ring.cpuWaitMilliseconds();processSubmitBase=ring.submitCount();
                    processingGraph=true;
                    {OnExit processingDone{[&]{processingGraph=false;}};
                    // The first checkpoint: a frame due before the graph's first one.
                    if(!historyReset)presentDuringProcess();
                    processed=!injectedReject&&graph->process(frame,pts,historyReset,out,pkt.sequence,&pkt.colorInfo,&pkt.hardwareSurface,comparisonMode_!=0,admitFg,previewFgMultiplier,(paused_||isImage)&&transaction,presentDuringProcess,pkt.doviEl,&pkt.doviP7);}
                    presenter.endReflex(currentReflexFrame);
                    processSlotWaitMs=ring.cpuWaitMilliseconds()-processWaitMsBase;
                }
                loopTrace.mark("graphSubmit");
                previewSkipSinceProcess=false;
                if(processed)lastProcessedPtsMs=pts;
                if(!processed){
                    const auto failedComponent=graph->failedBackend();
                    if(!transaction){
                        auto reduced=options.snapshot();
                        if(SUCCEEDED(ctx.device()->GetDeviceRemovedReason())&&disableFailedBackend(reduced,failedComponent)){
                            const auto attempted=options.snapshot();
                            drainLivePresentation();
                            if(!ring.drainQueue()||!ring.discardRecording()){status(L"Could not drain while recovering from the enhancement failure",true);break;}
                            if(!presenter.prepareGraphChange()){status(L"Could not drain while recovering from the enhancement failure",true);break;}
                            out={};hasOutput=false;graph->shutdown();
                            {std::lock_guard lock(mutex_);reduced.revision=++nextRevision_;}
                            options=PlayerOptions::from(reduced,options.nodeOrder,&options);
                            describeStages(stageRequest(options),options.snapshot(),gd);
                            if(!initializePreview(gd,options)){if(!stop_)status(L"Could not rebuild the base picture after the enhancement failure",true);break;}
                            publishFrameGenerationCapabilities();
                            const bool mlToFsr3=failedComponent==FailedBackend::Fg&&attempted.frameGenerationBackend==FrameGenerationBackend::Fsr4&&
                                options.settings.frameGenerationBackend==FrameGenerationBackend::Fsr;
                            backendRecoveryWarning=(mlToFsr3?std::wstring(L"FSR 4 ML failed while running; switched to FSR 3.1. Error codes are in the log"):
                                std::wstring(backendFailureName(failedComponent))+L"Running failed; the effect involved was turned off. Error code in the log")+(backendRecoveryWarning.empty()?L"":L"; "+backendRecoveryWarning);
                            {std::lock_guard lock(mutex_);desired_.rejectVideoRequest(attempted,options.snapshot());snapshot_.desired=desired_;snapshot_.applied=options.snapshot();snapshot_.rejectedRevision=attempted.revision;snapshot_.applying=desired_!=snapshot_.applied;snapshot_.backendWarning=backendRecoveryWarning;}
                            veyra::log::warn("backend-recovery",std::format("runtime component={} disabled; rebuilt revision={} nr={} sr={} multiplier={}; retry next source frame",unsigned(failedComponent),options.settings.revision,options.nr,options.sr,options.snapshot().multiplier));
                            finishReset(diagnostics::ResetOutcome::Failed);reset=true;pendingResetCause=pipeline::ResetReason::Settings;
                            holdFileAudio();fileAudioAlignPending=true;
                            continue;
                        }
                    }
                    if(transaction){
                        if(!ring.drainQueue()||!presenter.prepareGraphChange()){status(L"Could not drain presentation to restore settings. Playback stopped.",true);break;}
                        out={};graph->shutdown();recentGraphCache.evict("process-rollback");
                        graph=std::make_unique<pipeline::EnhanceGraph>(ctx,ring,&coreCache);graphBytes=0;options=PlayerOptions::from(previous,previousOrder);
                        gd=previousDesc;
                        if(graph->initialize(gd)&&openPreparedPresenter(options)&&graph->process(frame,pts,true,out,pkt.sequence,&pkt.colorInfo,&pkt.hardwareSurface,comparisonMode_!=0,{},0,false,{},pkt.doviEl,&pkt.doviP7)){
                            publishFrameGenerationCapabilities();
                            finishReset(diagnostics::ResetOutcome::RolledBack);
                            std::lock_guard lock(mutex_);if(desired_.revision==requested.revision)desiredNodeOrder_=previousOrder;desired_.rejectVideoRequest(requested,previous);snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_!=previous;snapshot_.status=L"Settings failed to apply; the whole set was rolled back";transaction=false;
                        }else{if(!stop_)status(L"Settings rollback failed; stopped",true);break;}
                    }else{
                        // Custom: a bare "failed; see the log" leaves the user with
                        // nothing to do. Name the likely cause and the action that
                        // actually recovers it, per backend.
                        std::wstring advice;
                        switch(failedComponent){
                        case FailedBackend::Infrastructure:
                            advice=L". Hardware decoding may have failed to allocate memory, especially for 4K. Restart the player; if it recurs, choose Software decoding in Settings → Playback";
                            break;
                        case FailedBackend::VideoHdr:
                            advice=L". RTX Video HDR could not start: turn the effect off in settings, or update the NVIDIA driver";
                            break;
                        case FailedBackend::OpticalFlow:
                            advice=L". The optical-flow backend is unavailable: switch to AMD FidelityFX in settings, or disable the dependent effects";
                            break;
                        default:
                            advice=L"; see the log for details";
                            break;
                        }
                        status(std::wstring(backendFailureName(failedComponent))+L" failed"+advice,true);
                        break;
                    }
                }
                if(veyra::log::verboseFrameLogs())veyra::log::info("source-identity",std::format("source={} totalRead={} graphProcessed={} cached={} revision={} nvofStandalone={}",pkt.sequence,sourceFrames,frames+1,rereadCached,options.settings.revision,gd.enableNvofStandalone));
                // Custom: hand this source frame's Dolby Vision level 1 to the
                // presenter, which will show this batch next. The value only
                // moves the highlight curve, and only when the follow switch is
                // on; an invalid value leaves the static curve untouched.
                presenter.setFrameDovi(pkt.dovi);
                presenter.sourceProcessed(out.batch.identity);
                if(out.fgReduced||previewFgMultiplier!=options.fgMultiplier){std::lock_guard lock(mutex_);snapshot_.previewFgMultiplier=out.fgReduced?2u:previewFgMultiplier;}
                reset=false;hasOutput=true;
                const auto processDone=Clock::now();
                if(veyra::log::verboseFrameLogs()&&testReplayAsCard)veyra::log::info("capture-graph-sample",std::format("source={} epoch={} revision={} arrival={} start={} submitted={} slotWaitMs={:.6f}",pkt.sequence,out.batch.identity.epoch,out.batch.identity.settingsRevision,captureArrival,std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100,std::chrono::duration_cast<std::chrono::nanoseconds>(processDone.time_since_epoch()).count()/100,processSlotWaitMs));
                // The restored graph's first frame has empty temporal history.
                // It is the failed transaction's rollback warmup, not another
                // successful user reset that may overwrite the failure record.
                const bool restoredWarmup=rollbackWarmupPending&&
                    (pendingResetCause==pipeline::ResetReason::None||pendingResetCause==pipeline::ResetReason::Settings);
                rollbackWarmupPending=false;
                if(!resetRecord&&out.historyReset&&!previewOnlyReset&&!restoredWarmup){
                    resetStart=processStart;resetStageStart=processStart;
                    resetRecord=diagnostics::FrameFlowMetrics::ResetRecord{};resetRecord->sessionId=runSessionId;resetRecord->settingsRevision=out.batch.identity.settingsRevision;resetRecord->epoch=out.batch.identity.epoch;resetRecord->sourceFrameId=out.batch.identity.sourceFrameId;resetRecord->reason=static_cast<uint8_t>(diagnostics::resetCause(pkt.flags,pendingResetCause,out.detectedReset));
                    resetRecord->stageMs[size_t(diagnostics::ResetStage::Drain)]=0.0;resetRecord->stageMs[size_t(diagnostics::ResetStage::Destroy)]=0.0;resetRecord->stageMs[size_t(diagnostics::ResetStage::Create)]=0.0;
                    resetRecord->rebuilt=false;markResetStage(diagnostics::ResetStage::Warmup);
                    if(frameFlow)frameFlow->resetLifecycle(*resetRecord);
                }
                if(resetRecord&&resetRecord->epoch==0){
                    resetRecord->epoch=out.batch.identity.epoch;resetRecord->sourceFrameId=out.batch.identity.sourceFrameId;
                    markResetStage(diagnostics::ResetStage::Warmup);
                }
                pendingResetCause=pipeline::ResetReason::None;
                if(!frameFlow||metricsRevision!=options.settings.revision||metricsEpoch!=metricsWindowEpoch){
                    const bool settingsChanged=metricsRevision!=options.settings.revision;
                    metricsRevision=options.settings.revision;metricsEpoch=metricsWindowEpoch;statsStart=Clock::now();statsSourceBase=sourceFrames-uint64_t(!rereadCached);
                    slotWaitBase=processWaitBase;slotWaitMsBase=processWaitMsBase;submitBase=processSubmitBase;
                    captureAges.clear();scheduleWaits.clear();processTimes.clear();presentTimes.clear();decodeTimes.clear();gpuReadyTimes.clear();for(auto& window:gpuStageTimes)window.clear();lastGpuSampleEnd={};presentEntryDeviation.clear();presentReturnDeviation.clear();submissionTimes.clear();submitted=expired=0;
                    // Admission predictions and the XeSS gate must not carry
                    // samples across a settings revision (new backend/dimensions);
                    // soft preview-skip history breaks do NOT reopen them.
                    if(settingsChanged)xessSubmissions.reset(presenter.generation(),presenter.xessPresentedCount(),presenter.xessGeneratedCount());
                    playbackSpeedLastWall={};
                    if(liveScheduler){liveStats={};liveSubmissions.clear();liveAges.clear();liveWaits.clear();livePresent.clear();livePresentGpu.clear();liveReady.clear();}
                    if(settingsChanged||!completionRates)completionRates=std::make_shared<FrameCompletionRates>(host100ns());
                    frameFlow=std::shared_ptr<FrameFlowWindow>(new FrameFlowWindow(runSessionId,out.batch.identity,host100ns(),completionRates),[](FrameFlowWindow* window){logFrameFlow(window->snapshot(monotonic100ns()),"closed");delete window;});
                    if(resetRecord)frameFlow->resetLifecycle(*resetRecord);else if(lastResetRecord)frameFlow->resetLifecycle(*lastResetRecord);
                    frameFlow->update([&](auto& m){m.counters.settingsResets+=uint64_t(settingsChanged);});
                    captureFlowBase=captureFlowLast;
                    // The published totals belong to the window that counted them. A UI poll between
                    // this switch and the next publish must not pair the new window with the previous
                    // revision's counts ("disabled revision cannot retain prior generated-frame count").
                    {std::lock_guard lock(mutex_);rateSkippedBase=snapshot_.captureRateSkipped;activeFlow_=frameFlow;snapshot_.metrics.sourceFrames=snapshot_.metrics.validGenerated=snapshot_.metrics.submitted=snapshot_.metrics.expired=0;}
                    veyra::log::info("metrics",std::format("reset session={} appliedRevision={} epoch={} sourceBase={}",runSessionId,metricsRevision,metricsEpoch,statsSourceBase));
                }
                // Published after the metrics window switch above: a snapshot that already reports
                // the new revision never carries the previous revision's totals. The publish of
                // this frame's metrics can be a while later.
                if(transaction||pendingSettingsAck){std::lock_guard lock(mutex_);snapshot_.applied=options.snapshot();snapshot_.applying=desired_!=snapshot_.applied;pendingSettingsAck=false;snapshot_.status=std::format(L"Input {}×{} / base {}×{} / NR {}×{} / optical flow {}×{} / FG and output {}×{} | {}",width,height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.flowWidth,gd.flowHeight,gd.workWidth,gd.workHeight,gd.nrBeforeSr?L"Low latency · NR first, then SR":gd.nrWidth<gd.workWidth?L"Processed internally in real time and written back":L"Native NR (higher performance cost)");veyra::log::info("display-color",std::format("hdrInput={} hdrOutput={} forceSdrPreview={}",gd.hdrInput,gd.hdrOutput,options.settings.forceSdrPreview));veyra::log::info("settings",std::format("Applied revision={} sourcePtsMs={} fgBackend={} flowBackend={} multiplier={} (source kept open)",options.settings.revision,pts,frameGenerationBackendName(options.settings.frameGenerationBackend),opticalFlowBackendName(options.settings.opticalFlowBackend),options.snapshot().multiplier));}
                const double processMs=std::chrono::duration<double,std::milli>(processDone-processStart).count();
                frameFlow->advanceHistory(out.batch.identity);
                frameFlow->update([&](auto& m){m.counters.historyResets+=uint64_t(out.historyReset);m.counters.captureDropResets+=uint64_t(pipeline::hasFrameFlag(pkt.flags,pipeline::FrameFlagBits::Drop));});
                const auto lineage=lineageTracker.observe(out.batch,sourceArrival,pkt.arrivalHost100ns>0,rereadCached||isImage);
                if(lineage&&out.hasGenerated){
                    frameFlow->pairArrived(*lineage);
                    if(veyra::log::verboseFrameLogs())veyra::log::info("frame-lineage",std::format("batch={} epoch={} revision={} sourceA={} sourceB={} ptsA={} ptsB={} arrivalA={} arrivalB={} captureCallbacks={}",out.batch.batchId,out.batch.identity.epoch,out.batch.identity.settingsRevision,lineage->a.identity.sourceFrameId,lineage->b.identity.sourceFrameId,lineage->a.pts100ns,lineage->b.pts100ns,lineage->a.host100ns,lineage->b.host100ns,lineage->a.captureCallback&&lineage->b.captureCallback));
                }
                processTimes.add(processMs);
                traceFrame(diagnostics::TraceKind::Submitted,out.batch.identity,out.batch.batchId,std::max(out.videoFenceValue,out.genFenceValue),out.batch.b100ns,out.fgSkippedBeforeEval,out.fgEvaluated,processMs);
                frameFlow->cpu(diagnostics::CpuStage::Decode,decodeMs,host100ns());
                if(isStream&&pkt.decodedHost100ns>0&&!rereadCached)frameFlow->cpu(diagnostics::CpuStage::DecodedQueue,std::max(0.0,double(std::chrono::duration_cast<std::chrono::nanoseconds>(decodeStart.time_since_epoch()).count()/100-pkt.decodedHost100ns)/10000),host100ns());
                frameFlow->update([&](auto& m){m.lastSubmit100ns=host100ns();});
                frameFlow->cpu(diagnostics::CpuStage::Submit,processMs,host100ns());
                frameFlow->cpu(diagnostics::CpuStage::SlotWait,processSlotWaitMs,host100ns());
                frameFlow->update([&](auto& m){m.latest.frame=out.batch.identity;m.latest.batchId=out.batch.batchId;m.latest.readyFence=std::max(out.videoFenceValue,out.genFenceValue);m.counters.sourceAccepted+=!rereadCached;++m.counters.realSubmitted;m.counters.previewSkippedBeforeGraph+=previewSkippedSinceSubmit;m.counters.fgCandidate+=out.fgCandidates;m.counters.fgEvaluated+=out.fgEvaluated;m.counters.fgSkippedForReset+=out.fgSkippedForReset;m.counters.fgSkippedBeforeEval+=out.fgSkippedBeforeEval;m.counters.fgReduced+=uint64_t(out.fgReduced);if(out.fgSkippedBeforeEval||out.fgBudgetSeed)m.lastFgRejected100ns=host100ns();if(!out.hasGenerated)m.counters.fgWarmup+=out.fgEvaluated;});
                previewSkippedSinceSubmit=0;
                if(transaction||frames==0){
                    std::lock_guard lock(mutex_);snapshot_.captureHalfRate=halfRate;
                    veyra::log::info("capture-rate",std::format("revision={} requested60To30={} active={} transportFps={} originalPtsPreserved={}",options.settings.revision,options.settings.content==ContentRate::Capture60To30,halfRate,isCapture?activeSource->info().averageFps:0,!captureClockCorrected));
                }
                const bool pairPacing=pairAnchoredLive&&options.fg;
                // File replay has no device pacing and still needs its PTS clock.
                // Physical and screen capture without FG present as soon as ready.
                const bool paceSourcePts=livePacesSourcePts(options.fg,false,false);
                if(isCapture&&!rereadCached&&(isStream||pairPacing||!liveTimeline.anchored(out.batch.identity.epoch,paceSourcePts))){
                    const auto duration100ns=liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps);
                    if(!liveTimeline.anchored(out.batch.identity.epoch,paceSourcePts)||(pairPacing&&frames==0))veyra::log::info("capture-timeline",std::format("interval100ns={} packetDurationKnown={} packetDurationPositive={} nominalFps={} FG={} pacing={}",duration100ns,!pkt.duration.isUnknown(),pkt.duration.num>0,activeSource->info().averageFps,options.fg,pairPacing?"capture-pair":paceSourcePts?"source-pts":"capture-ready"));
                    if(pairPacing||isStream){
                        // LiveFgRejectionHold: after a second of FG rejections the real frame goes
                        // out when ready. Admission above still used the FG phase.
                        bool fgHeld=false;
                        if(marginPair&&capturePairDecision){
                            const bool wasHeld=fgRejectionHold.held();
                            const bool admittedPair=*capturePairDecision!=pipeline::EnhanceGraph::FgDecision::Skip;
                            fgHeld=fgRejectionHold.observe(host100ns(),admittedPair);
                            fgHeldPairs+=fgHeld;fgHeldProbes+=fgHeld&&admittedPair;
                            if(wasHeld!=fgRejectionHold.held())
                                veyra::log::info("live-fg-hold",std::format("revision={} held={} source={} heldPairs={} heldProbes={} (FG rejected for {:.0f} s: real frames shown when ready instead of on the FG phase; pairs admitted while held expire their generated frames; {} admitted pairs in a row resume)",
                                    options.settings.revision,fgRejectionHold.held(),out.batch.identity.sourceFrameId,fgHeldPairs,fgHeldProbes,double(LiveFgRejectionHold::kHold100ns)/1e7,LiveFgRejectionHold::kResumePairs));
                        }
                        // Keep the smoother learning while held.
                        const auto smoothedAnchor=presentAnchorFor(out.batch.b100ns);
                        const auto presentAnchor=fgHeld?liveInputReady:smoothedAnchor;
                        liveGeneratedGrace=liveGeneratedGrace100ns(liveInputReady,presentAnchor);
                        liveTimeline.resetPair(out.batch.identity.epoch,out.batch.b100ns,presentAnchor,options.fg&&!fgHeld?pairDelay:0,paceSourcePts);
                        const bool smoothedCapacity=smoothTransportAnchor||smoothContentAnchor;
                        if(smoothedCapacity&&host100ns()>=nextLiveAnchorLog){
                            nextLiveAnchorLog=host100ns()+10000000;
                            veyra::log::info("live-anchor",std::format("revision={} contentClock={} callbackJitterMs={:.3f} clockDriftPpm={:.0f} snaps={} callbackMinusAnchorMs={:.3f} phaseMs={:.3f} deadlineExtraMs={:.3f} fgPhaseAllowanceMs={:.3f} fgHeld={} capacityWaitMs={:.2f} (presentation anchor: source or deduplicated content PTS on the tracked capture clock; fgHeld: real frame shown when ready, phase unused; capacity wait applies under explicit VSync)",options.settings.revision,smoothContentAnchor,liveArrival.jitterMs(),liveArrival.driftPpm(smoothContentAnchor?transportInterval:liveInterval),liveArrival.snaps(),double(liveInputReady-presentAnchor)/10000,double(pairDelay)/10000,double(pairExtra)/10000,double(fgPhaseAllowance)/10000,fgHeld,liveSmoothedCapacityWaitMs(double(liveInterval)/10000/std::max(1u,previewFgMultiplier),fixedCapacityWait?0:liveArrival.jitterMs())));
                        }
                    }
                    else if(!liveTimeline.anchored(out.batch.identity.epoch,paceSourcePts))liveTimeline.reset(out.batch.identity.epoch,out.batch.b100ns,liveInputReady,options.fg?duration100ns:0,paceSourcePts);
                }
                if(!isImage&&!isCapture&&frames==0){anchor=Clock::now();anchorMs=lastAudioClockMs=pts;}
                double frameWaitMs=0,framePresentMs=0;
                double gpuWaitMs=0;
                if(liveScheduler){
                    const auto timeline=liveTimeline;
                    const uint64_t jobGeneration=presentationGeneration.load();
                    auto watch=std::make_shared<CompletionWatch>();watch->output=out;watch->flow=frameFlow;watch->processStart=processStart;watch->real=!rereadCached;
                    watch->predictedGpuStart100ns=std::chrono::duration_cast<std::chrono::nanoseconds>(processStart.time_since_epoch()).count()/100+int64_t(queuedGpuMs*10000);
                    watch->captureArrival=captureArrival;watch->presentationGeneration=jobGeneration;
                    // Learn enough phase for early content-clock deadlines on late capture slots.
                    watch->phaseAnchor=smoothContentAnchor?presentAnchorFor(out.batch.b100ns):captureArrival;
                    watch->reflexFrame=currentReflexFrame;
                    pendingCompletions.push_back(watch);
                    struct LiveStepState {
                        unsigned next=0,handled=0,remaining=0;bool readyReported=false;
                        // A frame this step found still in the GPU: its readiness is then seen
                        // when the fence completes (the loop waits on it), not when we look.
                        std::array<bool,pipeline::FrameBatch::Capacity> seenUnfinished{};
                        Clock::time_point readyStart=Clock::now();std::optional<Clock::time_point> deadlineStart,capacityStart;
                        double capacityWaitMs=0;
                        double readyMs=0,waitMs=0,presentMs=0,ageMs=0;uint64_t count=0,dropped=0;
                        diagnostics::GpuSample blit;
                    };
                    auto step=std::make_shared<LiveStepState>();
                    watch->jobStart=step->readyStart;
                    const bool submitThenPresent=isCapture&&presenter.xessActive()&&!xessPresentAfterReady;
                    for(unsigned i=0;i<out.batch.count;++i)if(out.batch.frames[i].kind!=pipeline::FrameKind::Generated||out.batch.frames[i].validity==pipeline::GenerationValidity::Valid)++step->remaining;
                    const double sourceIntervalMs=double(isCapture?liveInterval:liveSourceInterval100ns(pkt.duration,activeSource->info().averageFps))/10000;
                    const auto baselineReady=pkt.decodedHost100ns>0?pkt.decodedHost100ns:captureArrival;
                    const bool delayEnhanced=options.nr||options.sr||options.fg;
                    // Captures are explicit on purpose (sweep 2026-09-22 B1): this step runs
                    // from the scheduler queue for up to two source frames, so every
                    // by-reference capture below must outlive the scheduler. The guard
                    // that destroys the scheduler is declared immediately before the run
                    // loop, so all of these are declared above it. Adding a reference to a
                    // local declared after that guard is now a compile error instead of a
                    // silent dangling reference.
                    if(!liveScheduler->push([this,testReplayAsCard,&anchor,&anchorMs,&xessSubmissions,&lastFilePresentLateness,&lastFilePresentedMs,&liveSubmissions,&presentationCompletedReal,
#ifdef VEYRA_ENABLE_MOONLIGHT
                        &moon,
#endif
#ifdef VEYRA_ENABLE_XBOX
                        &xb,
#endif
                        &seekDecoded,&seekStarted,&seekDemuxDone,&audio,&audioPipe,&audioStarted,&cadence,&ctx,&fileAudioAlignPending,&fileAwaitingVideo,&fileInputEnded,&generatedDropCap,&generatedDropCapacity,&generatedDropDeadline,&playbackRate,&frameFlow,&graph,&host100ns,&isCapture,&isImage,&liveArrival,&livePresent,&liveScheduler,&nextFgDeadlineLog,&nowMs,&options,&pendingCompletions,&processingGraph,&presentEntryDeviation,&presentReturnDeviation,&presentationEffective,&presentationGeneration,&presentationSkippedGenerated,&presenter,&ring,&runSessionId,&seekPreviewPending,&traceFrame,&traceSubframes,watch,step,timeline,captureArrival,baselineReady,delayEnhanced,activeSeekId,lineage,jobGeneration,rereadCached,sourceIntervalMs,submitThenPresent,generatedGrace=liveGeneratedGrace,smoothedCapacity=smoothTransportAnchor||smoothContentAnchor,halfRate,captureClockCorrected,driverPts100ns,flow=frameFlow,&deadlineMargin,marginPair,pairExtra](int64_t now)->LiveGpuScheduler::Step{

                        using State=LiveGpuScheduler::State;auto& batch=watch->output;auto& s=*step;
                        auto updatePending=[&](LiveStepState*){
                            unsigned left=0;for(unsigned i=s.next;i<batch.batch.count;++i)if(batch.batch.frames[i].kind!=pipeline::FrameKind::Generated||batch.batch.frames[i].validity==pipeline::GenerationValidity::Valid)++left;
                            flow->update([&](auto& m){if(left>s.remaining)m.pendingOutputFrames+=left-s.remaining;else m.pendingOutputFrames-=std::min(m.pendingOutputFrames,s.remaining-left);});s.remaining=left;
                        };std::unique_ptr<LiveStepState,decltype(updatePending)> pendingUpdate(&s,updatePending);
                        if(watch->ready&&!s.readyReported){s.readyMs=std::max(0.0,std::chrono::duration<double,std::milli>(watch->readyObserved-s.readyStart).count());s.readyReported=true;flow->cpu(diagnostics::CpuStage::ReadyWait,s.readyMs,host100ns());}
                        if(!isCapture&&fileAwaitingVideo&&!seekPreviewPending&&!fileInputEnded&&
                           (liveScheduler->occupancy()<2||!pendingCompletions.empty()))return {State::Pending,now+2000};
                        if(!isCapture&&audioStarted&&fileAudioAlignPending){
                            if(audioPipe.endpointRecovering())return {State::Pending,now+10000};
                            const double ptsMs=double(batch.batch.b100ns)/10000,media=audio.mediaTimeMs();
                            if(std::isfinite(media)&&media+1<ptsMs&&!std::isfinite(audioPipe.requestSeek(ptsMs)))return {State::Failed};
                            fileAudioAlignPending=false;
                        }
                        while(s.next<batch.batch.count){auto& item=batch.batch.frames[s.next];const bool generated=item.kind==pipeline::FrameKind::Generated;
                            if(stop_||(paused_&&!seekPreviewPending)||seekSeconds_>=0)return {State::Complete};
                            const double itemPtsMs=double(item.pts100ns)/10000;
                            const auto decisionTime=host100ns();
                            const bool generationExpired=generated&&(isCapture?timeline.expired(item.pts100ns,decisionTime,generatedGrace):!fileAwaitingVideo&&previewGeneratedExpired(nowMs(),itemPtsMs));
                            // XeSS shares the producer queue; native no-FG capture has an explicit GPU-side fence wait.
                            const bool early=submitThenPresent&&!generated&&!watch->ready;
                            if(!watch->frameReadyObserved[s.next]&&item.lease&&item.lease->ready())watch->frameReadyObserved[s.next]=host100ns();
                            const auto readiness=previewFrameReadiness(item,comparisonMode_!=0||!generatedPresentationCurrent(jobGeneration,presentationGeneration.load()),generationExpired,[&]{return early||graph->resolveFrame(batch,s.next);});
                            if(early&&readiness==PreviewFrameReadiness::Ready&&!s.readyReported){watch->presentedBeforeReady=true;s.readyReported=true;}
                            if(traceSubframes&&generated&&(readiness==PreviewFrameReadiness::Expired||readiness==PreviewFrameReadiness::Suppressed))
                                traceFrame(diagnostics::TraceKind::Discarded,item.identity,batch.batch.batchId,item.lease->readyFence,item.pts100ns,item.subframe,unsigned(readiness),isCapture?double(decisionTime-timeline.deadline(item.pts100ns))/10000:nowMs()-itemPtsMs);
                            if(readiness==PreviewFrameReadiness::Pending){
                                if(elapsedMs(s.readyStart)>2000){
                                    ctx.reportDeviceFailure("GPU-frame-ready-timeout",item.lease?item.lease->readyFence:0);
                                    veyra::log::error("capture-present",std::format("GPU frame ready timeout nr={} nrRuntime={} source={} epoch={} revision={} subframe={} dimensions={}x{} nrDimensions={}x{} flowBackend={}",graph->nrEnabled(),nrRuntimeName(options.settings.nrRuntime),item.identity.sourceFrameId,item.identity.epoch,item.identity.settingsRevision,item.subframe,graph->workWidth(),graph->workHeight(),graph->nrWidth(),graph->nrHeight(),opticalFlowBackendName(options.settings.opticalFlowBackend)));
                                    return {State::Failed};
                                }
                                s.seenUnfinished[s.next]=true;
                                return {State::Pending,host100ns()+2000};
                            }
                            if(readiness==PreviewFrameReadiness::Invalid){++s.handled;++s.next;s.deadlineStart.reset();continue;}
                            if(readiness==PreviewFrameReadiness::Suppressed){++presentationSkippedGenerated;++s.next;s.deadlineStart.reset();continue;}
                            if(isCapture&&readiness==PreviewFrameReadiness::Ready&&!s.readyReported){s.readyMs=elapsedMs(s.readyStart);s.readyReported=true;flow->cpu(diagnostics::CpuStage::ReadyWait,s.readyMs,host100ns());}
                            if(generated&&isCapture&&decisionTime>=nextFgDeadlineLog){
                                nextFgDeadlineLog=decisionTime+10000000;
                                const auto ready=watch->frameReadyObserved[s.next];
                                veyra::log::info("fg-deadline",std::format("revision={} batch={} subframe={} expired={} readyObservedLateMs={:.3f} decisionLateMs={:.3f} observedReadyToDecisionMs={:.3f} batchReady={} (CPU fence observations, not scanout; -1 means readiness unknown)",item.identity.settingsRevision,batch.batch.batchId,item.subframe,generationExpired,ready?double(ready-timeline.deadline(item.pts100ns))/10000:-1,double(decisionTime-timeline.deadline(item.pts100ns))/10000,ready?double(decisionTime-ready)/10000:-1,watch->ready.load()));
                                veyra::log::info("fg-drops",std::format("revision={} deadline={} capacity={} cap={} graceMs={:.1f} (session totals of frames dropped before Present, by reason)",item.identity.settingsRevision,generatedDropDeadline,generatedDropCapacity,generatedDropCap,double(generatedGrace)/10000));
                            }
                            if(readiness==PreviewFrameReadiness::Expired){++generatedDropDeadline;++s.dropped;++s.handled;++s.next;s.deadlineStart.reset();s.capacityStart.reset();continue;}
                            if(!s.deadlineStart)s.deadlineStart=Clock::now();
                            if(isCapture){if(host100ns()<timeline.deadline(item.pts100ns))return {State::Pending,timeline.deadline(item.pts100ns),timeline.paced()&&!presenter.providerOwnedPresentation()};}
                            else if(!fileAwaitingVideo){
                                if(audioStarted)audioPipe.videoReady(itemPtsMs);
                                if(nowMs()+.25<itemPtsMs)return {State::Pending,now+std::min<int64_t>(10000,int64_t((itemPtsMs-nowMs())*10000/playbackRate))};
                            }
                            const auto optionalNow=host100ns();
                            if(!isCapture&&!fileAwaitingVideo&&options.fg&&!presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
                                const auto interval=std::max<int64_t>(1,int64_t(sourceIntervalMs*10000/playbackRate)/std::max(1u,batch.batch.count));
                                const auto due=cadence.due(optionalNow+int64_t((itemPtsMs-nowMs())*10000/playbackRate),interval,20);
                                if(optionalNow<due)return {State::Pending,due};
                            }
                            int64_t capInterval=0;
                            if(presentationEffective.enabled||presentationEffective.outputRate==OutputRateMode::Custom){
                                const auto interval=std::max<int64_t>(1,int64_t(sourceIntervalMs*10000/playbackRate)/std::max(1u,batch.batch.count));
                                const auto mediaDeadline=isCapture?timeline.cadenceDeadline(item.pts100ns,optionalNow):fileAwaitingVideo?optionalNow:optionalNow+int64_t((itemPtsMs-nowMs())*10000/playbackRate);
                                // Media time is the deadline. The old "even" mode
                                // added a spacing floor on top; it measured as a
                                // no-op because production already lands within
                                // 0.8 ms of media time (b13/pacing-ab).
                                auto due=mediaDeadline;
                                if(presentationEffective.outputRate==OutputRateMode::Custom){
                                    capInterval=std::max<int64_t>(1,int64_t(10000000.0/presentationEffective.customFps));
                                    due=std::max(due,cadence.rateDue(optionalNow,capInterval));
                                }
                                // The cap selects displayed candidates, never
                                // changes processing/FG history or media time.
                                // If the next candidate is due before this
                                // slot, discard this stale display opportunity
                                // instead of accumulating latency.
                                if(capInterval>0&&cadence.rateSkipsCandidate(optionalNow,mediaDeadline,interval,capInterval)){++generatedDropCap;++s.dropped;++s.handled;++s.next;s.deadlineStart.reset();continue;}
                                if(optionalNow<due)return {State::Pending,due};
                            }
                            // Explicit VSync: one present of display capacity per present, so a
                            // frame never waits in DXGI's queue (true when not paced). A generated
                            // frame waits for a bounded interval chosen at the first miss.
                            // A 60->30 / 2X pair can use spare refresh slots if one
                            // full refresh remains for its real frame.
                            if((!legacyVsyncQueue||presentationEffective.enabled||presentationEffective.outputRate==OutputRateMode::Custom)&&!presenter.presentationReady()){
                                if(generated&&!legacyVsyncQueue){
                                    if(!s.capacityStart){
                                        s.capacityStart=Clock::now();
                                        const double interval=sourceIntervalMs/std::max(1u,batch.batch.count);
                                        const double base=smoothedCapacity?liveSmoothedCapacityWaitMs(interval,fixedCapacityWait?0:liveArrival.jitterMs()):2.0;
                                        s.capacityWaitMs=std::min(base,interval/2);
                                        if(halfRate&&batch.batch.count==2&&presentationEffective.display==DisplaySync::Vsync&&
                                           !presenter.vrrSync()&&!capInterval&&!fixedCapacityWait){
                                            const auto grid=presenter.refreshGrid();
                                            const double untilReal=double(timeline.deadline(batch.batch.b100ns)-optionalNow)/10000;
                                            s.capacityWaitMs=liveHalfRateCapacityWaitMs(base,interval,grid.period100ns/10000,untilReal);
                                        }
                                    }
                                    if(elapsedMs(*s.capacityStart)>s.capacityWaitMs){++generatedDropCapacity;++s.dropped;++s.handled;++s.next;s.deadlineStart.reset();s.capacityStart.reset();continue;}
                                }
                                return {State::Pending,optionalNow+2000};
                            }
                            s.capacityStart.reset();
                            const auto waited=elapsedMs(*s.deadlineStart);s.deadlineStart.reset();s.waitMs+=waited;flow->cpu(diagnostics::CpuStage::DeadlineWait,waited,host100ns());
                            const bool deviationValid=!isCapture&&!isImage&&!fileAwaitingVideo&&!paused_;
                            const double entryDeviation=deviationValid?nowMs()-itemPtsMs:0;
                            const auto presentBeginHost=host100ns();
                            const auto begin=Clock::now();const auto before=presenter.submittedCount();
                            presenter.reflexFrame(watch->reflexFrame);
                            presenter.observeHdrCoverage(hdrCoverageObserved_.load());
                            if(!presenter.present(ctx,ring,*graph,item.lease->slot,generated,item.lease->referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView(),item.pts100ns))return {State::Failed};
                            const auto presentEndHost=host100ns();
                            const double returnDeviation=deviationValid?nowMs()-itemPtsMs:0;
                            item.lease->consumerFence=presenter.consumerFenceValue(ring.lastSignaledValue());const bool didPresent=presenter.submittedCount()>before;s.blit=presenter.blitTiming(ctx.fence());
                            const auto elapsed=elapsedMs(begin);s.presentMs+=elapsed;flow->cpu(diagnostics::CpuStage::Present,elapsed,host100ns());
                            // A soft history reset retains this metrics window. Older
                            // session/revision windows must not update its admission cost.
                            if(flow==frameFlow){
                                livePresent.add(elapsed);
                                // DXGI/driver blocking is backpressure, not GPU blit
                                // work. The bounded scheduler retains that pressure.
                            }
                            if(didPresent){
                                cadence.submitted(optionalNow);
                                cadence.rateSubmitted(optionalNow,capInterval);
                                if(veyra::log::verboseFrameLogs()&&testReplayAsCard)veyra::log::info("capture-present-sample",std::format("source={} epoch={} revision={} batch={} subframe={} generated={} arrival={} arrivalA={} ready={} begin={} end={}",item.identity.sourceFrameId,item.identity.epoch,item.identity.settingsRevision,batch.batch.batchId,item.subframe,generated,captureArrival,lineage?lineage->a.host100ns:0,watch->frameReadyObserved[s.next],optionalNow,host100ns()));
                                if(veyra::log::verboseFrameLogs())veyra::log::info("pacing-sample",std::format("mode={} enabled={} sync={} pts={} begin={} end={} ready={} process={} generated={} queue={} lateMs={:.3f} arrival={} deadlinePaced={} duringProcess={}",unsigned(presentationEffective.mode),presentationEffective.enabled,unsigned(presentationEffective.display),item.pts100ns,optionalNow,host100ns(),watch->frameReadyObserved[s.next],std::chrono::duration_cast<std::chrono::nanoseconds>(watch->processStart.time_since_epoch()).count()/100,generated,liveScheduler->occupancy(),isCapture?(timeline.paced()?double(optionalNow-timeline.deadline(item.pts100ns))/10000:0):fileAwaitingVideo?0:nowMs()-itemPtsMs,captureArrival,!isCapture||timeline.paced(),processingGraph));
                                diagnostics::FrameTraceEvent event{presentEndHost,runSessionId,item.identity,batch.batch.batchId,item.lease->consumerFence,item.pts100ns,diagnostics::TraceKind::Present,item.subframe,1,elapsed};
                                event.decodedHost=baselineReady;event.processHost=std::chrono::duration_cast<std::chrono::nanoseconds>(watch->processStart.time_since_epoch()).count()/100;
                                event.readyHost=watch->frameReadyObserved[s.next];event.presentBeginHost=presentBeginHost;event.presentEndHost=presentEndHost;
                                event.entryDeviationMs=entryDeviation;event.returnDeviationMs=returnDeviation;event.queueDepth=unsigned(liveScheduler->occupancy());event.mediaDeviationValid=deviationValid;
                                Logger::instance().recordFrame(event);
                                if(deviationValid){presentEntryDeviation.add(std::abs(entryDeviation));presentReturnDeviation.add(std::abs(returnDeviation));}
                                if(veyra::log::verboseFrameLogs())veyra::log::info("submit",std::format("batch={} epoch={} revision={} subframe={} pts100ns={} host100ns={} fence={} (submission, display unmeasured)",batch.batch.batchId,item.identity.epoch,item.identity.settingsRevision,item.subframe,item.pts100ns,host100ns(),item.lease->consumerFence));
                            }
                            // Cumulative alignment: feed whatever the providers reported
                            // since the last submission. The AMD provider publishes from
                            // its own thread, so a per-call delta can miss an update that
                            // lands just after the call and would then never be counted.
                            {
                                const auto xessPresentedTotal=presenter.xessPresentedCount(),xessGeneratedTotal=presenter.xessGeneratedCount();
                                const auto delta=xessSubmissions.observe(presenter.generation(),xessPresentedTotal,xessGeneratedTotal);
                                if(delta.restarted)veyra::log::info("provider-count-reset",std::format("generation={} presentedTotal={} generatedTotal={} new session baseline=0",presenter.generation(),xessPresentedTotal,xessGeneratedTotal));
                                if(delta.presented||delta.generated)flow->xessSubmitted(delta.presented,delta.generated,host100ns());

                            }
                            if(didPresent&&!isCapture){
                                lastFilePresentedMs=itemPtsMs;lastFilePresentLateness=returnDeviation;
                                {std::lock_guard lock(mutex_);if(snapshot_.sessionId==runSessionId&&snapshot_.applied.revision==item.identity.settingsRevision){
                                    // Split the wait so the log says where the time went:
                                    // drain+demux is the network re-position, the rest is
                                    // decoding from the keyframe up to the target plus the
                                    // first enhanced frame.
                                    if(activeSeekId&&snapshot_.seekPresented!=activeSeekId)veyra::log::info("seek-latency",std::format("request={} firstPresentMs={:.3f} decodedToTarget={} ptsMs={:.3f} afterDemuxMs={:.3f}",activeSeekId,elapsedMs(seekStarted),seekDecoded,itemPtsMs,elapsedMs(seekDemuxDone)));
                                    snapshot_.position=itemPtsMs/1000;snapshot_.lateMs=lastFilePresentLateness;snapshot_.seekPresented=activeSeekId;}}
                                double nextPts=itemPtsMs+sourceIntervalMs;
                                for(unsigned next=s.next+1;next<batch.batch.count;++next){const auto& candidate=batch.batch.frames[next];if(candidate.kind!=pipeline::FrameKind::Generated||(candidate.validity==pipeline::GenerationValidity::Valid&&comparisonMode_==0)){nextPts=double(candidate.pts100ns)/10000;break;}}
                                if(audioStarted){audioPipe.videoPresented(nextPts);audioPipe.setPaused(paused_);}
                                if(fileAwaitingVideo){
                                    veyra::log::info("audio-sync",std::format("file lookahead anchor ptsMs={:.3f} queued={} (continuous audio after bounded prefill)",itemPtsMs,liveScheduler->occupancy()));
                                    // Silent playback keeps one wall-clock origin between
                                    // transport changes; per-frame reanchoring hides lateness.
                                    anchor=Clock::now();anchorMs=itemPtsMs;
                                }
                                fileAwaitingVideo=false;seekPreviewPending=false;
                            }
                            // Real B owns captureArrival. Generated A/B frames and
                            // paused cached frames must not invent input anchors.
                            if(didPresent){
#ifdef VEYRA_ENABLE_MOONLIGHT
                                if(moon)moon->videoPresented(double(item.pts100ns)/10000,host100ns());
#endif
#ifdef VEYRA_ENABLE_XBOX
                                if(xb)xb->videoPresented(double(item.pts100ns)/10000,host100ns(),!generated&&!rereadCached);
#endif
                            }
                            if(didPresent&&!generated&&!rereadCached){
                                const auto returned=host100ns();
                                flow->latency(captureArrival,returned);
                                const double age=double(returned-captureArrival)/10000;
                            }
                            if(didPresent&&!generated&&!rereadCached&&!paused_){
                                const auto stamp=host100ns();const auto stats=flow->snapshot(stamp);
                                const auto color=stats.gpuTiming[size_t(diagnostics::GpuStage::Color)].mean;
                                const auto blit=stats.gpuTiming[size_t(diagnostics::GpuStage::Blit)].mean;
                                std::optional<double> basic;
                                if(color&&blit)basic=*color+*blit+elapsed;
                                const auto estimate=enhancementDelayEstimate(delayEnhanced,isCapture,
                                    isCapture?double(stamp-baselineReady)/10000:lastFilePresentLateness,basic);
                                if(estimate)flow->cpu(diagnostics::CpuStage::EnhancementDelayEstimate,*estimate,stamp);
                            }
                            if(didPresent&&generated&&lineage)flow->generatedLatency(*lineage,host100ns());
                            // Deadline margin: what this generated frame would have had without
                            // the extra. Only readiness seen by the deadline, or a fence this step
                            // saw unfinished (so its completion was seen as it happened); a frame
                            // first looked at after its deadline (the thread was busy) says
                            // nothing about the GPU. Warmup and recovery pairs are not steady.
                            if(didPresent&&generated&&marginPair&&flow==frameFlow&&timeline.paced()&&!batch.historyReset&&!batch.fgRecovery){
                                const auto deadline=timeline.deadline(item.pts100ns),ready=watch->frameReadyObserved[s.next];
                                if(ready>0&&(ready<=deadline||s.seenUnfinished[s.next]))deadlineMargin.observe(host100ns(),deadline,ready,pairExtra);
                            }
                            if(didPresent){++s.handled;++s.count;flow->presented(generated,item.lease->consumerFence,host100ns(),presenter.lastPresentCallHost100ns());if(!generated)++presentationCompletedReal;s.ageMs=double(host100ns()-captureArrival)/10000;
                                if(flow==frameFlow){const auto time=host100ns();liveSubmissions.push_back(time);while(liveSubmissions.size()>1&&time-liveSubmissions.front()>10000000)liveSubmissions.pop_front();}}
                            ++s.next;
                        }
                        return {State::Complete};
                    },[&,watch,step,flow=frameFlow]{
                        const auto& batch=watch->output;const auto& s=*step;
                        flow->update([&](auto& m){m.pendingOutputFrames-=std::min(m.pendingOutputFrames,s.remaining);});
                        if(s.handled<batch.batch.count)++presentationCancelledJobs;
                        if(s.handled<batch.batch.count)traceFrame(diagnostics::TraceKind::Cancelled,batch.batch.identity,batch.batch.batchId,0,batch.batch.b100ns,0,batch.batch.count-s.handled,0);
                        flow->update([&](auto& m){m.counters.cancelledBeforePresent+=batch.batch.count-s.handled;m.gpuReadyWaitMs=s.readyMs;m.deadlineWaitMs=s.waitMs;if(s.count)m.captureArrivalToPresentReturnMs=s.ageMs;m.counters.generatedExpiredAfterEval+=s.dropped;});
                        if(flow!=frameFlow)return;
                        liveStats.submitted+=s.count;liveStats.expired+=s.dropped;
                        if(s.count){liveStats.waitMs=s.waitMs;liveStats.presentMs=s.presentMs;liveStats.ageMs=s.ageMs;liveStats.blit=s.blit;
                            liveAges.add(s.ageMs);liveWaits.add(s.waitMs);liveStats.ageP95=liveAges.p95();liveStats.waitP95=liveWaits.p95();liveStats.presentP95=livePresent.p95();
                            // An early (B1) present learns its GPU-ready time in the completion poll.
                            if(!watch->presentedBeforeReady){liveStats.readyMs=s.readyMs;liveReady.add(s.readyMs);liveStats.readyP95=liveReady.p95();}}
                        liveStats.fps=liveSubmissions.size()>1?double(liveSubmissions.size()-1)*1e7/(liveSubmissions.back()-liveSubmissions.front()):0;
                    })){status(L"Video presentation queue failed",true);break;}
                    frameFlow->update([&](auto& m){m.pendingOutputFrames+=step->remaining;});
                    advanceLive();
                    const auto occupancy=liveScheduler->occupancy();frameFlow->update([&](auto& m){m.counters.presentationBatchHighWater=std::max(m.counters.presentationBatchHighWater,occupancy);});
                    const auto completed=liveStats;
                    frameWaitMs=completed.waitMs;framePresentMs=completed.presentMs;gpuWaitMs=completed.readyMs;submitted=completed.submitted;expired=completed.expired;
                }else{
                    // Images have no media clock, audio or temporal batches.
                    const auto readyStart=Clock::now();
                    while(!stop_&&!graph->resolveGeneration(out)){
                        if(elapsedMs(readyStart)>2000){status(L"Image GPU ready timed out",true);stop_=true;break;}
                        deadlineWait.slice(.2);
                    }
                    if(stop_)break;
                    gpuWaitMs=elapsedMs(readyStart);gpuReadyTimes.add(gpuWaitMs);
                    frameFlow->cpu(diagnostics::CpuStage::ReadyWait,gpuWaitMs,host100ns());
                    frameFlow->ready(out.batch.batchId,false,0,0,host100ns());completeReset(out.batch.identity);
                    bool presentFailed=false;
                    for(auto& item:out.batch.frames){
                        if(!item.lease)continue;
                        const auto begin=Clock::now();const auto before=presenter.submittedCount();
                        if(!presenter.present(ctx,ring,*graph,item.lease->slot,false,item.lease->referencesValid,comparisonMode_,comparisonBase_,comparisonSplit_,item.identity,previewView(),item.pts100ns)){presentFailed=true;break;}
                        item.lease->consumerFence=presenter.consumerFenceValue(ring.lastSignaledValue());framePresentMs+=elapsedMs(begin);
                        frameFlow->cpu(diagnostics::CpuStage::Present,elapsedMs(begin),host100ns());
                        if(presenter.submittedCount()>before){++submitted;frameFlow->presented(false,item.lease->consumerFence,host100ns());}
                    }
                    if(presentFailed){status(L"Image presentation failed",true);break;}
                }
                if(!liveScheduler){scheduleWaits.add(frameWaitMs);presentTimes.add(framePresentMs);}
                loopTrace.mark("scheduleAndPresent");
                audioRebuffering=audioStarted&&audioPipe.waitingForVideo();
                const double lateness=isCapture?0:isImage?nowMs()-pts:lastFilePresentLateness;
                // Sample only actual presentations above, not each engine iteration.
                auto captureStats=source::CaptureMetrics{};
#ifdef VEYRA_ENABLE_XBOX
                if(xb){captureStats.received=xb->unitsReceived();captureStats.dropped=xb->skipped();captureStats.delivered=sourceFrames;
                    captureStats.readAgeMs=double(std::max<int64_t>(0,host100ns()-pkt.arrivalHost100ns))/10000;
                }
#endif
#ifdef VEYRA_ENABLE_MOONLIGHT
                if(moon){captureStats.received=moon->unitsReceived();captureStats.dropped=moon->skipped();captureStats.delivered=sourceFrames;
                    captureStats.readAgeMs=double(std::max<int64_t>(0,host100ns()-pkt.arrivalHost100ns))/10000;
                }
#endif
                loopTrace.mark("captureStats");
                diagnostics::FrameMetrics measured;pipeline::EnhanceGraph::Metrics graphStats;uint64_t slotWaitCount=0,commandSubmits=0;double slotWaitMilliseconds=0;uint32_t slotsInFlight=0;
                {measured=graph->gpuMetrics();graphStats=graph->metrics();measured.gpu[size_t(diagnostics::GpuStage::Blit)]=presenter.blitTiming(ctx.fence(),options.settings.revision,out.batch.identity.epoch);
                 // Present-sink FG backends record their application-side work on
                 // the presenter's list; surface that sample instead of leaving
                 // the stage unmeasured. The in-graph DLSS FG keeps its own.
                 if(graph->xessEnabled())measured.gpu[size_t(diagnostics::GpuStage::FgBatch)]=presenter.fgTiming(ctx.fence(),options.settings.revision,out.batch.identity.epoch);
                 slotWaitCount=ring.cpuWaitCount()-slotWaitBase;slotWaitMilliseconds=ring.cpuWaitMilliseconds()-slotWaitMsBase;commandSubmits=ring.submitCount()-submitBase;slotsInFlight=ring.inFlightCount();
                    loopTrace.mark("gpuStats");
                    collectTimings();
                    loopTrace.mark("tailCollectTimings");
                }
                if(measured.identity.settingsRevision!=options.settings.revision||measured.identity.epoch!=out.batch.identity.epoch){measured={};measured.identity=out.batch.identity;for(auto& sample:measured.gpu)sample.state=diagnostics::SampleState::Pending;}
                if(graph->xessEnabled()){
                    graphStats.fgGeneratedFrames=presenter.xessGeneratedCount();
                }


                for(size_t stage=0;stage<measured.gpu.size();++stage){const auto& sample=measured.gpu[stage];if(sample.state==diagnostics::SampleState::Measured&&sample.milliseconds&&sample.end&&sample.end!=lastGpuSampleEnd[stage]){gpuStageTimes[stage].add(*sample.milliseconds);lastGpuSampleEnd[stage]=sample.end;}}
                measured.resolution=pipeline::ResolutionPlan::make({width,height},options.sr,options.snapshot().nrPolicy,isImage,options.settings.revision,options.settings.srTarget,options.settings.lowLatency&&options.nr,0,runningSrPrescale(options.settings,ctx.adapter().isNvidia,isImage),runningSrOutput(options.settings,ctx.adapter().isNvidia),dualSrRuns(options.settings,ctx.adapter().isNvidia,isImage),options.settings.dualSrTarget);measured.decodeCpuMs=decodeMs;measured.submitCpuMs=std::chrono::duration<double,std::milli>(processDone-processStart).count();measured.gpuWaitCpuMs=gpuWaitMs;measured.deadlineWaitCpuMs=frameWaitMs;measured.presentCpuMs=framePresentMs;measured.queueWatermark=out.batch.count;
                LiveStats completed;if(liveScheduler)completed=liveStats;
                uint64_t skipped=0;{std::lock_guard lock(mutex_);skipped=snapshot_.captureRateSkipped-rateSkippedBase;}
                frameFlow->update([&](auto& m){m.counters.captureReceived=captureStats.received-captureFlowBase.received;m.counters.mailboxOverwritten=captureStats.dropped-captureFlowBase.dropped;m.counters.sourceSkippedBeforeGraph=skipped;m.slotReuseWaitCount=slotWaitCount;m.slotReuseWaitMs=slotWaitMilliseconds;m.counters.commandSlotsInFlight=slotsInFlight;m.counters.commandSlotHighWater=std::max(m.counters.commandSlotHighWater,slotsInFlight);});
                frameFlow->update([&](auto& m){m.slotWaitPerFrameMs=processSlotWaitMs;});
                captureFlowLast=captureStats;
                loopTrace.mark("flowUpdate");
                measured.flow=frameFlow->snapshot(host100ns());
                loopTrace.mark("flowSnapshot");
                measured.sourceFrames=measured.flow.counters.sourceAccepted;measured.validGenerated=measured.flow.counters.fgReadyValid;measured.submitted=measured.flow.counters.realPresented+measured.flow.counters.generatedPresented;measured.expired=measured.flow.counters.generatedExpiredAfterEval;
                const double ageP95=liveScheduler?completed.ageP95:captureAges.p95(),waitP95=liveScheduler?completed.waitP95:scheduleWaits.p95(),presentP95=liveScheduler?completed.presentP95:presentTimes.p95();
                // Refresh and VRR state (DisplayStateService, queried on events
                // only) are polled outside the snapshot lock; this never waits.
                presenter.displayRefreshHz();
                ++frames;{std::lock_guard lock(mutex_);snapshot_.metrics=measured;
                    // Custom (format label): one name for what the source carries,
                    // used by the player bar and the start-up badge. Dolby Vision
                    // first, then the dynamic-metadata HDR formats, then the static
                    // ones. Identification only: every case maps on the static curve.
                    const auto sourceHdrLabel = [&]() -> std::wstring {
                        if (isCapture || isImage || activeSource == nullptr) return {};
                        const auto& si = activeSource->info();
                        if (si.dolbyVision.present) {
                            if (si.dolbyVision.profile == 5) return L"Dolby Vision P5";
                            if (si.dolbyVision.profile == 7) return L"Dolby Vision P7";
                            // Profile 8 carries its base layer's signal in the
                            // compatibility id: 8.1 PQ, 8.2 SDR, 8.4 HLG.
                            if (si.dolbyVision.profile == 8) {
                                if (si.dolbyVision.compatibility == 4) return L"Dolby Vision P8.4";
                                if (si.dolbyVision.compatibility == 2) return L"Dolby Vision P8.2";
                                if (si.dolbyVision.compatibility == 1) return L"Dolby Vision P8.1";
                                return L"Dolby Vision P8";
                            }
                            return L"Dolby Vision";
                        }
                        if (si.hdrVivid.present) return L"HDR Vivid";
                        if (si.hdr10Plus.present) return L"HDR10+";
                        if (si.color.transfer == pipeline::TransferFunction::PQ) return L"HDR10";
                        if (si.color.transfer == pipeline::TransferFunction::HLG) return L"HLG";
                        return L"SDR";
                    }();
                    const std::wstring hdrSource = (gd.hdrInput && !sourceHdrLabel.empty()) ? sourceHdrLabel : L"HDR";
                    snapshot_.sourceHdrLabel=sourceHdrLabel;
                    snapshot_.colorStatus=graph->videoHdrActive()?L"SDR → RTX Video HDR":options.settings.videoHdr.enabled&&!gd.hdrInput?L"SDR → SDR (HDR display off)":gd.hdrOutput?(graph->hdr10Output()?hdrSource+L" → HDR10 / PQ":hdrSource+L" → scRGB / floating point"):gd.hdrInput?hdrSource+L" → SDR tone mapping":L"SDR → SDR";snapshot_.position=(isCapture||isImage?pts:lastFilePresentedMs)/1000;snapshot_.frames=sourceFrames;snapshot_.generated=graphStats.fgGeneratedFrames;snapshot_.lateMs=lateness;snapshot_.lateP95Ms=presentReturnDeviation.p95();
                    snapshot_.videoHdrActive=graph->videoHdrActive();
                    snapshot_.beautyActive=graph->beautyNodesActive();snapshot_.beautyFaces=graph->beautyFaceCount();
                    snapshot_.beautyNotice=graph->beautyNotice();
                    snapshot_.videoHdrStatus=graph->videoHdrActive()?L"Preview: SDR to HDR is running":
                        !options.settings.videoHdr.enabled?L"RTX Video HDR is off":
                        gd.hdrInput?L"Native HDR input; no SDR-to-HDR needed":
                        L"SDR preview at the moment; HDR conversion is not running";
                    // Measured playback speed: media-PTS advance per wall time
                    // over ~1s windows (1.0 = normal speed), resampled on seek.
                    // A rejected LUT input space must be visible, not only logged
                    // (plan v5.2: never apply a mismatched LUT silently).
                    if(!graph->colorLutNotice().empty())snapshot_.colorStatus+=L"; "+graph->colorLutNotice();
                    if(!graph->antiAliasNotice().empty())snapshot_.colorStatus+=L"; "+graph->antiAliasNotice();
                    const auto speedNow=Clock::now();
                    if(playbackSpeedLastWall==Clock::time_point()||pts+0.5<playbackSpeedLastPts){playbackSpeedLastPts=pts;playbackSpeedLastWall=speedNow;}
                    else if(std::chrono::duration<double>(speedNow-playbackSpeedLastWall).count()>=0.9){
                        const double wallS=std::chrono::duration<double>(speedNow-playbackSpeedLastWall).count();
                        const double mediaS=(pts-playbackSpeedLastPts)/1000.0;
                        if(mediaS>=0)snapshot_.playbackSpeed=mediaS/wallS;
                        playbackSpeedLastPts=pts;playbackSpeedLastWall=speedNow;
                    }
                    snapshot_.previewSkipped=measured.flow.counters.previewSkippedBeforeGraph;
                    snapshot_.fgBudgetLimited=measured.flow.lastFgRejected100ns>0&&host100ns()-measured.flow.lastFgRejected100ns<10000000;
                    snapshot_.xessGenerationSuppressed=graph->xessEnabled()&&presenter.xessGenerationSuppressed();
                    // Reuse the existing snapshot lock, rather than locking once
                    // more for every generated Present.
                    const auto& coverage=presenter.liveCurveCoverage();
                    snapshot_.hdrCoverageValid=coverage.valid;
                    snapshot_.hdrAboveCeilingPercent=coverage.aboveCeilingPercent();
                    snapshot_.hdrInRollOffPercent=coverage.inRollOffPercent();
                    snapshot_.hdrInLiftPercent=coverage.inLiftPercent();
                    snapshot_.displayVrr=gfx::vrrLevel(presenter.displayVrr());snapshot_.presentationVrrSync=presenter.vrrSync();
                    snapshot_.nrActive=graph->nrEnabled()&&graphStats.nrEvaluateCount>0;
                    snapshot_.nrAutoActive=graph->autoNrPoolReady();
                    snapshot_.nrAutoPercent=graph->autoNrPoolReady()?AutoNrController::percent(graph->autoNrLevel()):0;
                    const auto autoExtent=graph->autoNrExtent();snapshot_.nrAutoWidth=autoExtent.width;snapshot_.nrAutoHeight=autoExtent.height;
                    snapshot_.nrAutoStatus=nrAutoStatus;
                    snapshot_.nrReuseActive=graph->nrReuseActive();snapshot_.nrReuseAutomatic=graph->nrReuseAutomatic();
                    snapshot_.nrReuseLevel=graph->nrReuseActive()?graph->nrReuseLevel():0;
                    snapshot_.nrReuseFrames=graphStats.nrReuseFrames;snapshot_.nrReuseUnavailable=graph->nrReuseUnavailable();
                    snapshot_.audioRebuffering=audioRebuffering;
                    snapshot_.audioVideoWaits=audioPipe.videoWaitCount();
                    snapshot_.srActive=gd.enableSr&&graphStats.srEvaluateCount>0;
                    snapshot_.fgActive=options.fg&&(graph->xessEnabled()?presenter.xessActive()&&graphStats.fgGeneratedFrames>0:graph->fgEnabled()&&measured.flow.counters.fgReadyValid>0);
                    if(transaction&&backendRecoveryWarning.empty()&&nvidiaAdapter&&(!graph->xessEnabled()||presenter.xessActive()))snapshot_.backendWarning.clear();
                    snapshot_.flowPerf=graph->actualFlowPerf();snapshot_.contentFps=isCapture&&!legacyCadence&&dedupContent(options.settings.content)&&contentDedup.contentFps()>0?int(std::lround(contentDedup.contentFps())):out.measuredContentRate;
                    snapshot_.nrEvaluated=graphStats.nrEvaluateCount;snapshot_.nvofExecuted=graphStats.nvofExecuteCount;
                    snapshot_.captureReceived=captureStats.received;snapshot_.captureDropped=captureStats.dropped;snapshot_.captureFps=captureStats.callbackFps;snapshot_.captureReadAgeMs=captureStats.readAgeMs;snapshot_.captureAgeMs=liveScheduler?completed.ageMs:captureStats.frameAgeMs;snapshot_.captureAgeP95Ms=ageP95;
                    snapshot_.captureSampledBlack=captureStats.sampledBlack;
                    snapshot_.schedulingWaitP95Ms=waitP95;snapshot_.processCpuP95Ms=processTimes.p95();snapshot_.presentCpuP95Ms=presentP95;
                }
                loopTrace.mark("publishSnapshot");
                const auto gpuP95=[&](diagnostics::GpuStage stage){return gpuStageTimes[size_t(stage)].p95();};
                if(veyra::log::verboseFrameLogs()&&Clock::now()>=nextTimingLog){const auto [retained,overwritten]=Logger::instance().frameTraceSize();
                    veyra::log::info("frame-trace",std::format("retained={} capacity=8192 overwritten={} (records available in diagnostic preview)",retained,overwritten));}
                if(options.settings.multiplier>1&&Clock::now()>=nextTimingLog)
                    veyra::log::info("fg-validity",std::format("revision={} candidate={} valid={} invalid={} providerDisabled={} contentPolicy={} sourceTransportFps={} backend={} (invalid = disabled by the provider; no automatic content-rate/multiplier rewrite)",options.settings.revision,graphStats.fgSubmittedCandidates,graphStats.fgGeneratedFrames,graphStats.fgDisabledFrames,graphStats.fgProviderDisabled,int(options.settings.content),activeSource->info().averageFps,frameGenerationBackendName(options.settings.frameGenerationBackend)));
                if(!isCapture&&Clock::now()>=nextTimingLog){const double playbackSpeedNow=[&]{std::lock_guard lock(mutex_);return snapshot_.playbackSpeed;}();
                // present-deviation / output-queue are folded into the
                // player-timing line below (sweep E5); the separate lines stay
                // available under VEYRA_VERBOSE_FRAME_LOGS for old tooling.
                const auto dashboard=frameFlow->snapshot(host100ns());
                if(veyra::log::verboseFrameLogs()){
                veyra::log::info("present-deviation",std::format("samples={} entryAbsP95Ms={:.3f} returnAbsP95Ms={:.3f} (one sample per actual submission; media-clock deviation, not scanout latency)",presentReturnDeviation.size(),presentEntryDeviation.p95(),presentReturnDeviation.p95()));
                veyra::log::info("output-queue",std::format("pendingFrames={} enhancementProcessingMs={:.3f} extraDelayEstimateMs={:.3f}",dashboard.pendingOutputFrames,dashboard.enhancementProcessing.mean.value_or(-1),dashboard.cpuTiming[size_t(diagnostics::CpuStage::EnhancementDelayEstimate)].mean.value_or(-1)));}
                uint64_t vramBudget=0,vramUsage=0;ctx.videoMemoryInfo(vramBudget,vramUsage);watchVram(vramUsage,vramBudget);
                veyra::log::info("player-timing",std::format("vramMiB={} budgetMiB={} revision={} decodeP95Ms={:.3f} graphSubmitP95Ms={:.3f} gpuReadyP95Ms={:.3f} presentP95Ms={:.3f} gpuColorP95Ms={:.3f} gpuSrP95Ms={:.3f} gpuFlowP95Ms={:.3f} gpuNrP95Ms={:.3f} gpuResidualP95Ms={:.3f} gpuFgBatchP95Ms={:.3f} gpuBlitP95Ms={:.3f} slotWaits={} slotWaitMs={:.3f} commandSubmits={} displaySubmits={} expiredGenerated={} previewSkipped={} playbackSpeed={:.3f} processed={} entryAbsP95Ms={:.3f} returnAbsP95Ms={:.3f} pendingFrames={} enhancementProcessingMs={:.3f}",vramUsage>>20,vramBudget>>20,options.settings.revision,decodeTimes.p95(),processTimes.p95(),liveScheduler?completed.readyP95:gpuReadyTimes.p95(),presentP95,gpuP95(diagnostics::GpuStage::Color),gpuP95(diagnostics::GpuStage::Sr),gpuP95(diagnostics::GpuStage::Flow),gpuP95(diagnostics::GpuStage::Nr),gpuP95(diagnostics::GpuStage::Residual),gpuP95(diagnostics::GpuStage::FgBatch),gpuP95(diagnostics::GpuStage::Blit),slotWaitCount,slotWaitMilliseconds,commandSubmits,submitted,expired,measured.flow.counters.previewSkippedBeforeGraph,playbackSpeedNow,sourceFrames-statsSourceBase,presentEntryDeviation.p95(),presentReturnDeviation.p95(),dashboard.pendingOutputFrames,dashboard.enhancementProcessing.mean.value_or(-1)));nextTimingLog=Clock::now()+std::chrono::seconds(1);}
                if(isCapture&&Clock::now()>=nextTimingLog){
                    logFrameFlow(measured.flow,"active");
                    const auto rates=snapshot();
                    // Adapter memory every second: a long session that slowly runs out of video
                    // memory gets slower in every pass at once (field log 2026-10-01).
                    uint64_t vramBudget=0,vramUsage=0;ctx.videoMemoryInfo(vramBudget,vramUsage);watchVram(vramUsage,vramBudget);
                    veyra::log::info("frame-rate",std::format("revision={} gpuCompletedFps={:.2f} completedReal={} capture60To30={} rateSkipped={} presentSubmitFps={:.2f} windowMs=1000 vramMiB={} budgetMiB={} (not scanout FPS)",options.settings.revision,rates.fps,rates.processedCompleted,rates.captureHalfRate,rates.captureRateSkipped,rates.submissionFps.value_or(0),vramUsage>>20,vramBudget>>20));
                    if(isCapture&&dedupContent(options.settings.content)&&!legacyCadence){
                        const auto& st=contentDedup.stats();
                        veyra::log::info("content-dedup",std::format("revision={} mode={} observed={} kept={} repeats={} held={} blind={} regular={} irregular={} contentFps={:.2f} maxShiftMs={:.2f} repeatMaxDiff={} repeatMaxOver={} newMinOver={} band={} (per second; a repeat has at most about 5 per million components of the whole picture beyond the band of the last new one, dropped before GPU work; kept frames carry content timestamps)",
                            options.settings.revision,int(options.settings.content),st.observed-dedupLogged.observed,st.kept-dedupLogged.kept,st.repeats-dedupLogged.repeats,st.held-dedupLogged.held,st.blind-dedupLogged.blind,
                            st.regular-dedupLogged.regular,st.irregular-dedupLogged.irregular,contentDedup.contentFps(),st.maxShiftMs,dedupRepeatMax8,dedupRepeatMaxOver,dedupNewMinOver==UINT64_MAX?0:dedupNewMinOver,pipeline::PictureReference::kBand8));
                        dedupLogged=st;dedupRepeatMax8=0;dedupRepeatMaxOver=0;dedupNewMinOver=UINT64_MAX;
                    }
                    veyra::log::info("capture-timing",std::format("revision={} received={} processed={} dropped={} callbackFps={:.2f} readAgeMs={:.3f} callbackToPresentReturnP95Ms={:.3f} processCpuP95Ms={:.3f} gpuReadyP95Ms={:.3f} schedulingWaitP95Ms={:.3f} presentCpuP95Ms={:.3f} gpuColorP95Ms={:.3f} gpuSrP95Ms={:.3f} gpuFlowP95Ms={:.3f} gpuNrP95Ms={:.3f} gpuResidualP95Ms={:.3f} gpuFgBatchP95Ms={:.3f} gpuBlitP95Ms={:.3f} slotWaits={} slotWaitMs={:.3f} commandSubmits={} displaySubmits={} expiredGenerated={} nr={} nvof={} generated={} historyResets={} presentationDrains={} presentationCompletedReal={} presentationSkippedGenerated={} presentationCancelledJobs={} singleGpuOwner=1 batchCapacity=2 (not HDMI-to-display latency)",options.settings.revision,captureStats.received,sourceFrames-statsSourceBase,captureStats.dropped,captureStats.callbackFps,captureStats.readAgeMs,ageP95,processTimes.p95(),liveScheduler?completed.readyP95:gpuReadyTimes.p95(),waitP95,presentP95,gpuP95(diagnostics::GpuStage::Color),gpuP95(diagnostics::GpuStage::Sr),gpuP95(diagnostics::GpuStage::Flow),gpuP95(diagnostics::GpuStage::Nr),gpuP95(diagnostics::GpuStage::Residual),gpuP95(diagnostics::GpuStage::FgBatch),gpuP95(diagnostics::GpuStage::Blit),slotWaitCount,slotWaitMilliseconds,commandSubmits,submitted,expired,graphStats.nrEvaluateCount,graphStats.nvofExecuteCount,graphStats.fgGeneratedFrames,historyResets.load(),presentationDrains.load(),presentationCompletedReal.load(),presentationSkippedGenerated.load(),presentationCancelledJobs.load()));
                    nextTimingLog=Clock::now()+std::chrono::seconds(1);
                }
                loopTrace.mark("periodicLogs");
            }
        }while(false);
    }catch(const std::exception& e){
        veyra::log::error("engine",std::format("{} (engine run loop)",e.what()));
        status(L"Engine error; see diagnostics",true);failed=true;}
    (void)failed;
#ifdef VEYRA_ENABLE_MOONLIGHT
    {std::lock_guard lock(mutex_);activeMoonlight_.reset();}
    if(moon)moon->close();
#endif
#ifdef VEYRA_ENABLE_XBOX
    {std::lock_guard lock(mutex_);activeXbox_.reset();}
    if(xb)xb->close();
#endif
    // Each step is logged: a session that stopped responding after a stream
    // failure left "graph shutdown complete" as its last line.
    audioPipe.stopThread();audio.shutdown();veyra::log::info("engine","teardown: audio stopped");
    ring.drainQueue();presenter.close();graph->shutdown();recentGraphCache.evict("session-close");
    (void)coreCache.close();
    veyra::log::info("engine","teardown: closing sources");
    source.close();av_frame_free(&cachedFrame);av_frame_free(&imageFrame);
    veyra::log::info("engine","teardown: sources closed");
    if(gpuFailureAwaitingRemoval){
        // Usually the drain above already waited for the reset; allow it a little longer.
        uint32_t removed=0;bool alive=ctx.checkDeviceAlive(removed);
        for(int i=0;alive&&i<20;++i){Sleep(100);alive=ctx.checkDeviceAlive(removed);}
        if(!alive)ctx.reportDeviceFailure("live-presentation-failed-at-teardown");
        else veyra::log::warn("gpu-failure","device still not removed at teardown; no DRED report available");
    }
    gpu->shutdown();
    {std::lock_guard lock(mutex_);snapshot_.running=false;snapshot_.audioEndpointRecovering=false;snapshot_.audioRebuffering=false;}
    CoUninitialize();
}
}
