#include "core/MeterProfile.h"
#include "core/GrooveGenerator.h"
#include <array>
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    const auto three = MeterProfile::forMeter(3, 4);
    assert(three.valid);
    assert(three.groupCount == 1);
    assert(three.groupStarts[0] == 0);

    const auto sixEight = MeterProfile::forMeter(6, 8);
    assert(sixEight.valid);
    assert(sixEight.groupCount == 2);
    assert(sixEight.groupStarts[0] == 0);
    assert(sixEight.groupStarts[1] == 3);
    assert(sixEight.backbeat[3]);

    const auto five = MeterProfile::forMeter(5, 4);
    assert(five.valid);
    assert(five.groupCount == 2);
    assert(five.groupStarts[0] == 0);
    assert(five.groupStarts[1] == 3);

    const auto seven = MeterProfile::forMeter(7, 8);
    assert(seven.valid);
    assert(seven.groupCount == 3);
    assert(seven.groupStarts[0] == 0);
    assert(seven.groupStarts[1] == 2);
    assert(seven.groupStarts[2] == 4);

    GrooveGenerator generator;
    GrooveContext context;
    context.sampleRate = 48000.0;
    context.bpm = 120.0;
    context.numerator = 6;
    context.denominator = 8;
    context.intensity = 0.6f;
    context.seed = 42;

    auto style = Style::basicRock();
    style.closedHatProbability = 1.0f;
    style.openHatProbability = 0.0f;
    style.rideProbability = 0.0f;
    style.snareBackbeat = 1.0f;
    style.extraKickProbability = 0.0f;
    style.ghostSnareProbability = 0.0f;

    std::array<DrumEvent, 128> events{};
    const auto count = generator.generateBar(style, context, events);

    // At 120 BPM in 6/8, each eighth-note unit is 12000 samples and the bar is 72000.
    bool snareOnSecondGroup = false;
    int hats = 0;
    for (std::size_t i = 0; i < count; ++i) {
        assert(events[i].sampleOffset >= 0);
        assert(events[i].sampleOffset < 72000);
        if (events[i].instrument == DrumInstrument::Snare && events[i].sampleOffset == 36000)
            snareOnSecondGroup = true;
        if (events[i].instrument == DrumInstrument::ClosedHat)
            ++hats;
    }
    assert(snareOnSecondGroup);
    assert(hats == 6); // eighth-note pulse, not twelve sixteenth hats
}
