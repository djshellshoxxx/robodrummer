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

    const auto top = tracker.topHypotheses();
    assert(std::abs(top[0].bpm - 120.0) <= 1.5);
    assert(top[0].confidence >= top[1].confidence * 0.5f);
}
