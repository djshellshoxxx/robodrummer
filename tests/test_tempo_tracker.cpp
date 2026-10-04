#include "analysis/TempoTracker.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    TempoTracker tracker;
    tracker.reset();
    for (int i = 0; i < 20; ++i) tracker.addOnset(i * 0.5, 1.0f);
    auto best = tracker.best();
    assert(std::abs(best.bpm - 120.0) <= 1.0);
    assert(best.confidence > 0.35f);

    tracker.reset();
    double t = 0.0;
    for (int i = 0; i < 24; ++i) {
        if (i != 9 && i != 17) tracker.addOnset(t, 1.0f);
        if (i == 12) tracker.addOnset(t + 0.25, 0.35f);
        t += 0.5;
    }
    best = tracker.best();
    assert(std::abs(best.bpm - 120.0) <= 1.5);
    assert(best.confidence > 0.20f);

    tracker.reset();
    t = 0.0;
    for (int i = 0; i < 24; ++i) {
        if (!(i > 0 && i % 5 == 0)) tracker.addOnset(t, 1.0f);
        t += 0.5;
    }
    best = tracker.best();
    assert(std::abs(best.bpm - 120.0) <= 1.5);

    tracker.reset();
    t = 0.0;
    for (int i = 0; i < 8; ++i) { tracker.addOnset(t, 1.0f); t += 0.5; }
    for (int i = 0; i < 16; ++i) { tracker.addOnset(t, 1.0f); t += 60.0 / 132.0; }
    best = tracker.best();
    assert(std::abs(best.bpm - 132.0) <= 2.0);

    const auto top = tracker.topHypotheses();
    assert(std::abs(top[0].bpm - 132.0) <= 2.0);
}
