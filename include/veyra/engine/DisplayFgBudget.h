// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <cmath>
#include <cstdint>

namespace veyra::engine {
// Live frame-generation output hint (claude/latency-min-20261013).
//
// A panel shows at most one new picture per refresh. Generated pictures above
// that rate are replaced before scanout (Automatic) or dropped/waited for
// (VSync), and every extra multiplier step makes the real frame wait longer:
// graph FG shows it (M-1)/M of a source interval after it arrived. The
// multiplier stays the user's choice (decision 2026-10-14: no automatic
// lowering); for live sources the status line suggests a better one:
//   1. the output fits the refresh (content x M <= 1.02 x refresh): nothing;
//   2. the source alone already fills the panel (>= 0.97 x refresh, e.g.
//      60 fps on 60 Hz): turn generation off - no generated picture can add a
//      refresh, and real frames would present without waiting behind one;
//   3. otherwise the largest selectable lower multiplier whose output fits;
//   4. if none fits but the source is slower than the panel (60 fps on a
//      100 Hz panel): Automatic still shows more distinct pictures per refresh
//      with 2X, so suggest 2X only when more was requested; under VSync the
//      surplus waits or is dropped, so suggest turning generation off.
inline constexpr double kDisplayFgFitTolerance=1.02;
inline constexpr double kDisplayFgFillRatio=0.97;

// Multipliers the user can pick for the backend (fgMultiplierChoices: XeSS
// stops at 4X, FSR offers 2X only). Lower values than an accepted request
// are always within the runtime limits.
constexpr bool displayFgMultiplierSelectable(FrameGenerationBackend backend,unsigned multiplier){
    switch(backend){
    case FrameGenerationBackend::Dlss: return multiplier==2||multiplier==3||multiplier==4||multiplier==6;
    case FrameGenerationBackend::XeSS: return multiplier>=2&&multiplier<=4;
    case FrameGenerationBackend::Anr: return multiplier>=2&&multiplier<=6;
    case FrameGenerationBackend::Vfg: return multiplier>=2&&multiplier<=8;
    case FrameGenerationBackend::Fsr:
    case FrameGenerationBackend::Fsr4:
    case FrameGenerationBackend::AmdFrc: return multiplier==2;
    }
    return false;
}

// "Auto" multiplier: pick the tier from the source frame rate and the target ceiling.
// User definition (2026-10-24): 24fps with a 120 ceiling picks 5X, 30fps picks 4X, and so on;
// when the backend has no such tier the next one up is used (DLSS has no 5X, so 24fps picks 6X=144,
// and the output ceiling clips it back to 120).
// Unlike displayFgSuggestion: that only ever recommends lower, never higher; this allows taking the next tier up.
// The tolerance is kDisplayFgFitTolerance (2%): 23.976x5=119.88 counts as reaching 120, so 5X is picked over 6X.
// ceiling: the largest multiplier this backend can actually offer right now (GPU capability, DLSS 6 / VFG 8 / FSR 2).
// Same as FrameBatch::Capacity: the absolute cap on the multiplier is 8 (at most 1 real frame + 7 generated frames per batch).
inline constexpr unsigned kDisplayFgMultiplierCeiling=8;
// When even the lowest tier (2X) overshoots the ceiling, only add it if the source rate is far enough below the target: gain = ceiling - source,
// waste = 2x source - ceiling. Equality means source = ceiling x 2/3, and above that ratio no patch is added
// (ceiling 120 / source 100, ceiling 144 / source 100 both fall on this side -> close it off).
// This is the one place that needs a "save GPU" judgement: when 2X does not fit at all, most of what is patched gets clipped.
inline constexpr double kDisplayFgAutoCloseAbove=2.0/3.0;
// Returns 0 = frame rate unknown/no usable tier (the caller keeps the current tier), 1 = turn patching off, otherwise the multiplier.
//
// Tier rule (user 2026-10-24): among the tiers that work, pick the smallest one that reaches need,
// so source fps x multiplier lands just on the output ceiling; when the backend lacks that tier, take the next one up. No extra "save GPU"
// step down - 24/100 should get 5X (120 frames, 20 clipped by the ceiling), 100/240 should get 3X, both are the formula's natural result, and the user explicitly asked for these two cells to follow the formula.
inline unsigned displayFgAutoMultiplier(FrameGenerationBackend backend,double contentFps,double targetFps,unsigned ceiling=kDisplayFgMultiplierCeiling){
    if(!std::isfinite(contentFps)||!std::isfinite(targetFps)||contentFps<=0||targetFps<=0||ceiling<2)return 0;
    // The source rate already reaches or exceeds the ceiling: patching has no gain, close it off.
    if(contentFps>=targetFps/kDisplayFgFitTolerance)return 1;
    const double doubled=contentFps*2.0;
    // Even 2X does not fit the ceiling: only add it when the source rate is far below the target, otherwise close it off
    // (most of what was patched would be clipped frames, wasting GPU).
    if(displayFgMultiplierSelectable(backend,2)&&doubled>targetFps*kDisplayFgFitTolerance){
        if(contentFps>targetFps*kDisplayFgAutoCloseAbove)return 1;
        return 2u;
    }
    const auto need=unsigned(std::ceil(targetFps/(contentFps*kDisplayFgFitTolerance)-1e-9));
    unsigned best=0;
    for(unsigned m=2;m<=ceiling&&m<=kDisplayFgMultiplierCeiling;++m){
        if(!displayFgMultiplierSelectable(backend,m))continue;
        best=m;                 // ascending pass, so the last usable tier is the largest one available
        if(m>=need)return m;    // the first tier that reaches need is the smallest one that suffices
    }
    // No tier reaches need (e.g. DLSS ceiling 6 against 24fps/200 target): take the largest one available,
    // and the output ceiling still clips the extra frames back to the target.
    return best;
}

// 0 = no suggestion, 1 = turn generation off, otherwise a lower multiplier.
// queued: presents above the refresh wait or are dropped (VSync) instead of
// being replaced (Automatic). Unknown rates suggest nothing.
inline unsigned displayFgSuggestion(FrameGenerationBackend backend,unsigned requested,double contentFps,double refreshHz,bool queued=false){
    if(requested<2||!std::isfinite(contentFps)||!std::isfinite(refreshHz)||contentFps<=0||refreshHz<=0)return 0;
    if(contentFps*requested<=refreshHz*kDisplayFgFitTolerance)return 0;
    if(contentFps>=refreshHz*kDisplayFgFillRatio)return 1;
    for(unsigned m=requested-1;m>=2;--m)
        if(displayFgMultiplierSelectable(backend,m)&&contentFps*m<=refreshHz*kDisplayFgFitTolerance)return m;
    if(queued)return 1;
    return requested>2&&displayFgMultiplierSelectable(backend,2)?2:0;
}

// A changed suggestion shows only after it held for the whole hold time, so a
// content rate wobbling around a boundary (30/40 fps games, the dedup
// estimate) or a brief refresh change never makes the hint flicker; a
// candidate that changes again restarts the hold. The first decision after
// reset() applies at once. Units: 100 ns.
class DisplayFgBudgetGate {
public:
    explicit DisplayFgBudgetGate(int64_t hold100ns=15000000):hold_(hold100ns){}
    void reset(){valid_=pendingValid_=false;}
    // Returns true when the applied decision changed.
    bool update(unsigned decision,int64_t now){
        if(!valid_){applied_=decision;valid_=true;pendingValid_=false;return true;}
        if(decision==applied_){pendingValid_=false;return false;}
        if(!pendingValid_||pending_!=decision){pending_=decision;since_=now;pendingValid_=true;return false;}
        if(now-since_<hold_)return false;
        applied_=decision;pendingValid_=false;return true;
    }
    bool valid()const{return valid_;}
    unsigned applied()const{return applied_;}
private:
    int64_t hold_,since_=0;
    unsigned applied_=0,pending_=0;
    bool valid_=false,pendingValid_=false;
};
}
