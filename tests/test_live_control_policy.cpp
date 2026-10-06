#include "core/LiveControlPolicy.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;
    assert(std::abs(applyIntensityCommand(0.5f, MidiCommand::IntensityUp) - 0.6f) < 0.0001f);
    assert(std::abs(applyIntensityCommand(0.5f, MidiCommand::IntensityDown) - 0.4f) < 0.0001f);
    assert(applyIntensityCommand(0.95f, MidiCommand::IntensityUp) == 1.0f);
    assert(applyIntensityCommand(0.05f, MidiCommand::IntensityDown) == 0.0f);
    assert(applyIntensityCommand(0.5f, MidiCommand::Fill) == 0.5f);
}
