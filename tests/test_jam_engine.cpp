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

    engine.resetPhase();
    engine.syncToPpq(1.0);
    const auto syncedCount = engine.processBlock(512, events.data(), events.size());
    bool sawSyncedSnare = false;
    for (std::size_t i = 0; i < syncedCount; ++i)
        if (events[i].instrument == DrumInstrument::Snare && events[i].sampleOffset == 0)
            sawSyncedSnare = true;
    assert(sawSyncedSnare);

    engine.resetPhase();
    engine.apply(MidiCommand::Fill);
    const auto fillCount = engine.processBlock(96000, events.data(), events.size());
    bool sawTom = false;
    for (std::size_t i = 0; i < fillCount; ++i)
        if (events[i].instrument == DrumInstrument::HighTom || events[i].instrument == DrumInstrument::MidTom || events[i].instrument == DrumInstrument::FloorTom)
            sawTom = true;
    assert(sawTom);
    assert(!engine.state().fillRequested);

    engine.apply(MidiCommand::ResetListening);
    assert(!engine.state().resetListeningRequested);

    engine.apply(MidiCommand::Stop);
    assert(engine.processBlock(512, events.data(), events.size()) == 0);
}
