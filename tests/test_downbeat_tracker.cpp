#include "analysis/DownbeatTracker.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    DownbeatTracker tracker;
    tracker.reset(4);

    // Simulate 4/4 guitar phrasing where beat 1 is consistently stronger.
    for (int bar = 0; bar < 8; ++bar) {
        tracker.observeBeat(1.00f);
        tracker.observeBeat(0.35f);
        tracker.observeBeat(0.55f);
        tracker.observeBeat(0.32f);
    }

    auto state = tracker.state();
    assert(state.meterNumerator == 4);
    assert(state.downbeatConfidence > 0.55f);
    assert(state.nextBeatInBar == 1);

    // A single anomalous strong offbeat must not rotate the bar immediately.
    tracker.observeBeat(0.40f);
    tracker.observeBeat(1.00f);
    tracker.observeBeat(0.35f);
    tracker.observeBeat(0.30f);
    state = tracker.state();
    assert(state.downbeatConfidence > 0.40f);

    tracker.reset(3);
    for (int bar = 0; bar < 8; ++bar) {
        tracker.observeBeat(0.95f);
        tracker.observeBeat(0.30f);
        tracker.observeBeat(0.42f);
    }
    state = tracker.state();
    assert(state.meterNumerator == 3);
    assert(state.downbeatConfidence > 0.50f);
    assert(state.nextBeatInBar == 1);
}
