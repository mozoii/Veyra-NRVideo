// SPDX-License-Identifier: GPL-3.0-only
// Shared types of the Xbox transport engines. Kept separate from the source
// classes so IXboxSource can include them without a circular dependency.
// Ported from PR #289 by RyanBB (@RyanLookchard), adapted to this fork's
// ResolutionMode tier table, input:: controller types and V1-default engine.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "veyra/xbox/ResolutionMode.h"
#include "veyra/xbox/StreamProfile.h"

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace veyra::xbox { class Account; }

namespace veyra::source {

struct XboxConnectDesc {
    std::shared_ptr<xbox::Account> account;
    std::string serverId;            // the console
    std::string consoleName;         // for messages
    bool gamepad = true;             // forward the PC's controller
    std::string controllerGuid;      // SDL GUID of the chosen pad; empty = Auto
    bool softwareDecode = false;    // opt-in workaround for silent hardware decode corruption
    std::string locale = "en-US";
    xbox::ResolutionMode resolutionMode = xbox::ResolutionMode::Auto;
    xbox::TransportProfile transport; // STUN, Teredo, bitrate hints, decode backend
    int transportEngine = 1;         // 1 = V1 native libdatachannel (default), 2 = V2 WebView2
    std::shared_ptr<ID3D12Device> decodeDevice;
    std::shared_ptr<ID3D12CommandQueue> decodeQueue;
    const std::atomic<bool>* stopRequested = nullptr; // engine owner outlives the source
};

struct XboxStats {
    enum class State : uint8_t { Idle, Starting, Connecting, Streaming, Ended, Failed };
    State state = State::Idle;
    std::wstring message;           // progress while starting; the reason when Ended or Failed
    std::string disconnectReason;
    bool reconnectable = false;
    uint32_t width = 0, height = 0;
    bool hardwareDecode = false;
    uint64_t units = 0, decoded = 0, dropped = 0, decodeErrors = 0, keyframeRequests = 0;
    uint64_t nacks = 0, recovered = 0, incompleteDrops = 0; // RTP loss recovery (V1)
    double receivedFps = 0, decodedFps = 0, videoMbps = 0;
    double decodeMs = 0, rttMs = 0;
};

} // namespace veyra::source
