#include "analysis/SessionMemory.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    SessionMemory memory;
    memory.reset();

    SessionBarObservation bar;
    bar.tempoBpm = 120.0;
    bar.intensity = 0.40f;
    bar.activity = 0.75f;

    for (int i = 0; i < 16; ++i) {
        bar.tempoBpm = 120.0 + 0.25 * i;
        bar.intensity = (i % 4 < 2) ? 0.40f : 0.70f;
        bar.phraseBoundary = (i == 3 || i == 7 || i == 11 || i == 15);
        bar.manualFillRequested = (i == 6 || i == 14);
        bar.fillOccurred = bar.manualFillRequested || bar.phraseBoundary;
        bar.activity = (i >= 8 && i <= 10) ? 0.02f : 0.75f;
        memory.observeBar(bar);
    }

    const auto s = memory.snapshot();
    assert(s.barCount == 16);
    assert(s.averageTempoBpm > 121.0 && s.averageTempoBpm < 123.0);
    assert(s.averageTempoDriftPerBar > 0.20 && s.averageTempoDriftPerBar < 0.30);
    assert(s.intensityVolatility > 0.10f);
    assert(s.averagePhraseBars > 3.5f && s.averagePhraseBars < 4.5f);
    assert(s.longestPauseBars >= 3);
    assert(s.manualFillRatePerBar > 0.10f);
    assert(s.fillRatePerBar >= s.manualFillRatePerBar);

    const auto r = memory.recommendations();
    assert(r.confidence > 0.5f);
    assert(r.suggestedPhraseBars == 4);
    assert(r.fillBiasAdjustment > 0.0f);
    assert(r.dynamicSensitivity > 1.0f);

    memory.reset();
    assert(memory.snapshot().barCount == 0);

    // A very steady player should not be pushed toward aggressive adaptation.
    for (int i = 0; i < 12; ++i) {
        bar = {};
        bar.tempoBpm = 100.0;
        bar.intensity = 0.5f;
        bar.activity = 0.8f;
        bar.phraseBoundary = (i == 5 || i == 11);
        memory.observeBar(bar);
    }
    const auto steady = memory.recommendations();
    assert(steady.suggestedPhraseBars == 6);
    assert(steady.dynamicSensitivity <= 1.0f);
}
