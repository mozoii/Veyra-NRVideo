#include "veyra/engine/ExportStreams.h"
#include "veyra/Log.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
}
namespace veyra::engine {
namespace {
std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::wstring wide(const char* s){if(!s)return {};const int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s,-1,r.data(),n);if(!r.empty())r.pop_back();return r;}
std::string metadata(AVDictionary* m,const char* key){const auto e=av_dict_get(m,key,nullptr,0);return e?e->value:"";}
bool avError(std::wstring& error,const wchar_t* stage,int code){char detail[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,detail,sizeof(detail));error=std::format(L"{}: {} ({})",stage,wide(detail),code);log::error("export-streams",utf8(error));return false;}
bool textSubtitle(AVCodecID id){const auto d=avcodec_descriptor_get(id);return d&&(d->props&AV_CODEC_PROP_TEXT_SUB);}
bool validPgsPacket(const AVPacket& packet){
    // Some Matroska compression methods are unavailable in the shipped media
    // library. Its demuxer can return compressed bytes after only logging an
    // error; pgs_frame_merge then drops them without an error return. Validate
    // segment framing before passing packets to that filter, never claim that
    // an empty output track preserved the source subtitles.
    if(packet.size<=0)return false;
    size_t offset=0;
    while(offset<size_t(packet.size)){
        if(size_t(packet.size)-offset<3)return false;
        const auto* data=packet.data+offset;
        if((data[0]<0x14||data[0]>0x17)&&data[0]!=0x80)return false;
        const size_t length=3+(size_t(data[1])<<8)+data[2];
        if(length>size_t(packet.size)-offset)return false;
        offset+=length;
    }
    return true;
}
struct PacketDelete{void operator()(AVPacket* p)const{av_packet_free(&p);}};
using Packet=std::unique_ptr<AVPacket,PacketDelete>;
void workerAvLog(void* context,int level,const char* format,va_list args){
    if(level>av_log_get_level())return;
    char line[4096]{};int prefix=1;av_log_format_line2(context,level,format,args,line,sizeof(line),&prefix);
    if(level<=AV_LOG_ERROR)log::error("ffmpeg-export",line);else if(level<=AV_LOG_WARNING)log::warn("ffmpeg-export",line);else log::info("ffmpeg-export",line);
}
}
void ExportStreams::enableWorkerLogging(bool detailed){av_log_set_callback(workerAvLog);av_log_set_level(detailed?AV_LOG_DEBUG:AV_LOG_WARNING);}
struct ExportStreams::Impl {
    struct Track {
        AVStream* source=nullptr;AVStream* target=nullptr;
        AVCodecContext* decoder=nullptr;AVCodecContext* encoder=nullptr;
        bool subtitle=false,text=false;
        uint64_t read=0,written=0;
        ~Track(){avcodec_free_context(&decoder);avcodec_free_context(&encoder);}
    };
    struct Buffered {Packet packet;int index;double time,duration;};
    AVFormatContext* input=nullptr;AVFormatContext* output=nullptr;
    std::vector<std::unique_ptr<Track>> tracks;
    std::deque<Buffered> pending;
    ExportMediaInfo info;
    std::atomic<bool>* cancel=nullptr;
    ULONGLONG deadline=0;
    int video=-1;
    bool eof=false;
    double watermark=-std::numeric_limits<double>::infinity(),start=0,end=0;
    size_t bufferedBytes=0;
    ~Impl(){pending.clear();tracks.clear();avformat_close_input(&input);}
    static int interrupted(void* opaque){auto& p=*static_cast<Impl*>(opaque);return (p.cancel&&p.cancel->load())||(p.deadline&&GetTickCount64()>p.deadline);}
};
ExportStreams::ExportStreams():p_(std::make_unique<Impl>()){}
ExportStreams::~ExportStreams()=default;
const ExportMediaInfo& ExportStreams::info()const{return p_->info;}
bool ExportStreams::prepare(const std::wstring& path,AVFormatContext* output,const ExportMediaOptions& options,
                           int defaultAudio,double start,double end,std::atomic<bool>& cancel,std::wstring& error){
    auto& p=*p_;p.output=output;p.cancel=&cancel;p.start=start;p.end=end;p.deadline=GetTickCount64()+15000;
    p.input=avformat_alloc_context();if(!p.input)return avError(error,L"Create track reader",AVERROR(ENOMEM));
    p.input->interrupt_callback={Impl::interrupted,&p};
    int rc=avformat_open_input(&p.input,utf8(path).c_str(),nullptr,nullptr);
    if(rc<0)return avError(error,L"Open source tracks",rc);
    rc=avformat_find_stream_info(p.input,nullptr);if(rc<0)return avError(error,L"Read source tracks",rc);
    p.info.duration=p.input->duration==AV_NOPTS_VALUE?0:p.input->duration/double(AV_TIME_BASE);
    p.video=av_find_best_stream(p.input,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if(p.video<0){error=L"The file has no video track that can be exported";return false;}
    p.tracks.resize(p.input->nb_streams);
    if(options.audio.policy==ExportTrackPolicy::Default&&defaultAudio>=0&&
       (unsigned(defaultAudio)>=p.input->nb_streams||p.input->streams[defaultAudio]->codecpar->codec_type!=AVMEDIA_TYPE_AUDIO)){
        error=std::format(L"The selected audio track {} no longer exists or has the wrong type",defaultAudio);return false;
    }
    if(defaultAudio<0)defaultAudio=av_find_best_stream(p.input,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
    auto chosen=[](const ExportTrackSelection& s,int index,bool isDefault){return s.policy==ExportTrackPolicy::All||(s.policy==ExportTrackPolicy::Selected&&s.contains(index))||(s.policy==ExportTrackPolicy::Default&&isDefault);};
    const bool mkv=options.container==ExportContainer::Matroska;
    bool hasSubtitles=false,anySkipped=false;unsigned skippedAudio=0,keptAudio=0;
    for(unsigned i=0;i<p.input->nb_streams;++i){
        auto* source=p.input->streams[i];const auto* cp=source->codecpar;
        const bool audio=cp->codec_type==AVMEDIA_TYPE_AUDIO,subtitle=cp->codec_type==AVMEDIA_TYPE_SUBTITLE;
        if(!audio&&!subtitle)continue;
        ExportTrackInfo t;t.index=int(i);t.audio=audio;t.subtitle=subtitle;t.channels=cp->ch_layout.nb_channels;
        t.codec=avcodec_get_name(cp->codec_id);t.language=metadata(source->metadata,"language");t.title=metadata(source->metadata,"title");
        t.defaultTrack=(source->disposition&AV_DISPOSITION_DEFAULT)!=0;t.forced=(source->disposition&AV_DISPOSITION_FORCED)!=0;
        t.selected=chosen(audio?options.audio:options.subtitles,int(i),audio&&int(i)==defaultAudio);
        t.action=t.selected?L"Keep as is":L"Don't keep";
        const bool text=subtitle&&textSubtitle(cp->codec_id);
        AVCodecID targetCodec=cp->codec_id;
        if(t.selected){
            if(subtitle&&!text&&(start>0||end>0)){t.compatible=false;t.action=L"Trimming bitmap subtitles is not supported yet; keep the whole film or exclude this track";}
            else if(avformat_query_codec(output->oformat,cp->codec_id,FF_COMPLIANCE_NORMAL)<=0){
                if(text&&options.convertTextSubtitles){targetCodec=mkv?AV_CODEC_ID_ASS:AV_CODEC_ID_MOV_TEXT;t.converted=true;t.action=mkv?L"Convert to ASS text subtitles":L"Convert to MP4 text subtitles (complex styling is not kept)";}
                else{t.compatible=false;t.action=mkv?L"MKV cannot keep this codec as is; exclude this track":L"MP4 cannot keep this codec as is; choose MKV or exclude this track";}
            }
            if(!t.compatible){
                // "All" is the default and means everything this container can
                // hold: skip what it cannot and say so before start. A track the
                // user picked by hand still fails the export instead of vanishing.
                const bool all=(audio?options.audio:options.subtitles).policy==ExportTrackPolicy::All;
                auto& report=all?p.info.skipped:error;
                if(!report.empty())report+=L"\n";
                report+=std::format(L"Track {} ({}): {}",i,wide(t.codec.c_str()),t.action);
                if(all){t.action+=L" (skipped)";anySkipped=true;skippedAudio+=audio;}
            }
        }
        p.info.tracks.push_back(t);
        if(!t.selected||!t.compatible)continue;
        auto track=std::make_unique<Impl::Track>();track->source=source;track->subtitle=subtitle;track->text=text;
        track->target=avformat_new_stream(output,nullptr);if(!track->target)return avError(error,L"Create output track",AVERROR(ENOMEM));
        if(t.converted){
            const auto* decoder=avcodec_find_decoder(cp->codec_id);const auto* encoder=avcodec_find_encoder(targetCodec);
            if(!decoder||!encoder){error=std::format(L"The subtitle codec needed by track {} is not available",i);return false;}
            track->decoder=avcodec_alloc_context3(decoder);track->encoder=avcodec_alloc_context3(encoder);
            if(!track->decoder||!track->encoder)return avError(error,L"Allocate subtitle codec",AVERROR(ENOMEM));
            rc=avcodec_parameters_to_context(track->decoder,cp);if(rc<0)return avError(error,L"Read subtitle parameters",rc);
            track->decoder->pkt_timebase=source->time_base;
            rc=avcodec_open2(track->decoder,decoder,nullptr);if(rc<0)return avError(error,L"Open subtitle decoder",rc);
            auto* enc=track->encoder;enc->time_base={1,1000};enc->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
            if(track->decoder->subtitle_header_size){
                enc->subtitle_header_size=track->decoder->subtitle_header_size;
                enc->subtitle_header=static_cast<uint8_t*>(av_mallocz(enc->subtitle_header_size+AV_INPUT_BUFFER_PADDING_SIZE));
                if(!enc->subtitle_header)return avError(error,L"Allocate subtitle header",AVERROR(ENOMEM));
                memcpy(enc->subtitle_header,track->decoder->subtitle_header,enc->subtitle_header_size);
            }
            rc=avcodec_open2(enc,encoder,nullptr);if(rc<0)return avError(error,L"Open subtitle encoder",rc);
            rc=avcodec_parameters_from_context(track->target->codecpar,enc);
        }else rc=avcodec_parameters_copy(track->target->codecpar,cp);
        if(rc<0)return avError(error,L"Copy track parameters",rc);
        track->target->codecpar->codec_tag=0;track->target->time_base=source->time_base;
        if(audio&&start>0)track->target->codecpar->initial_padding=0;
        track->target->disposition=source->disposition;
        av_dict_copy(&track->target->metadata,source->metadata,0);
        // Source-duration tags become false after a trim. The muxer derives new
        // duration from packets while language/title/default/forced remain intact.
        av_dict_set(&track->target->metadata,"DURATION",nullptr,0);
        hasSubtitles|=subtitle;keptAudio+=audio;p.tracks[i]=std::move(track);
        log::info("export-streams",std::format("stream={} codec={} text={} action={}",i,t.codec,text,utf8(t.action)));
    }
    // Validate explicit selections against this file, not against preview tracks.
    for(const auto* s:{&options.audio,&options.subtitles})if(s->policy==ExportTrackPolicy::Selected){
        for(uint32_t j=0;j<s->count;++j){const int index=s->indices[j];const auto type=s==&options.audio?AVMEDIA_TYPE_AUDIO:AVMEDIA_TYPE_SUBTITLE;
            if(index<0||unsigned(index)>=p.input->nb_streams||p.input->streams[index]->codecpar->codec_type!=type){error=std::format(L"The selected track {} no longer exists or has the wrong type",index);return false;}
        }
    }
    if(!error.empty())return false;
    if(anySkipped){
        p.info.skipped=std::wstring(L"These tracks cannot be kept in the current container and are skipped on export")+(mkv?L"":L" (MKV can keep more)")+L": \n"+p.info.skipped;
        if(skippedAudio&&!keptAudio)p.info.skipped+=L"\nNote: the exported video will have no sound";
        log::warn("export-streams",utf8(p.info.skipped));
    }
    if(mkv&&hasSubtitles)for(unsigned i=0;i<p.input->nb_streams;++i){
        auto* source=p.input->streams[i];const auto* cp=source->codecpar;
        if(cp->codec_type!=AVMEDIA_TYPE_ATTACHMENT||(cp->codec_id!=AV_CODEC_ID_TTF&&cp->codec_id!=AV_CODEC_ID_OTF))continue;
        auto* target=avformat_new_stream(output,nullptr);if(!target)return avError(error,L"Create font attachment",AVERROR(ENOMEM));
        rc=avcodec_parameters_copy(target->codecpar,cp);if(rc<0)return avError(error,L"Copy font attachment",rc);
        target->codecpar->codec_tag=0;av_dict_copy(&target->metadata,source->metadata,0);
    }
    av_dict_copy(&output->metadata,p.input->metadata,0);av_dict_set(&output->metadata,"DURATION",nullptr,0);
    output->max_interleave_delta=AV_TIME_BASE; // sparse subtitle tracks cannot buffer the whole film
    output->error_recognition|=AV_EF_EXPLODE; // a failing automatic bitstream filter must never silently discard a track
    p.deadline=0;return true;
}
bool ExportStreams::writeUntil(double seconds,double origin,bool final,std::wstring& error){
    auto& p=*p_;p.deadline=GetTickCount64()+15000;
    // Read a bounded amount beyond video progress, using video packets as the
    // demux watermark. A subtitle seven minutes ahead never blocks audio.
    while(!p.eof&&p.watermark<origin+seconds+1.0){
        if(p.cancel->load()){error=L"Export cancelled";return false;}
        Packet pkt(av_packet_alloc());if(!pkt)return avError(error,L"Allocate track packet",AVERROR(ENOMEM));
        const int rc=av_read_frame(p.input,pkt.get());if(rc==AVERROR_EOF){p.eof=true;break;}if(rc<0)return avError(error,L"Read extra tracks",rc);
        const int index=pkt->stream_index;const auto* source=p.input->streams[index];
        const int64_t ts=pkt->pts!=AV_NOPTS_VALUE?pkt->pts:pkt->dts;
        const double time=ts==AV_NOPTS_VALUE?0:ts*av_q2d(source->time_base);
        if(index==p.video&&ts!=AV_NOPTS_VALUE)p.watermark=time;
        if(!p.tracks[index])continue;
        if(ts==AV_NOPTS_VALUE){error=std::format(L"Packets of track {} have no timestamps, so sync cannot be guaranteed",index);return false;}
        double duration=pkt->duration*av_q2d(source->time_base);
        auto& track=*p.tracks[index];
        ++track.read;
        if(track.source->codecpar->codec_id==AV_CODEC_ID_HDMV_PGS_SUBTITLE&&!validPgsPacket(*pkt)){
            error=std::format(L"Track {} has invalid PGS data, or the source file's track compression is not supported by the current media library; remux the source losslessly first, or exclude this subtitle track",index);
            return false;
        }
        if(track.encoder){
            AVSubtitle sub{};int got=0;
            const int decoded=avcodec_decode_subtitle2(track.decoder,&sub,&got,pkt.get());
            if(decoded<0){avsubtitle_free(&sub);return avError(error,L"Decode subtitles",decoded);}
            if(!got){avsubtitle_free(&sub);continue;}
            if(sub.end_display_time>sub.start_display_time)duration=(sub.end_display_time-sub.start_display_time)/1000.0;
            std::vector<uint8_t> bytes(1024*1024);
            const int encoded=avcodec_encode_subtitle(track.encoder,bytes.data(),int(bytes.size()),&sub);avsubtitle_free(&sub);
            if(encoded<0)return avError(error,L"Convert subtitles",encoded);
            if(!encoded)continue;
            Packet converted(av_packet_alloc());if(!converted||av_new_packet(converted.get(),encoded)<0)return avError(error,L"Allocate converted subtitle",AVERROR(ENOMEM));
            memcpy(converted->data,bytes.data(),encoded);converted->flags=pkt->flags;converted->pts=pkt->pts;converted->dts=pkt->dts;converted->duration=pkt->duration;
            pkt=std::move(converted);
        }
        const double relative=time-origin;
        if(track.subtitle&&track.text){if(relative+duration<=0)continue;}
        else if(relative<0&&(p.start>0||relative+duration< -0.00001))continue;
        // Retain the original negative priming packet ending at zero. Dropping
        // it while preserving Matroska CodecDelay skips audible samples twice.
        if(p.end>0&&time>=p.end)continue;
        p.bufferedBytes+=pkt->size;
        if(p.bufferedBytes>64*1024*1024||p.pending.size()>=16384){error=L"The source file's tracks are interleaved too far apart, beyond the safe buffer of 64 MiB / 16384 packets";return false;}
        p.pending.push_back({std::move(pkt),index,relative,duration});
    }
    for(auto it=p.pending.begin();it!=p.pending.end();){
        auto& track=*p.tracks[it->index];
        double start=it->time,end=start+it->duration;
        if(p.end>0)end=std::min(end,p.end-origin);
        // Hold text until its end is known to fit the video. At final drain a
        // crossing cue is clipped, including cues that started before the trim.
        const bool due=final||(track.subtitle&&track.text?end<=seconds:start<seconds);
        if(!due){++it;continue;}
        if(final)end=std::min(end,seconds);
        const size_t bytes=it->packet->size;
        auto* pkt=it->packet.get();
        if(start<seconds&&(!track.text||end>std::max(0.0,start))){
            const auto tb=track.source->time_base;
            if(track.text){start=std::max(0.0,start);pkt->pts=pkt->dts=av_rescale_q(int64_t(std::llround(start*1000000)),{1,1000000},tb);pkt->duration=std::max<int64_t>(1,av_rescale_q(int64_t(std::llround((end-start)*1000000)),{1,1000000},tb));}
            else{const int64_t offset=av_rescale_q(int64_t(std::llround(origin*1000000)),{1,1000000},tb);if(pkt->pts!=AV_NOPTS_VALUE)pkt->pts-=offset;if(pkt->dts!=AV_NOPTS_VALUE)pkt->dts-=offset;}
            av_packet_rescale_ts(pkt,tb,track.target->time_base);pkt->stream_index=track.target->index;pkt->pos=-1;
            const int rc=av_interleaved_write_frame(p.output,pkt);if(rc<0)return avError(error,L"Write extra track",rc);
            ++track.written;
        }
        p.bufferedBytes-=bytes;it=p.pending.erase(it);
    }
    if(final)for(size_t index=0;index<p.tracks.size();++index)if(p.tracks[index])log::info("export-streams",std::format("drained stream={} read={} written={} videoEnd={} origin={}",index,p.tracks[index]->read,p.tracks[index]->written,seconds,origin));
    p.deadline=0;return true;
}
ExportMediaInfo ExportStreams::probe(const std::wstring& input,const ExportMediaOptions& options,double start,double end,std::atomic<bool>& cancel){
    ExportMediaInfo result;AVFormatContext* output=nullptr;
    const int rc=avformat_alloc_output_context2(&output,nullptr,options.container==ExportContainer::Matroska?"matroska":"mp4",nullptr);
    if(rc<0||!output){result.error=L"The output muxer is not available";return result;}
    {ExportStreams streams;std::wstring error;streams.prepare(input,output,options,-1,start,end,cancel,error);result=streams.info();result.error=error;}
    avformat_free_context(output);return result;
}
}
