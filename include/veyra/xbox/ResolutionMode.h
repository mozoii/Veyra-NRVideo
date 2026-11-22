// SPDX-License-Identifier: GPL-3.0-only
// Xbox home-streaming quality tiers: Auto / 720p / 720p HQ / 1080p / 1080p HQ / 1440p.
//
// Two things are pinned down: the advertised dimensions and the control-channel
// alias (from the official 1440p announcement and the third-party clients), and
// the tier list itself. Everything else on the wire — the spoofed osName, the
// capability bitrate and the H.264 level per tier — is PROVISIONAL pending the
// official-client capture recorded in docs/xbox-1440p-negotiation.md. Do not
// present those as verified.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace veyra::xbox {

enum class ResolutionMode : uint8_t { Auto, P720, P720HQ, P1080, P1080HQ, P1440 };

struct ResolutionDesc {
    ResolutionMode mode;
    const char* name;            // settings spelling, e.g. "p1440"
    const char* label;           // UI label, e.g. "1440p"
    const char* alias;           // control-channel resolutionAlias
    unsigned width = 0;          // advertised display width in pixels
    unsigned height = 0;         // advertised display height in pixels
    const char* osName = "";     // spoofed os.name / settings.osName (PROVISIONAL)
    unsigned maxBitrateKbps = 0; // clientdevicecapabilities maxBitrateKbps (PROVISIONAL)
    int fps = 60;                // advertised supportsFps (PROVISIONAL)
};

// Stable order for UI and iteration. Auto is the default and lets the service
// pick the highest resolution the device and network support.
inline constexpr std::array<ResolutionDesc, 6> kResolutionModes = {{
    // Auto: the service picks; keep the 1080p advertisement and the "Auto" alias.
    {ResolutionMode::Auto,   "auto",   "Auto",    "Auto",   1920, 1080, "windows", 20000, 60},
    {ResolutionMode::P720,   "p720",   "720p",    "720",    1280,  720, "android", 10000, 60},
    {ResolutionMode::P720HQ, "p720hq", "720p HQ", "720HQ",  1280,  720, "android", 10000, 60},
    {ResolutionMode::P1080,  "p1080",  "1080p",   "1080",   1920, 1080, "windows", 20000, 60},
    {ResolutionMode::P1080HQ,"p1080hq","1080p HQ","1080HQ", 1920, 1080, "tizen",   30000, 60},
    {ResolutionMode::P1440,  "p1440",  "1440p",   "1440",   2560, 1440, "windows", 30000, 60},
}};

inline const ResolutionDesc& resolutionDesc(ResolutionMode mode) {
    for (const auto& d : kResolutionModes) if (d.mode == mode) return d;
    return kResolutionModes[0];
}

inline ResolutionMode resolutionModeFromName(const std::string& name, ResolutionMode fallback = ResolutionMode::Auto) {
    for (const auto& d : kResolutionModes)
        if (name == d.name) return d.mode;
    return fallback;
}

inline const char* resolutionModeName(ResolutionMode mode) { return resolutionDesc(mode).name; }

inline std::string resolutionModeList() {
    std::string out;
    for (size_t i = 0; i < kResolutionModes.size(); ++i) {
        if (i) out += ',';
        out += kResolutionModes[i].name;
    }
    return out;
}

} // namespace veyra::xbox
