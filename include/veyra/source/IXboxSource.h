// SPDX-License-Identifier: GPL-3.0-only
// Interface of the Xbox transport engine. Both V1 (native libdatachannel) and
// V2 (Chromium reference player inside WebView2) implement it, so the engine,
// UI and enhancement chain see the same source contract regardless of transport.
// Ported from PR #289 by RyanBB (@RyanLookchard), using this fork's input::
// controller types.
#pragma once
#include <cstdint>

#include "veyra/input/ControllerTypes.h"
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/source/IFrameSource.h"
#include "veyra/source/XboxStats.h"

namespace veyra::source {

class IXboxSource : public IFrameSource {
public:
    virtual bool connect(XboxConnectDesc desc) = 0;
    virtual void cancel() = 0;
    virtual XboxStats stats() const = 0;
    virtual uint64_t skipped() const = 0;
    virtual uint64_t unitsReceived() const = 0;
    virtual sink::CaptureAudioState audioState() const = 0;
    virtual void setAudioGain(float value) = 0;
    virtual void setAudioSync(unsigned mode, int offset) = 0;
    virtual void videoPresented(double ptsMs, int64_t host100ns, bool sourceFrame = true) = 0;
    virtual void videoReset(bool resetAudio = true) = 0;
    virtual void controller(const input::ControllerState& state) = 0;
    virtual input::ControllerFeedback takeFeedback() = 0;
};

} // namespace veyra::source
