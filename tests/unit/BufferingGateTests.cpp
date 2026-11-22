#include "veyra/engine/BufferingGate.h"

#include <cstdio>

using veyra::engine::BufferingGate;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

namespace {
// The unit test only cares about level and timeout decisions; the grace period is tested separately.
BufferingGate makeGate() {
    veyra::engine::BufferingGateLimits limits;
    limits.warmupMs = 0;
    return BufferingGate(limits);
}
}  // namespace

int main() {
    // Just opened and PCM is still 0: do not judge the level inside the grace period (the misdiagnose on 2026-11-01 was here).
    {
        BufferingGate g;   // default 2000 ms grace
        check(g.update(true, 0.0, 0.0, 0) == BufferingGate::Action::None, "warm-up: an empty ring right after open is not dryness");
        check(!g.holding(), "warm-up: nothing is held while the ring fills");
        g.update(true, 0.0, 0.0, 1999);
        check(!g.holding(), "warm-up: still filling one millisecond before the grace period ends");
        g.update(true, 0.0, 0.0, 2100);
        check(g.holding(), "warm-up: past the grace period a dry ring does hold");
    }
    // A steady PCM ring is about 1 s (measured 987-1019 ms): the release threshold must stay below it or the hold never clears.
    {
        BufferingGate g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        check(g.holding(), "a dry start holds");
        g.update(true, 987.0, 2517.0, 100);
        check(!g.holding(), "the steady PCM level (987 ms) releases the hold");
    }
    // Steady state does not hold.
    {
        auto g = makeGate();
        check(g.update(true, 1000.0, 2517.0, 0) == BufferingGate::Action::None, "steady levels never ask for an action");
        check(!g.holding(), "steady levels do not hold");
    }
    // Falling below the enter threshold holds, but is not a reconnect yet.
    {
        auto g = makeGate();
        check(g.update(true, 250.0, 400.0, 1000) == BufferingGate::Action::None, "entering the hold is not an action");
        check(g.holding(), "below the enter thresholds the gate holds");
    }
    // Hysteresis: recovering between enter and exit still holds.
    {
        auto g = makeGate();
        g.update(true, 250.0, 400.0, 0);
        g.update(true, 800.0, 1200.0, 1000);
        check(g.holding(), "a partial recovery between enter and exit keeps holding");
    }
    // Both routes must reach the exit threshold to release.
    {
        auto g = makeGate();
        g.update(true, 250.0, 400.0, 0);
        g.update(true, 1100.0, 1600.0, 1000);
        check(!g.holding(), "reaching both exit thresholds releases the hold");
    }
    // A single dry route also holds: read-ahead queue dry, PCM still has a little left.
    {
        auto g = makeGate();
        g.update(true, 900.0, 100.0, 0);
        check(g.holding(), "a dry read-ahead queue alone is enough to hold");
    }
    // The 5 s window, the reconnect budget, giving up.
    {
        auto g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        check(g.update(true, 100.0, 100.0, 4999) == BufferingGate::Action::None, "nothing happens before 5 s");
        check(g.update(true, 100.0, 100.0, 5000) == BufferingGate::Action::Reconnect, "5 s of dryness asks for a reconnect");
        check(g.reconnects() == 1, "the reconnect is counted");
        check(g.update(true, 100.0, 100.0, 9000) == BufferingGate::Action::None, "a reconnect restarts the 5 s window");
        check(g.update(true, 100.0, 100.0, 10000) == BufferingGate::Action::Reconnect, "the budget allows a second reconnect");
        check(g.update(true, 100.0, 100.0, 15000) == BufferingGate::Action::GiveUp, "a spent budget gives up");
        check(g.gaveUp(), "giving up is sticky");
        check(g.update(true, 100.0, 100.0, 60000) == BufferingGate::Action::None, "a given-up gate stops asking");
    }
    // Data returns after a reconnect: release the hold and keep the reconnect count.
    {
        auto g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        g.update(true, 100.0, 100.0, 5000);
        check(g.update(true, 1100.0, 1600.0, 6000) == BufferingGate::Action::None, "recovery after a reconnect is silent");
        check(!g.holding() && !g.gaveUp(), "recovery clears both hold and give-up");
        check(g.reconnects() == 1, "recovery keeps the reconnect count");
    }
    // A reconnect that fails too: release the hold, keep the give-up sticky - the engine hints once and the picture is not frozen again.
    {
        auto g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        g.update(true, 100.0, 100.0, 5000);
        g.update(true, 100.0, 100.0, 10000);
        check(g.update(true, 100.0, 100.0, 15000) == BufferingGate::Action::GiveUp, "a third dry window gives up");
        g.abandon();
        check(!g.holding() && g.gaveUp(), "abandon releases the hold but keeps giving up");
    }
    // Re-arm after a seek or a track change: an empty ring refilled is not dryness, but the reconnect count is not reset.
    {
        BufferingGate g;   // default 2000 ms grace
        g.update(true, 1000.0, 2517.0, 0);
        g.rearm(1000);
        check(g.update(true, 0.0, 0.0, 1500) == BufferingGate::Action::None, "rearm: an empty ring after a seek is not dryness");
        check(!g.holding(), "rearm: nothing is held while the ring refills");
        g.update(true, 0.0, 0.0, 3100);
        check(g.holding(), "rearm: past the grace period a dry ring holds again");
    }
    // No audio (local file opened without an audio track, or the track was switched off) never holds.
    {
        auto g = makeGate();
        check(g.update(false, 0.0, 0.0, 0) == BufferingGate::Action::None, "no audio means no action");
        check(!g.holding(), "no audio never holds");
    }
    // The thresholds can be changed from outside: field tuning against the network.
    {
        veyra::engine::BufferingGateLimits limits;
        limits.warmupMs = 0;
        limits.enterPcmMs = 150.0;
        limits.exitPcmMs = 400.0;
        BufferingGate g(limits);
        g.update(true, 200.0, 5000.0, 0);
        check(!g.holding(), "a lowered enter threshold ignores 200 ms of PCM");
        g.update(true, 100.0, 5000.0, 1000);
        check(g.holding(), "the lowered threshold still holds at 100 ms");
    }

    if (failures) std::printf("buffering gate: %d failure(s)\n", failures);
    else std::printf("buffering gate: all checks passed\n");
    return failures ? 1 : 0;
}
