#include "core/FillGenerator.h"
#include <array>
#include <cassert>

int main() {
    using namespace robodrummer;

    assert(fillLengthBeats(FillLength::OneBeat, 4) == 1.0);
    assert(fillLengthBeats(FillLength::TwoBeats, 4) == 2.0);
    assert(fillLengthBeats(FillLength::OneBar, 4) == 4.0);
    assert(fillLengthBeats(FillLength::TwoBars, 4) == 8.0);
    assert(fillLengthBeats(FillLength::LongTransition, 3) == 6.0);

    FillGenerator generator;
    FillContext context;
    context.sampleRate = 48000.0;
    context.bpm = 120.0;
    context.numerator = 4;
    context.denominator = 4;
    context.strength = 0.75f;

    std::array<DrumEvent, 128> events{};
    context.length = FillLength::OneBeat;
    const auto oneBeat = generator.generate(context, events);
    assert(oneBeat >= 2);

    context.length = FillLength::OneBar;
    const auto oneBar = generator.generate(context, events);
    assert(oneBar > oneBeat);

    context.length = FillLength::TwoBars;
    const auto twoBars = generator.generate(context, events);
    assert(twoBars > oneBar);

    context.length = FillLength::LongTransition;
    const auto transition = generator.generate(context, events);
    assert(transition > oneBar);
    bool sawCrash = false;
    for (std::size_t i = 0; i < transition; ++i)
        sawCrash |= events[i].instrument == DrumInstrument::Crash;
    assert(sawCrash);
}
