#include "plugin/JamEngine.h"
#include <array>
#include <cassert>
int main() {
    using namespace robodrummer;
    JamEngine engine;
    engine.prepare(48000.0);
    engine.setTempo(120.0);
    engine.setMeter(4,4);
    std::array<DrumEvent, 128> events{};
    const auto n = engine.processBlock(96000, events.data(), events.size());
    assert(n > 0);
    bool sawKick0 = false, sawSnareBeat2 = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (events[i].instrument == DrumInstrument::Kick && events[i].sampleOffset == 0) sawKick0 = true;
        if (events[i].instrument == DrumInstrument::Snare && events[i].sampleOffset == 24000) sawSnareBeat2 = true;
    }
    assert(sawKick0);
    assert(sawSnareBeat2);
    engine.apply(MidiCommand::Stop);
    assert(engine.processBlock(512, events.data(), events.size()) == 0);
}
