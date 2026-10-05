#include "plugin/JamEngine.h"
#include <array>
#include <cassert>

int main() {
    using namespace robodrummer;
    JamEngine engine;
    engine.prepare(48000.0);
    engine.setTempo(120.0);
    engine.setMeter(4, 4);

    std::array<DrumEvent, 256> events{};

    engine.requestFill(0.20f);
    auto n = engine.processBlock(96000, events.data(), events.size());
    std::size_t weakFillVoices = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (events[i].sampleOffset >= 72000 &&
            (events[i].instrument == DrumInstrument::Snare ||
             events[i].instrument == DrumInstrument::HighTom ||
             events[i].instrument == DrumInstrument::MidTom ||
             events[i].instrument == DrumInstrument::FloorTom))
            ++weakFillVoices;
    }

    engine.resetPhase();
    engine.requestFill(1.0f);
    n = engine.processBlock(96000, events.data(), events.size());
    std::size_t strongFillVoices = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (events[i].sampleOffset >= 72000 &&
            (events[i].instrument == DrumInstrument::Snare ||
             events[i].instrument == DrumInstrument::HighTom ||
             events[i].instrument == DrumInstrument::MidTom ||
             events[i].instrument == DrumInstrument::FloorTom))
            ++strongFillVoices;
    }

    assert(weakFillVoices >= 2);
    assert(strongFillVoices > weakFillVoices);
}
