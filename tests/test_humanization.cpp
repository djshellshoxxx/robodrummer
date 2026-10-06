#include "core/GrooveGenerator.h"
#include <array>
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    GrooveGenerator generator;
    GrooveContext context;
    context.sampleRate = 48000.0;
    context.bpm = 120.0;
    context.numerator = 4;
    context.denominator = 4;
    context.intensity = 0.65f;
    context.seed = 0x1234abcd;

    Style rigid = Style::basicRock();
    rigid.humanizeMs = 0.0f;
    std::array<DrumEvent, 128> baseline{};
    const auto baselineCount = generator.generateBar(rigid, context, baseline);
    assert(baselineCount > 0);

    Style human = rigid;
    human.humanizeMs = 10.0f;
    std::array<DrumEvent, 128> a{};
    std::array<DrumEvent, 128> b{};
    const auto countA = generator.generateBar(human, context, a);
    const auto countB = generator.generateBar(human, context, b);

    assert(countA == baselineCount);
    assert(countB == countA);

    const int maxTimingDelta = static_cast<int>(std::ceil(context.sampleRate * human.humanizeMs / 1000.0));
    const int barSamples = static_cast<int>(std::llround(context.sampleRate * 60.0 / context.bpm * context.numerator));
    bool timingChanged = false;
    bool velocityChanged = false;

    for (std::size_t i = 0; i < countA; ++i) {
        assert(a[i].instrument == baseline[i].instrument);
        assert(a[i].instrument == b[i].instrument);
        assert(a[i].sampleOffset == b[i].sampleOffset);
        assert(std::abs(a[i].velocity - b[i].velocity) < 0.000001f);
        assert(a[i].sampleOffset >= 0 && a[i].sampleOffset < barSamples);
        assert(a[i].velocity >= 0.0f && a[i].velocity <= 1.0f);

        const int delta = std::abs(a[i].sampleOffset - baseline[i].sampleOffset);
        assert(delta <= maxTimingDelta);
        timingChanged |= delta > 0;
        velocityChanged |= std::abs(a[i].velocity - baseline[i].velocity) > 0.0001f;
    }

    // The structural downbeat remains exact; microtiming is applied to later events.
    assert(baseline[0].sampleOffset == 0);
    assert(a[0].sampleOffset == 0);
    assert(timingChanged);
    assert(velocityChanged);

    // Genre amount is meaningful: zero humanization produces the unchanged groove.
    std::array<DrumEvent, 128> rigidAgain{};
    const auto rigidCount = generator.generateBar(rigid, context, rigidAgain);
    assert(rigidCount == baselineCount);
    for (std::size_t i = 0; i < rigidCount; ++i) {
        assert(rigidAgain[i].sampleOffset == baseline[i].sampleOffset);
        assert(std::abs(rigidAgain[i].velocity - baseline[i].velocity) < 0.000001f);
    }
}
