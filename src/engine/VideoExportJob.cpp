#include "veyra/engine/VideoExportJob.h"
#include "veyra/engine/ExportStreams.h"
#include "veyra/engine/ExportTimeline.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/sink/VideoEncoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/GraphDescription.h"
#include <filesystem>
#include <algorithm>
#include <format>
#include <thread>
#include <chrono>
#include <cmath>
#include <deque>
#include <cstring>
#include <vector>
#include <wrl/client.h>
#include <d3d12sdklayers.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavutil/mastering_display_metadata.h>
}
namespace veyra::engine {
namespace { std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;} }
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions options,bool hevc,std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,unsigned maxFrames,const std::function<bool()>& frameBoundary,const std::function<void(const ExportCounts&)>& counts,ExportResume* resume,const ExportSegments& priorSegments,unsigned resumeAttempt){
    if(resume)*resume={};
    if(!std::isfinite(options.exportStartSeconds)||!std::isfinite(options.exportEndSeconds)||options.exportStartSeconds<0||options.exportEndSeconds<0||(options.exportEndSeconds>0&&options.exportEndSeconds<=options.exportStartSeconds)){progress(0,L"Invalid export trim range");return false;}
    if(std::filesystem::exists(output)){progress(0,L"The target file already exists; use another name");return false;}
    if(cancel){progress(0,L"Export cancelled");return false;}
    std::wstring fgNote;
    if(options.fg&&presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
        progress(0,L"XeSS does not expose generated textures for encoding. Select FSR, DLSS or VFG for export.");
        return false;
    }
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;pipeline::EnhanceGraph graph(ctx,ring);
    struct CompletionEvent {HANDLE value=nullptr;~CompletionEvent(){if(value)CloseHandle(value);}} completionEvent;
    std::unique_ptr<sink::VideoEncoder> encoder;
    AVFormatContext* mux=nullptr;AVStream* videoStream=nullptr;ExportStreams streams;
    bool ok=false,headerWritten=false;int64_t written=0;double audioEndSeconds=0,videoOriginSeconds=0;
    uint64_t repairedTimestamps=0;
    std::wstring encoderName;
    std::wstring failureReason;
    auto failAv=[&](const wchar_t* stage,int code){
        char error[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,error,sizeof(error));
        failureReason=std::format(L"{} failed (error {})",stage,code);
        veyra::log::error("export",std::format("stage={} code={} detail={}",utf8(stage),code,error));
        return false;
    };
    const auto partial=options.exportTemporaryPath.empty()?reserveExportTemporaryFile(output,failureReason):options.exportTemporaryPath;
    if(partial.empty()){progress(0,failureReason);return false;}
    try { do {
        Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
        dd.enableDebugLayer=GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_DEBUG",nullptr,0)>0;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))break;
        // Export runs on every adapter now: NVIDIA uses NVENC (D3D12,
        // zero-copy) and everything else uses the driver's Media Foundation
        // hardware encoder. Features that need NGX/NVOF stay NVIDIA-only and
        // are gated below instead of failing the job.
        const bool nvidiaAdapter=ctx.adapter().isNvidia;
        if(!nvidiaAdapter)veyra::log::info("export",std::format("adapter={} vendor={}; selected AMD NR/FSR runtimes are checked independently of NVIDIA features; encoder uses the system Media Foundation hardware MFT",utf8(ctx.adapter().description),ctx.adapter().vendorIdHex));
        source::SourceOpenDesc od;od.path=input;od.preferHardwareDecode=false;if(!source.open(od)){failureReason=source.errorMessage();break;}
        auto info=source.info();if(!pipeline::Extent{info.width,info.height}.valid()){progress(0,L"The input is larger than one GPU texture allows");break;}
        // The first decoded frame is authoritative when container headers omit
        // transfer/range metadata. Retain it instead of scanning and reopening.
        const double requestedStart=options.exportStartSeconds;
        const double requestedEnd=options.exportEndSeconds;
        if(requestedStart>0&& !source.seek(pipeline::Rational{static_cast<int64_t>(std::llround(requestedStart*1000000.0)),1000000})){failureReason=L"Export could not seek to the clip's in point";break;}
        pipeline::FramePacket firstPacket;const AVFrame* firstFrame=nullptr;bool firstReady=false;
        while(!firstReady&&!cancel){
            const auto firstStatus=source.read(firstPacket,&firstFrame);
            if(firstStatus!=source::SourceReadStatus::Frame||!firstFrame){failureReason=firstStatus==source::SourceReadStatus::Eos?L"The clip's in point is past the end of the source video":L"Export could not read the first frame ahead";break;}
            const double firstPts=firstPacket.pts.toDouble();
            firstReady=requestedStart<=0||firstPacket.pts.isUnknown()||!std::isfinite(firstPts)||firstPts>=requestedStart;
            if(!firstReady)firstFrame=nullptr;
        }
        if(!firstReady||cancel)break;
        info=source.info(); // retain this frame for the export, without decoding it again
        int rateNum=info.nominalRateNum,rateDen=info.nominalRateDen;
        if(rateNum<=0||rateDen<=0){rateNum=30;rateDen=1;veyra::log::warn("export-timeline","missing nominal rate; encoder configured at 30 fps, source timestamps retained");}
        const double sourceInterval=double(rateDen)/rateNum;
        pipeline::EnhanceGraphDesc gd;gd.hdrInput=gd.hdrOutput=info.color.isHdrPath();
        gd.videoHdr=options.settings.videoHdr;
        gd.videoHdr.enabled=gd.videoHdr.enabled&&nvidiaAdapter;
        gd.hdrOutput=gd.hdrInput||gd.videoHdr.enabled;
        // Plan v5.3: the grade changes the peak and the content light distribution.
        // Recomputing MaxCLL/MaxFALL needs a full pre-pass before the header is
        // written, which this exporter does not do yet, so say so instead of
        // letting the user assume the static metadata tracks the graded output.
        if(gd.hdrInput&&gd.hdrOutput&&options.settings.color.enabled&&!options.settings.color.neutral())
            veyra::log::warn("color-export","HDR export with the colour grade active: MaxCLL/MaxFALL are carried over from the source and NOT recomputed for the graded output (marked as not updated)");
        gd.captureBitDepth=info.color.pixelFormat==pipeline::SourcePixelFormat::P010?10:info.color.pixelFormat==pipeline::SourcePixelFormat::P016?16:8;
        // Adapter gate mirrors the preview rules (EngineController): DLSS NR,
        // DLSS SR and the NVOF guidance need an NVIDIA device; AMD FSR
        // upscaling is the vendor-neutral video SR. NR/FG requests must keep
        // their selected backend and fail clearly when that backend is unavailable.
        const bool nvidiaFeatures=nvidiaAdapter;
        const bool amdAdapter=ctx.adapter().vendorId==0x1002;
        const bool amdNrExport=amdAdapter&&currentNrRuntime(options.settings.nrRuntime)==NrRuntime::LmxxfAmd;
        if(options.nr&&!nvidiaFeatures&&!amdNrExport){failureReason=L"This device cannot run the selected NR. Select an NR runtime supported by this GPU.";break;}
        // One description for the whole job: the stage rules (which SR runs,
        // whether NR/FG are available) come from the same place the preview uses.
        StageRequest stages;stages.nr=options.nr;stages.sr=options.sr;stages.fg=options.fg;stages.fgMultiplier=options.fgMultiplier;
        stages.width=info.width;stages.height=info.height;stages.exportJob=true;stages.nvidiaAdapter=nvidiaAdapter;stages.amdNr=amdNrExport;
        const auto plan=describeStages(stages,options.snapshot(),gd);
        // Export keeps the standard DLSS-G model: quality first, no latency to save.
        gd.dlssgLite=false;
        if(!nvidiaFeatures&&gd.opticalFlowBackend==OpticalFlowBackend::Nvidia)gd.opticalFlowBackend=OpticalFlowBackend::AmdFidelityFx;
        const auto resolution=plan;
        // Availability comes from the shared stage contract: native-size RTX
        // Video SR restores video even though ResolutionPlan has no enlargement.
        const bool srAvailable=gd.enableSr;
        if(options.nr&&amdNrExport){
            fgNote=L"AMD NR uses internal processing up to 1080p and composites and exports at the original output size.";
            veyra::log::info("export-nr",std::format("runtime=lmxxf source={}x{} output={}x{} internal={}x{} layers={}",info.width,info.height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.nrLayersExtent.size()));
        }else if(options.nr&&gd.enableNr){
            // NR follows the preview size preset; its residual is composed at output size.
            veyra::log::info("export-nr",std::format("source={}x{} output={}x{} internal={}x{} layers={}",info.width,info.height,gd.workWidth,gd.workHeight,gd.nrWidth,gd.nrHeight,gd.nrLayersExtent.size()));
        }
        if(options.nr&&gd.fixedExecutionPlan)for(uint32_t i=0;i<gd.fixedExecutionPlan->stepCount;++i){
            const auto& step=gd.fixedExecutionPlan->steps[i];
            if(step.type!=EffectType::NrEnhance)continue;
            veyra::log::info("export-nr-layer",std::format("parameter={} resource={} input={}x{} internal={}x{} output={}x{}",
                step.parameterIndex,step.resourceIndex,step.input.width,step.input.height,
                step.processing.width,step.processing.height,step.output.width,step.output.height));
        }
        if(options.sr&&!srAvailable)fgNote+=fgNote.empty()?L"This GPU cannot use the selected super resolution; it is off for this export":L"; this GPU cannot use the selected super resolution, it is off for this export";
        if(!nvidiaFeatures&&srAvailable)fgNote+=fgNote.empty()?L"This export uses AMD FSR super resolution":L"; this export uses AMD FSR super resolution";
        gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        // Keep the requested multiplier. Changing it after initialization fails
        // would produce a successful-looking file with different settings.
        bool graphReady=false,fgFailure=false;
        auto startGraph=[&](bool fg,uint32_t multiplier){
            gd.enableFg=fg;gd.fgMultiplier=fg?multiplier:1;
            if(graph.initialize(gd)&&graph.createViews()){graphReady=true;return;}
            if(graph.failedBackend()==FailedBackend::VideoHdr||(gd.convertVideoHdr()&&!gd.enableNr&&!gd.enableSr&&!gd.enableFg&&graph.failedBackend()==FailedBackend::NgxCore)){
                graph.shutdown();
                gd.videoHdr.enabled=false;gd.hdrOutput=gd.hdrInput;
                fgNote+=L"; RTX Video HDR failed to initialize, this export stays SDR (error code in the log)";
                if(graph.initialize(gd)&&graph.createViews()){graphReady=true;return;}
            }
            fgFailure=graph.failedBackend()==FailedBackend::Fg;
            graph.shutdown();
        };
        if(options.fg&&!exportFrameGenerationSupported(options.settings.frameGenerationBackend,nvidiaFeatures)){
            failureReason=L"The export device cannot run the selected frame-generation backend. The backend and multiplier were kept.";break;
        }
        if(options.fg){
            startGraph(true,options.fgMultiplier);
            if(!graphReady&&fgFailure){
                failureReason=std::format(L"{} {}X frame generation could not initialize. The backend and multiplier were retained; see the task diagnostic log.",std::wstring(frameGenerationBackendName(options.settings.frameGenerationBackend).begin(),frameGenerationBackendName(options.settings.frameGenerationBackend).end()),options.fgMultiplier);
            }
        } else startGraph(false,1);
        if(!graphReady){if(failureReason.empty())failureReason=L"Enhancement pipeline failed to initialize; see diagnostics";break;}
        if(cancel)break;
        if(gd.hdrOutput&&!hevc){hevc=true;fgNote+=fgNote.empty()?L"HDR video automatically uses HEVC Main10":L"; HDR video automatically uses HEVC Main10";}
        if(!fgNote.empty())progress(0,fgNote);
        const AVRational rate=av_mul_q({rateNum,rateDen},{int(options.fg?options.fgMultiplier:1),1});
        constexpr AVRational mediaTimeBase{1,1000000};
        veyra::log::info("export-timeline",std::format("source-timed export nominal={}/{} encoder={}/{} fg={} backend={} note={} (no CFR preflight or output decode verification)",rateNum,rateDen,rate.num,rate.den,options.fg?options.fgMultiplier:1,frameGenerationBackendName(options.settings.frameGenerationBackend),utf8(fgNote)));
        if(avformat_alloc_output_context2(&mux,nullptr,options.exportMedia.container==ExportContainer::Matroska?"matroska":"mp4",utf8(partial).c_str())<0||!mux)break;
        videoStream=avformat_new_stream(mux,nullptr);if(!videoStream)break;videoStream->time_base=mediaTimeBase;videoStream->avg_frame_rate=rate;
        auto* cp=videoStream->codecpar;cp->codec_type=AVMEDIA_TYPE_VIDEO;cp->codec_id=hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264;cp->width=gd.workWidth;cp->height=gd.workHeight;cp->format=AV_PIX_FMT_YUV420P;cp->color_range=AVCOL_RANGE_MPEG;cp->color_space=AVCOL_SPC_BT709;cp->color_primaries=AVCOL_PRI_BT709;cp->color_trc=AVCOL_TRC_IEC61966_2_1;
        if(gd.hdrOutput){cp->format=AV_PIX_FMT_YUV420P10LE;cp->color_space=AVCOL_SPC_BT2020_NCL;cp->color_primaries=AVCOL_PRI_BT2020;cp->color_trc=AVCOL_TRC_SMPTE2084;cp->profile=AV_PROFILE_HEVC_MAIN_10;}
        // Plan v5.3: this exporter cannot recompute content light levels (that
        // needs a full pre-pass before the header is written), so the source
        // declaration is carried into the 'clli' box verbatim and the graded case
        // is flagged as "not updated" instead of silently changing meaning.
        if(gd.hdrInput&&gd.hdrOutput&&info.color.hdrMaxCllNits>0.0f){
            if(auto* side=av_packet_side_data_new(&cp->coded_side_data,&cp->nb_coded_side_data,AV_PKT_DATA_CONTENT_LIGHT_LEVEL,sizeof(AVContentLightMetadata),0)){
                auto* cll=reinterpret_cast<AVContentLightMetadata*>(side->data);
                cll->MaxCLL=unsigned(info.color.hdrMaxCllNits);
                cll->MaxFALL=unsigned(info.color.hdrMaxFallNits);
            }else failureReason=L"Could not write the content light metadata (MaxCLL/MaxFALL)";
        }
        if(!streams.prepare(input,mux,options.exportMedia,options.audioStreamIndex,requestedStart,requestedEnd,cancel,failureReason))break;
        if(!streams.info().skipped.empty()){const std::wstring note=L"Some tracks do not fit the current container and were not kept (see the queue notes)";fgNote+=fgNote.empty()?note:L"; "+note;progress(0,fgNote);}
        auto writeAudioUntil=[&](double seconds,bool final=false){return streams.writeUntil(seconds,videoOriginSeconds,final,failureReason);};
        // Encoders retain ordinal timestamps; only the bounded in-flight queue
        // maps them to media time. Hold one compressed packet for its duration.
        std::deque<std::pair<int64_t,int64_t>> timestamps;
        auto freePacket=[](AVPacket* p){av_packet_free(&p);};
        std::unique_ptr<AVPacket,decltype(freePacket)> pendingVideo(av_packet_alloc(),freePacket);
        if(!pendingVideo)break;
        auto flushVideo=[&](int64_t endUs){
            auto* pkt=pendingVideo.get();if(!pkt->size)return true;
            pkt->duration=std::max<int64_t>(1,endUs-pkt->pts);
            audioEndSeconds=(pkt->pts+pkt->duration)/1000000.0;
            av_packet_rescale_ts(pkt,mediaTimeBase,videoStream->time_base);
            const int rc=av_interleaved_write_frame(mux,pkt);av_packet_unref(pkt);
            if(rc<0)return failAv(L"Write video frame",rc);
            ++written;return writeAudioUntil(audioEndSeconds);
        };
        auto writer=[&](const uint8_t* bytes,size_t size,int64_t pts,bool key){
            if(!headerWritten)return false;
            if(timestamps.empty()||timestamps.front().first!=pts){failureReason=L"The encoder returned an unknown frame number";return false;}
            const auto timeUs=timestamps.front().second;timestamps.pop_front();
            if(!flushVideo(timeUs))return false;
            auto* pkt=pendingVideo.get();if(av_new_packet(pkt,int(size))<0)return false;
            memcpy(pkt->data,bytes,size);pkt->stream_index=videoStream->index;pkt->pts=pkt->dts=timeUs;
            if(key)pkt->flags|=AV_PKT_FLAG_KEY;return true;
        };
        // Must drain before rate/writeAudioUntil/writer leave scope, including cancel/error.
        struct EncoderCloser {std::unique_ptr<sink::VideoEncoder>& encoder;~EncoderCloser(){encoder.reset();}} closer{encoder};
        sink::EncoderConfig encoderConfig;encoderConfig.hevc=hevc;encoderConfig.fpsNum=unsigned(rate.num);encoderConfig.fpsDen=unsigned(rate.den);encoderConfig.bitrateMbps=options.settings.exportBitrateMbps;encoderConfig.rateControl=options.exportRateControl;
        std::wstring encoderDetail;
        encoder=sink::openVideoEncoder(ctx,ring,graph,encoderConfig,writer,encoderDetail);
        if(!encoder){
            progress(0,encoderDetail.empty()?L"No video encoder available":encoderDetail);
            if(failureReason.empty())failureReason=encoderDetail.empty()?L"No video encoder available":encoderDetail;
            veyra::log::error("export","no usable video encoder for this adapter/codec");
            break;
        }
        encoderName=encoder->describe();
        veyra::log::info("export",std::format("encoder={} codec={} rateControl={} bitrateMbps={} rate={}/{}",std::string(sink::encoderBackendName(encoder->backend())),hevc?"HEVC":"H264",std::string(sink::exportRateControlName(encoderConfig.rateControl)),encoderConfig.bitrateMbps,rate.num,rate.den));
        auto headers=encoder->headers();cp->extradata=static_cast<uint8_t*>(av_mallocz(headers.size()+AV_INPUT_BUFFER_PADDING_SIZE));if(!cp->extradata)break;memcpy(cp->extradata,headers.data(),headers.size());cp->extradata_size=int(headers.size());
        int muxResult=avio_open(&mux->pb,utf8(partial).c_str(),AVIO_FLAG_WRITE);
        if(muxResult<0){failAv(L"Create output file",muxResult);break;}
        muxResult=avformat_write_header(mux,nullptr);
        if(muxResult<0){failAv(L"Write container header",muxResult);break;}headerWritten=true;
        uint64_t sourceCount=0,generatedCount=0,holdCount=0;int64_t outputIndex=0;bool error=false;std::shared_ptr<pipeline::FrameLease> lastReal;
        double previousPts=0;int64_t lastOutputUs=-1;ContinuousTimeline timeline;
        uint64_t slowFrames=0;
        // Directed export measurements exclude feature/encoder creation, but include final drain.
        const auto pipelineStart=std::chrono::steady_clock::now();
        double completionWaitMs=0;
        // Acceptance hook: VEYRA_TEST_EXPORT_SLOW_FRAME=<source index>:<ms> holds that frame's
        // completion back, standing in for a GPU that is slow but alive. Unset in normal use.
        int slowFrameIndex=-1,slowFrameMs=0;
        {wchar_t hook[32]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_SLOW_FRAME",hook,32)&&swscanf_s(hook,L"%d:%d",&slowFrameIndex,&slowFrameMs)!=2)slowFrameIndex=-1;slowFrameMs=std::clamp(slowFrameMs,0,40000);}
        // Acceptance hook: VEYRA_TEST_EXPORT_REMOVE_DEVICE_AT=<source index> removes the D3D12
        // device at that frame in the first process of a job, standing in for a driver reset.
        int removeDeviceIndex=-1;
        {wchar_t hook[16]{};if(resumeAttempt==0&&GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_REMOVE_DEVICE_AT",hook,16))removeDeviceIndex=_wtoi(hook);}
        bool deviceLost=false;uint32_t removedReason=0;
        // Measured 5b default: bounded NVENC exports without generated-frame readback.
        // Keep FG/MFT serial; NVENC retains the conversion input fence and its slots.
        // Explicit test overrides preserve the serial and independent-component controls.
        auto testFlag=[](const wchar_t* key,bool fallback){
            wchar_t value[8]{};const DWORD n=GetEnvironmentVariableW(key,value,8);
            return n&&n<8?_wtoi(value)!=0:fallback;
        };
        const bool serialOverride=testFlag(L"VEYRA_TEST_EXPORT_SERIAL",false);
        const bool eligibleNvenc=!options.fg&&encoder->backend()==sink::EncoderBackend::Nvenc&&slowFrameIndex<0;
        const bool asyncNvenc=!serialOverride&&eligibleNvenc&&testFlag(L"VEYRA_TEST_EXPORT_ASYNC",eligibleNvenc);
        bool fenceEvents=!serialOverride&&testFlag(L"VEYRA_TEST_EXPORT_FENCE_EVENTS",eligibleNvenc);
        if(fenceEvents){
            const bool injected=GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_EVENT_CREATE_FAIL",nullptr,0)>0;
            if(!injected)completionEvent.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!completionEvent.value){veyra::log::warn("export-pipeline",std::format("completion event create failed error={} injected={}; keeping polling",injected?ERROR_INVALID_HANDLE:GetLastError(),injected));fenceEvents=false;}
        }
        struct PendingFrame {pipeline::EnhanceGraph::FrameOutputs output;uint64_t consumerFence=0,source=0;std::chrono::steady_clock::time_point submitted;};
        std::deque<PendingFrame> pendingFrames;unsigned maxInFlight=1;
        // Preserve the 30s alive-device contract, cancellation and slow-frame hook.
        // With two frames in flight this waits only before a pool slot is reused.
        auto waitCompleted=[&](pipeline::EnhanceGraph::FrameOutputs& out,std::chrono::steady_clock::time_point submitted,uint64_t sourceIndex,uint64_t consumerFence=0){
            const auto waitStart=std::chrono::steady_clock::now();
            auto nextHealthCheck=submitted+std::chrono::milliseconds(250);
            const bool injectedStall=slowFrameIndex>=0&&sourceIndex==uint64_t(slowFrameIndex);
            const bool commonFence=(!out.videoFenceValue||out.videoFenceObject.Get()==ctx.fence())&&
                (!out.genFenceValue||out.genFenceObject.Get()==ctx.fence());
            const uint64_t waitFence=std::max({out.videoFenceValue,out.genFenceValue,consumerFence});
            bool eventRegistered=false;
            while(!cancel&&((injectedStall&&std::chrono::steady_clock::now()-submitted<std::chrono::milliseconds(slowFrameMs))||
                    !graph.resolveGeneration(out)||!pipeline::fenceComplete(ctx.fence(),consumerFence))){
                const auto now=std::chrono::steady_clock::now();
                if(now>=nextHealthCheck){
                    nextHealthCheck=now+std::chrono::milliseconds(250);
                    if(!ctx.checkDeviceAlive(removedReason)){
                        ctx.reportDeviceFailure("export-frame",ring.lastSignaledValue());deviceLost=true;error=true;
                        failureReason=std::format(L"The GPU driver hung at frame {} and was reset by the system (0x{:X}). This happens occasionally on RTX 40 GPUs with the community NR; turn NR off, lower the NR internal resolution or change the optical flow, then retry",sourceIndex+1,removedReason);break;
                    }
                }
                if(now-submitted>std::chrono::seconds(30)){
                    ctx.reportDeviceFailure("export-frame-timeout",ring.lastSignaledValue());error=true;
                    failureReason=std::format(L"The GPU did not finish enhancing frame {} within 30 s (no reset reported); lower the NR / super resolution and try again",sourceIndex+1);
                    veyra::log::error("export","frame GPU completion timed out after 30s");break;
                }
                if(fenceEvents&&commonFence&&!injectedStall&&!eventRegistered){
                    const bool injected=GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_EVENT_REGISTER_FAIL",nullptr,0)>0;
                    const HRESULT hr=injected?E_FAIL:ctx.fence()->SetEventOnCompletion(waitFence,completionEvent.value);
                    if(SUCCEEDED(hr))eventRegistered=true;
                    else {veyra::log::warn("export-pipeline",std::format("completion event registration failed hr=0x{:X} injected={}; keeping polling",unsigned(hr),injected));fenceEvents=false;}
                }
                if(eventRegistered){
                    const DWORD result=WaitForSingleObject(completionEvent.value,50);
                    if(result!=WAIT_OBJECT_0&&result!=WAIT_TIMEOUT){veyra::log::warn("export-pipeline",std::format("completion event wait failed result={} error={}; keeping polling",result,GetLastError()));fenceEvents=false;eventRegistered=false;}
                }else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            completionWaitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-waitStart).count();
            if(error||cancel)return false;
            const double latency=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-submitted).count();
            if(latency>1000){
                ++slowFrames;uint64_t budget=0,usage=0;ctx.videoMemoryInfo(budget,usage);
                if(slowFrames<=20)veyra::log::warn("export",std::format("slow frame source={} gpuWaitMs={:.0f} vramMiB={} budgetMiB={}",sourceIndex,latency,usage>>20,budget>>20));
            }
            return true;
        };
        auto finishOldest=[&](){
            auto& old=pendingFrames.front();
            if(!waitCompleted(old.output,old.submitted,old.source,old.consumerFence))return false;
            pendingFrames.pop_front();return true;
        };
        const uint32_t multiplier=options.fg?options.fgMultiplier:1u;
        auto encodeAt=[&](unsigned slot,bool generated,double seconds){
            const int64_t timeUs=std::max(lastOutputUs+1,int64_t(std::llround(seconds*1000000)));
            timestamps.emplace_back(outputIndex,timeUs);lastOutputUs=timeUs;
            return encoder->encode(slot,generated,outputIndex++);
        };
        while(!cancel){if(frameBoundary&&!frameBoundary()){error=true;break;}pipeline::FramePacket packet;const AVFrame* frame=nullptr;
            if(pendingFrames.size()>=2&&!finishOldest())break;
            source::SourceReadStatus rs;
            if(sourceCount==0){packet=firstPacket;frame=firstFrame;rs=source::SourceReadStatus::Frame;}
            else rs=source.read(packet,&frame);
            if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame){failureReason=source.errorMessage();error=true;break;}
            double sourcePts=packet.pts.toDouble();
            if(sourceCount==0)videoOriginSeconds=!packet.pts.isUnknown()&&std::isfinite(sourcePts)?sourcePts:0;
            // A splice that steps back realigns once and keeps its offset (one history reset);
            // an unknown or single bad timestamp continues one interval after the last frame.
            const bool unknownPts=packet.pts.isUnknown()||!std::isfinite(sourcePts-videoOriginSeconds);
            bool steppedBack=false;
            double pts=timeline.map(unknownPts?std::numeric_limits<double>::quiet_NaN():sourcePts-videoOriginSeconds,
                                    sourceCount?previousPts+sourceInterval:0,&steppedBack);
            if(requestedEnd>0&&pts+videoOriginSeconds>=requestedEnd)break;
            const bool repairPts=unknownPts||steppedBack;
            if(repairPts){
                ++repairedTimestamps;
                if(repairedTimestamps<=5)veyra::log::warn("export-timeline",std::format("timestamp repaired source={} raw={} output={}",sourceCount,sourcePts,pts));
            }
            if(removeDeviceIndex>=0&&sourceCount==uint64_t(removeDeviceIndex)){
                Microsoft::WRL::ComPtr<ID3D12Device5> device5;
                if(SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&device5)))){device5->RemoveDevice();veyra::log::warn("export","test hook: D3D12 device removed at this frame");}
            }
            pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(frame,(pts+videoOriginSeconds)*1000,sourceCount==0||repairPts||pipeline::breaksHistory(packet.flags),out,packet.sequence,&packet.colorInfo,&packet.hardwareSurface,false)){error=true;break;}
            const auto readyStart=std::chrono::steady_clock::now();
            if(!asyncNvenc&&!waitCompleted(out,readyStart,sourceCount))break;
            if(sourceCount>0&&options.fg){
                for(uint32_t j=1;j<options.fgMultiplier;++j){
                    pipeline::BatchFrame* item=nullptr;
                    for(uint32_t k=0;k<out.batch.count;++k)if(out.batch.frames[k].subframe==j&&out.batch.frames[k].validity==pipeline::GenerationValidity::Valid)item=&out.batch.frames[k];
                    const double generatedPts=previousPts+(pts-previousPts)*j/multiplier;
                    if(item){if(!encodeAt(item->lease->slot,true,generatedPts)){error=true;break;}item->lease->consumerFence=ring.lastSignaledValue();++generatedCount;}
                    else {if(!lastReal||!encodeAt(lastReal->slot,false,generatedPts)){error=true;break;}lastReal->consumerFence=ring.lastSignaledValue();++holdCount;}
                }
            }
            if(error)break;
            auto& real=out.batch.frames[out.batch.count-1];
            if(!encodeAt(real.lease->slot,false,pts)){error=true;break;}
            real.lease->consumerFence=ring.lastSignaledValue();lastReal=real.lease;
            if(asyncNvenc){
                const uint64_t consumerFence=real.lease->consumerFence;
                pendingFrames.push_back({std::move(out),consumerFence,sourceCount,readyStart});
                maxInFlight=std::max(maxInFlight,unsigned(pendingFrames.size()));
            }
            previousPts=pts;
            ++sourceCount;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});const double clipDuration=requestedEnd>0?requestedEnd-requestedStart:std::max(0.001,info.duration.toDouble()-videoOriginSeconds);progress(clipDuration>0?std::clamp(pts/clipDuration,0.0,.99):0,std::format(L"Exporting: {} source frames / {} encoded frames ({})",sourceCount,outputIndex,encoderName));
            if(maxFrames&&sourceCount>=maxFrames)break;
        }
        while(!pendingFrames.empty()&&!error&&!cancel)if(!finishOldest())break;
        // Submission or encoding can also be the first call to see the reset.
        if(error&&!cancel&&!deviceLost&&!ctx.checkDeviceAlive(removedReason)){
            ctx.reportDeviceFailure("export-frame",ring.lastSignaledValue());
            deviceLost=true;
            failureReason=std::format(L"The GPU driver hung at frame {} and was reset by the system (0x{:X}). This happens occasionally on RTX 40 GPUs with the community NR; turn NR off, lower the NR internal resolution or change the optical flow, then retry",sourceCount+1,removedReason);
        }
        if(deviceLost&&!cancel&&resume&&(written>0||pendingVideo->size)){
            // Close what the encoder already returned into a valid file; the lost device is
            // not asked to drain. The next part starts half a frame after the last written
            // frame, so the following source frame is neither repeated nor lost to rounding.
            const double frameSeconds=sourceInterval/multiplier;
            const int64_t endUs=pendingVideo->size?pendingVideo->pts+std::max<int64_t>(1,std::llround(frameSeconds*1000000)):std::llround(audioEndSeconds*1000000);
            if(flushVideo(endUs)&&writeAudioUntil(audioEndSeconds,true)&&av_write_trailer(mux)>=0){
                resume->resumable=true;resume->originSeconds=videoOriginSeconds;
                resume->resumeAtSeconds=videoOriginSeconds+audioEndSeconds-frameSeconds*0.5;
                failureReason=std::format(L"The GPU was reset at frame {}; the first {:.1f} s are saved, exporting the rest",sourceCount+1,audioEndSeconds);
                veyra::log::warn("export-resume",std::format("device lost at source={} written={} part ends {:.3f}s; resume at source {:.3f}s, part origin {:.3f}s",
                    sourceCount,written,audioEndSeconds,resume->resumeAtSeconds,videoOriginSeconds));
            } else veyra::log::error("export-resume","the part written before the device loss could not be closed; not resumable");
        }
        if(error||cancel)break;
        if(options.fg&&lastReal)for(uint32_t j=1;j<multiplier;++j){if(!encodeAt(lastReal->slot,false,previousPts+sourceInterval*j/multiplier)){error=true;break;}++holdCount;}
        if(error)break;
        veyra::log::info("export-counts",std::format("slowFrames={} source={} generated={} hold={} output={} multiplier={} repairedTimestamps={} backend={} vfgQuality={} encoder={} bitrateMbps={} note={} (holds are not generated frames)",slowFrames,sourceCount,generatedCount,holdCount,outputIndex,multiplier,repairedTimestamps,frameGenerationBackendName(options.settings.frameGenerationBackend),options.settings.vfgQuality,std::string(sink::encoderBackendName(encoder->backend())),options.settings.exportBitrateMbps,utf8(fgNote)));
        progress(.99,L"Finishing: waiting for the encoder's remaining frames");
        if(!encoder->finish()){if(failureReason.empty())failureReason=L"Encoder failed to finish; see the encoder diagnostics";break;}
        veyra::log::info("export-pipeline",std::format("source={} pipelineMs={:.3f} completionWaitMs={:.3f} mode={} fenceEvents={} maxInFlight={}",
            sourceCount,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pipelineStart).count(),completionWaitMs,asyncNvenc?"async":"serial",fenceEvents,maxInFlight));
        const int64_t estimatedEnd=lastOutputUs+std::max<int64_t>(1,int64_t(std::llround(sourceInterval*1000000/multiplier)));
        const int64_t finalEnd=requestedEnd>0?std::min(estimatedEnd,int64_t(std::llround((requestedEnd-videoOriginSeconds)*1000000))):estimatedEnd;
        if(!flushVideo(finalEnd))break;
        if(!writeAudioUntil(audioEndSeconds,true))break;
        progress(.995,L"Finishing: writing the container index");
        muxResult=av_write_trailer(mux);
        if(muxResult<0){failAv(L"Write container index",muxResult);break;}
        ok=written>0;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});
    }while(false); }catch(const std::exception& e){veyra::log::error("export",std::format("exception: {}",e.what()));failureReason=L"Export error; see diagnostics";ok=false;}
    encoder.reset();if(mux){if(mux->pb){const int rc=avio_closep(&mux->pb);if(rc<0){failAv(L"Flush and close the output file",rc);ok=false;}}avformat_free_context(mux);}
    ring.drainQueue();graph.shutdown();source.close();ring.shutdown();
    if(GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_DEBUG",nullptr,0)>0&&ctx.device()){
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;const HRESULT hr=ctx.device()->QueryInterface(IID_PPV_ARGS(&debug));unsigned errors=0;
        if(FAILED(hr)){ok=false;veyra::log::error("export-debug",std::format("InfoQueue query failed hr=0x{:X}",unsigned(hr)));}
        else for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){
            SIZE_T bytes=0;debug->GetMessage(i,nullptr,&bytes);std::vector<unsigned char> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(FAILED(debug->GetMessage(i,message,&bytes))){ok=false;++errors;continue;}
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;veyra::log::error("export-debug",message->pDescription);}
        }
        if(errors)ok=false;veyra::log::info("export-debug",std::format("errors={} removedReason=0x{:X}",errors,unsigned(ctx.device()->GetDeviceRemovedReason())));
    }
    ctx.shutdown();
    // Earlier processes of this job stopped at a GPU reset: join their parts and this
    // one into the output. The parts are removed only once the joined file is saved.
    std::wstring finished=partial;
    ExportSegments parts;
    if(ok&&!priorSegments.empty()){
        progress(.999,L"Joining the parts written before and after the GPU reset");
        parts=priorSegments;parts.emplace_back(partial,videoOriginSeconds);
        const auto joined=partial+L".joined";
        std::wstring joinError;
        ok=!cancel&&joinExportSegments(parts,joined,options.exportMedia.container==ExportContainer::Matroska,joinError);
        if(ok){
            finished=joined;
            fgNote+=std::format(L"{}GPU reset {} time(s); exported in {} parts and joined without re-encoding",fgNote.empty()?L"":L"; ",priorSegments.size(),parts.size());
        } else {
            std::error_code ec;std::filesystem::remove(joined,ec);
            std::wstring kept;for(const auto& part:parts)kept+=L"\n"+part.first;
            failureReason=(joinError.empty()?std::wstring(L"Joining the parts failed"):joinError)+L"; the part files are kept: "+kept;
        }
    }
    if(ok){
        progress(.999,L"Saving the final file");
        // Bounded deterministic acceptance hook around the real final rename.
        // Normal runs do not wait here; no output or success result is faked.
        wchar_t delayText[16]{};
        if(GetEnvironmentVariableW(L"VEYRA_TEST_EXPORT_SAVE_DELAY_MS",delayText,16)){
            const auto deadline=GetTickCount64()+std::clamp(_wtoi(delayText),0,2000);
            while(!cancel&&GetTickCount64()<deadline)Sleep(5);
        }
        ok=!cancel&&MoveFileExW(finished.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!ok&&!cancel){const DWORD error=GetLastError();failureReason=std::format(L"Video encoded. Windows error {} blocked renaming. The temporary file contains the complete video.",error);veyra::log::error("export-rename",std::format("MoveFileExW failed error={} finished={}",error,utf8(finished)));}
        if(ok){
            for(const auto& part:parts){std::error_code ec;std::filesystem::remove(part.first,ec);}
            veyra::log::info("export","encoder drained, mux closed, output saved; no post-export decoding");
        }
    }
    if(ok){
        std::wstring done=encoderName.empty()?L"Video export finished":std::format(L"Video export finished ({})",encoderName);
        if(repairedTimestamps>0)done+=std::format(L" (repaired {} frames with missing or backward timestamps)",repairedTimestamps);
        progress(1,fgNote.empty()?done:fgNote+L"; "+done);
    }
    else {
        std::wstring message=cancel?L"Export cancelled":failureReason.empty()?L"Video export failed; see diagnostics":failureReason;
        std::error_code ec;
        std::wstring cleanup;
        if(cancel&&finished!=partial)std::filesystem::remove(finished,ec);
        if(cancel&&!removeExportTemporaryFile(partial,cleanup))message+=L"; "+cleanup;
        else if(std::filesystem::exists(finished,ec))message+=L"; temporary file kept: "+finished;
        progress(0,message);
    }
    return ok;
}

bool joinExportSegments(const ExportSegments& parts,const std::wstring& output,bool matroska,std::wstring& error){
    if(parts.size()<2){error=L"No parts to join";return false;}
    std::vector<AVFormatContext*> inputs(parts.size(),nullptr);
    AVFormatContext* out=nullptr;AVPacket* packet=av_packet_alloc();
    bool ok=false;
    auto failAv=[&](const char* stage,int code){
        char text[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,text,sizeof(text));
        veyra::log::error("export-join",std::format("{} failed code={} detail={}",stage,code,text));
        error=std::format(L"Joining the parts failed ({} error {})",std::wstring(stage,stage+std::strlen(stage)),code);
        return false;
    };
    do {
        if(!packet)break;
        for(size_t i=0;i<parts.size();++i){
            int rc=avformat_open_input(&inputs[i],utf8(parts[i].first).c_str(),nullptr,nullptr);
            if(rc<0){failAv("open part",rc);break;}
            rc=avformat_find_stream_info(inputs[i],nullptr);
            if(rc<0){failAv("read part",rc);break;}
        }
        if(!error.empty())break;
        const auto* first=inputs[0];
        // Same streams in the same order, and identical codec headers: the joined file
        // carries the first part's headers for all of them.
        bool same=true;
        for(size_t i=1;i<inputs.size()&&same;++i){
            if(inputs[i]->nb_streams!=first->nb_streams){same=false;break;}
            for(unsigned s=0;s<first->nb_streams;++s){
                const auto* a=first->streams[s]->codecpar;const auto* b=inputs[i]->streams[s]->codecpar;
                if(a->codec_type!=b->codec_type||a->codec_id!=b->codec_id){same=false;break;}
                if(a->codec_type==AVMEDIA_TYPE_VIDEO&&(a->width!=b->width||a->height!=b->height||a->extradata_size!=b->extradata_size||
                   (a->extradata_size>0&&std::memcmp(a->extradata,b->extradata,size_t(a->extradata_size))!=0))){same=false;break;}
            }
        }
        if(!same){
            error=L"The parts before and after the GPU reset have different encoder parameters and cannot be joined without re-encoding";
            veyra::log::error("export-join","parts differ in streams or video parameter sets; not joined");
            break;
        }
        if(avformat_alloc_output_context2(&out,nullptr,matroska?"matroska":"mp4",utf8(output).c_str())<0||!out){error=L"Cannot create the joined output";break;}
        for(unsigned s=0;s<first->nb_streams;++s){
            AVStream* stream=avformat_new_stream(out,nullptr);
            if(!stream||avcodec_parameters_copy(stream->codecpar,first->streams[s]->codecpar)<0){error=L"Cannot copy the track parameters";break;}
            stream->codecpar->codec_tag=0;
            stream->time_base=first->streams[s]->time_base;
            stream->avg_frame_rate=first->streams[s]->avg_frame_rate;
            stream->disposition=first->streams[s]->disposition;
            av_dict_copy(&stream->metadata,first->streams[s]->metadata,0);
        }
        if(!error.empty())break;
        int rc=avio_open(&out->pb,utf8(output).c_str(),AVIO_FLAG_WRITE);
        if(rc<0){failAv("create output",rc);break;}
        rc=avformat_write_header(out,nullptr);
        if(rc<0){failAv("write header",rc);break;}
        std::vector<int64_t> lastDts(first->nb_streams,AV_NOPTS_VALUE);
        uint64_t copied=0,dropped=0;
        for(size_t i=0;i<inputs.size()&&error.empty();++i){
            const int64_t offsetUs=std::llround((parts[i].second-parts[0].second)*1000000.0);
            while((rc=av_read_frame(inputs[i],packet))>=0){
                const unsigned s=unsigned(packet->stream_index);
                if(s>=first->nb_streams){av_packet_unref(packet);continue;}
                AVStream* to=out->streams[s];
                av_packet_rescale_ts(packet,inputs[i]->streams[s]->time_base,to->time_base);
                const int64_t offset=av_rescale_q(offsetUs,AVRational{1,1000000},to->time_base);
                if(packet->pts!=AV_NOPTS_VALUE)packet->pts+=offset;
                if(packet->dts!=AV_NOPTS_VALUE)packet->dts+=offset;
                // A packet that does not move forward (audio overlapping the cut) is dropped:
                // the earlier part already carries that time.
                if(packet->dts!=AV_NOPTS_VALUE&&lastDts[s]!=AV_NOPTS_VALUE&&packet->dts<=lastDts[s]){++dropped;av_packet_unref(packet);continue;}
                if(packet->dts!=AV_NOPTS_VALUE)lastDts[s]=packet->dts;
                packet->pos=-1;
                rc=av_interleaved_write_frame(out,packet);
                if(rc<0){failAv("write packet",rc);break;}
                ++copied;
            }
            if(rc<0&&rc!=AVERROR_EOF&&error.empty())failAv("read packet",rc);
        }
        if(!error.empty())break;
        rc=av_write_trailer(out);
        if(rc<0){failAv("write index",rc);break;}
        veyra::log::info("export-join",std::format("joined parts={} packets={} droppedOverlap={}",parts.size(),copied,dropped));
        ok=true;
    } while(false);
    av_packet_free(&packet);
    for(auto*& input:inputs)if(input)avformat_close_input(&input);
    if(out){if(out->pb){const int rc=avio_closep(&out->pb);if(rc<0&&ok)ok=failAv("close output",rc);}avformat_free_context(out);}
    if(ok)error.clear();
    return ok;
}
}
