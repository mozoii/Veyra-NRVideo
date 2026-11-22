#pragma once
// Counters a live source publishes to the engine and the player bar. The PC and
// Xbox streaming sources fill them; the field names predate the capture-card
// removal and are kept so the diagnostics format stays stable.
#include <cstdint>

namespace veyra::source {
struct CaptureMetrics {
    uint64_t received=0, delivered=0, dropped=0;
    double callbackFps=0, readAgeMs=0, frameAgeMs=0;
    bool sampledBlack=false; // Sparse source pixels, not input signal detection.
};
}
