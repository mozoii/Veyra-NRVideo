#pragma once

// Buffering gate: the shared "should we hold together" decision for audio and video.
//
// Background (2026-11-01 field case): the audio track of a Bilibili source is a separate CDN request while the picture goes through the demux session.
// When the audio connection drops, the demux side queue stays full, so the picture keeps running while the sound stops -
// the user sees "frozen + silent" and the log only shows the audio side's own underrun counter.
// The video side already has Phase::Buffering; the audio side had no matching signal, and this class fills in that half.
//
// Pure logic: no threads, no network, no clock of its own. The engine feeds the two
// water levels and a millisecond timestamp once per loop turn; the gate answers
// "should we hold now", and - when the hold lasts too long - "reconnect" or "give up".
namespace veyra::engine {

struct BufferingGateLimits {
    // Audio water level in milliseconds. Falling below enter holds; only returning to exit releases. Without this hysteresis
    // the thresholds flicker several times a second around the line and the sound breaks up.
    //
    // Exit must sit well below the **steady** level, otherwise the hold never clears: the PCM ring
    // holds about one second, idles between 987 and 1019 ms, and an exit of 1000 would mean "always a little short".
    // 2026-11-01 field case: a pcmMs=0 at open triggered the hold, then PCM settled at 987 → the gate froze
    // for 5 s → a spurious reconnect → the sound was gone entirely; "continue last playback" looked broken.
    double enterPcmMs = 300.0;
    double enterQueuedMs = 500.0;
    double exitPcmMs = 500.0;
    double exitQueuedMs = 1500.0;
    // Grace period after opening: the PCM ring is still empty (just opened, not prefilled yet),
    // so no water level is judged. Without it every playback starts by declaring the sound dry.
    long long warmupMs = 2000;
    // This long still held: the connection is dead, not slow.
    long long reconnectAfterMs = 5000;
    // How many reconnects one playback may take; when the budget is spent, give up and hand the choice back to the user.
    int reconnectBudget = 2;
};

class BufferingGate {
public:
    enum class Action { None, Reconnect, GiveUp };

    explicit BufferingGate(BufferingGateLimits limits = BufferingGateLimits{}) : limits_(limits) {}

    // Called once per loop turn. audioAvailable=false (local file opened without audio, or the track was closed) means only the video side is judged,
    // and the gate itself stays "not holding".
    Action update(bool audioAvailable, double pcmMs, double queuedMs, long long nowMs) {
        // The first call records the start; nothing is held during the grace period: a zero now means "not filled yet", not "run dry".
        if (!armed_) {
            armed_ = true;
            armedAtMs_ = nowMs;
            holding_ = false;
            gaveUp_ = false;
            sinceMs_ = nowMs;
        }
        if (nowMs - armedAtMs_ < limits_.warmupMs) {
            holding_ = false;
            return Action::None;
        }
        if (!audioAvailable) {
            holding_ = false;
            gaveUp_ = false;
            sinceMs_ = nowMs;
            return Action::None;
        }
        const bool low = holding_ ? (pcmMs < limits_.exitPcmMs || queuedMs < limits_.exitQueuedMs)
                                  : (pcmMs < limits_.enterPcmMs || queuedMs < limits_.enterQueuedMs);
        if (low && !holding_) {
            holding_ = true;
            sinceMs_ = nowMs;
            gaveUp_ = false;
        } else if (!low && holding_) {
            holding_ = false;
            gaveUp_ = false;
        }
        if (!holding_ || gaveUp_) return Action::None;
        if (nowMs - sinceMs_ < limits_.reconnectAfterMs) return Action::None;
        if (reconnects_ < limits_.reconnectBudget) {
            ++reconnects_;
            sinceMs_ = nowMs;   // give the reconnect a fresh wait window
            return Action::Reconnect;
        }
        gaveUp_ = true;
        return Action::GiveUp;
    }

    // Called when the reconnect also fails: release the hold but keep "gave up", so the engine can
    // tell the user once and the picture can continue (silent). Only the hold is cleared, not the give-up,
    // so the gate does not restart from square one.
    // Called when reopening failed as well: release the hold but keep "gave up", so the
    // engine can tell the user once and the picture is not frozen for good.
    void abandon() { holding_ = false; gaveUp_ = true; }

    // Called after a seek or a track change: the buffer empties before it refills and that stretch
    // must not read as a dead stream. Only the grace period and the hold are reset, **not** the reconnect count -
    // otherwise one broken stream could buy itself an unlimited number of reconnects.
    // Called after a seek or a track change: the buffer empties before it refills and
    // that stretch must not read as a dead stream. The reconnect count is kept, so a
    // broken stream cannot buy itself an unlimited number of reconnects.
    void rearm(long long nowMs) {
        armed_ = true;
        armedAtMs_ = nowMs;
        holding_ = false;
        gaveUp_ = false;
        sinceMs_ = nowMs;
    }

    bool holding() const { return holding_; }
    bool gaveUp() const { return gaveUp_; }
    int reconnects() const { return reconnects_; }
    const BufferingGateLimits& limits() const { return limits_; }

private:
    BufferingGateLimits limits_{};
    bool armed_ = false;
    bool holding_ = false;
    bool gaveUp_ = false;
    int reconnects_ = 0;
    long long armedAtMs_ = 0;
    long long sinceMs_ = 0;
};

}  // namespace veyra::engine
