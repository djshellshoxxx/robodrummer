#include "analysis/SessionMemoryPolicy.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    JamBrainSettings base;
    base.phraseBars = 8;
    base.fillBias = 0.0f;
    base.buildThreshold = 0.10f;
    base.releaseThreshold = 0.10f;

    SessionMemoryRecommendations weak;
    weak.confidence = 0.3f;
    weak.suggestedPhraseBars = 4;
    weak.fillBiasAdjustment = 0.15f;
    weak.dynamicSensitivity = 1.3f;

    auto low = SessionMemoryPolicy::apply(base, 0.60f, weak, true);
    assert(low.brainSettings.phraseBars == 8);
    assert(std::abs(low.brainSettings.fillBias) < 0.0001f);
    assert(std::abs(low.dynamicFollow - 0.60f) < 0.0001f);

    SessionMemoryRecommendations trusted = weak;
    trusted.confidence = 0.8f;

    auto learned = SessionMemoryPolicy::apply(base, 0.60f, trusted, true);
    assert(learned.brainSettings.phraseBars >= 4 && learned.brainSettings.phraseBars < 8);
    assert(learned.brainSettings.fillBias > 0.0f);
    assert(learned.brainSettings.buildThreshold < base.buildThreshold);
    assert(learned.dynamicFollow > 0.60f && learned.dynamicFollow <= 1.0f);

    auto disabled = SessionMemoryPolicy::apply(base, 0.60f, trusted, false);
    assert(disabled.brainSettings.phraseBars == base.phraseBars);
    assert(disabled.brainSettings.fillBias == base.fillBias);
    assert(disabled.dynamicFollow == 0.60f);
}
