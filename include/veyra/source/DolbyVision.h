#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <cstdint>
#include <string>

namespace veyra::source {
enum class DolbyBaseLayer : uint8_t { None, Unsupported, Hdr10, Sdr, Hlg };
struct DolbyVisionInfo {
    bool present=false, baseLayer=false, enhancementLayer=false, rpuDeclared=false, rpuObserved=false;
    // Profile-5 conversion bookkeeping (custom). `profile5Converted` means the
    // RPU supplied usable conversion parameters for a frame, i.e. the conversion
    // is really running; the other three only keep their log line to one
    // occurrence per open.
    bool profile5Converted=false, profile5MissingColor=false, profile5UnsupportedMapping=false, profile5Placeholder=false;
    uint8_t profile=0, level=0, compatibility=0;
    DolbyBaseLayer route() const {
        if(!present)return DolbyBaseLayer::None;
        if(!baseLayer)return DolbyBaseLayer::Unsupported;
        if(profile==7)return DolbyBaseLayer::Hdr10;
        // Profile 5: 12-bit HEVC IPT-PQ with no HDR10/SDR compatibility layer.
        // The HEVC base layer is still PQ/BT.2020-flagged and can be watched as
        // HDR10 (RPU/enhancement layer is not applied, no native DV output).
        if(profile==5)return DolbyBaseLayer::Hdr10;
        if(profile!=8&&profile!=10)return DolbyBaseLayer::Unsupported;
        // Profile 10 with compatibility id 0 means "Dolby Vision proprietary": no
        // cross-compatibility is advertised, but Dolby's bitstream table gives its
        // base layer the VUI 1/9/16/15/1, i.e. PQ / BT.2020. Both of Dolby's own
        // profile 10 test signals carry it, so refusing it rejected a valid stream.
        if(profile==10&&compatibility==0)return DolbyBaseLayer::Hdr10;
        switch(compatibility){
        case 1:return DolbyBaseLayer::Hdr10;
        case 2:return DolbyBaseLayer::Sdr;
        case 4:return DolbyBaseLayer::Hlg;
        default:return DolbyBaseLayer::Unsupported;
        }
    }
    bool isProfile84() const { return profile == 8 && route() == DolbyBaseLayer::Hlg; }
    std::wstring description() const {
        if(!present)return {};
        const auto layer=route();
        const wchar_t* name=layer==DolbyBaseLayer::Hdr10?L"HDR10":layer==DolbyBaseLayer::Hlg?L"HLG":L"SDR";
        if(layer==DolbyBaseLayer::Unsupported)return L"Dolby Vision P"+std::to_wstring(profile)+L": this type needs RPU reconstruction and cannot be shown correctly yet";
        // Profile 5 has no compatible base layer at all: its colour exists only
        // inside the RPU, so say what is actually happening rather than
        // claiming a base-layer compatibility that does not exist - and only
        // claim the conversion once its parameters are really in effect.
        if(profile==5)return profile5Converted
            ? L"Dolby Vision P5 · IPT-PQ-C2 base layer converted from the RPU ("+std::wstring(name)+L" output; the enhancement layer and RPU dynamic metadata are not part of the output)"
            : L"Dolby Vision P5: no usable RPU conversion parameters yet, so the frame cannot be converted correctly (it does not fall back to plain YUV decoding)";
        return L"Dolby Vision P"+std::to_wstring(profile)+L" · "+name+L" base-layer compatible (no RPU/enhancement layer, no native DV output)";
    }
    bool matches(const pipeline::ColorDescription& c) const {
        using namespace pipeline;
        switch(route()){
        case DolbyBaseLayer::Hdr10:return c.transfer==TransferFunction::PQ&&c.matrix==YuvMatrix::BT2020NCL&&c.primaries==ColorPrimaries::BT2020;
        case DolbyBaseLayer::Hlg:return c.transfer==TransferFunction::HLG&&c.matrix==YuvMatrix::BT2020NCL&&c.primaries==ColorPrimaries::BT2020;
        case DolbyBaseLayer::Sdr:return !c.isHdrPath()&&c.matrix==YuvMatrix::BT709&&c.primaries==ColorPrimaries::BT709;
        case DolbyBaseLayer::None:return true;
        default:return false;
        }
    }
};
}
