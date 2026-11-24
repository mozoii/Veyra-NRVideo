// SPDX-License-Identifier: GPL-3.0-only
// Transport/ICE profile of an xHome session, ported from PR #289 by RyanBB
// (@RyanLookchard) and adapted to this fork's ResolutionMode tier table.
// The tier itself stays in ResolutionMode.h; this file holds only the transport
// knobs the user can enable, all defaulting to the historical behaviour.
// Pure data and helpers only; no service calls.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace veyra::xbox {

struct IceServerConfig {
    std::string url;        // "stun:host[:port]" or "turn:host[:port]"
    std::string username;   // TURN only
    std::string credential; // TURN only
};

// Hardware decode API for the H.264 stream. Auto = D3D12VA (the historical path);
// D3D11VA = the DXVA path the HEVC stream already uses, clean on Intel iGPUs
// whose D3D12VA H.264 output is silently damaged.
enum class DecodeBackend : uint8_t { Auto = 0, D3D11VA = 1 };

struct TransportProfile {
    int videoBitrateKbps = 0;              // 0 = no b=AS line; the console decides
    int audioBitrateKbps = 0;              // 0 = no audio b=AS line
    bool useStun = false;                  // public STUN list below; off keeps host-only gathering
    bool fullTeredo = false;               // false = the historical two IPv4 candidates
    DecodeBackend decodeBackend = DecodeBackend::Auto;
    std::vector<IceServerConfig> iceServers; // user STUN/TURN, appended after the defaults
};

// Parses "stun:host[:port]" / "turn:host[:port]" (default port 3478). Bracketed IPv6
// literals are accepted. Returns false for an unknown scheme or an empty host.
bool parseIceServerUrl(const std::string& url, std::string* host, uint16_t* port, bool* turn);

// The public STUN list the reference clients use (facts, not code). Order preserved.
inline const std::vector<IceServerConfig>& defaultStunServers() {
    static const std::vector<IceServerConfig> servers = {
        {"stun:worldaz.relay.teams.microsoft.com:3478", {}, {}},
        {"stun:stun.l.google.com:19302", {}, {}},
        {"stun:stun1.l.google.com:19302", {}, {}},
        {"stun:relay1.expressturn.com:3478", {}, {}},
        {"stun:relay2.expressturn.com:3478", {}, {}},
        {"stun:stun.kinesisvideo.us-east-1.amazonaws.com:443", {}, {}},
        {"stun:stun.douyucdn.cn:18000", {}, {}},
    };
    return servers;
}

} // namespace veyra::xbox
