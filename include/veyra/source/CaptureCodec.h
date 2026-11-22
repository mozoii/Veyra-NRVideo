#pragma once
// Compressed payload codec of the streaming sources (PC and Xbox streaming)
// and of the decoder they share. The name predates the capture-card removal:
// the same decoder once handled capture-card payloads too.
namespace veyra::source {
enum class CaptureCodec { None, Mjpeg, H264, Hevc, Av1, Vp9 };

inline const char* captureCodecKey(CaptureCodec codec) {
    switch (codec) {
    case CaptureCodec::Mjpeg: return "mjpeg";
    case CaptureCodec::H264: return "h264";
    case CaptureCodec::Hevc: return "hevc";
    case CaptureCodec::Av1: return "av1";
    case CaptureCodec::Vp9: return "vp9";
    case CaptureCodec::None: break;
    }
    return "none";
}
}
