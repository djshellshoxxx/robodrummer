#include "plugin/JamEngine.h"
#include <array>
#include <cassert>
#include <cmath>
int main() {
    using namespace robodrummer;
    JamEngine engine;
    engine.prepare(48000.0);
    engine.setTempo(120.0);
    engine.setMeter(4,4);
    assert(engine.currentBarIndex() == 0);
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
    assert(engine.currentBarIndex() == 1);

    engine.resetPhase();
    engine.nudgePhaseSamples(2400); // 0.1 beat at 120 BPM, 48 kHz
    assert(std::abs(engine.currentBeatPhase() - 0.10) < 0.002);
    engine.nudgePhaseSamples(-1200);
    assert(std::abs(engine.currentBeatPhase() - 0.05) < 0.002);

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

    engine.resetPhase();
    engine.apply(MidiCommand::Break);
    const auto breakCount = engine.processBlock(96000, events.data(), events.size());
    assert(breakCount == 0);
    assert(!engine.state().breakRequested);
    const auto postBreakCount = engine.processBlock(512, events.data(), events.size());
    assert(postBreakCount > 0);
    bool sawPostBreakKick = false;
    for (std::size_t i = 0; i < postBreakCount; ++i)
        if (events[i].instrument == DrumInstrument::Kick && events[i].sampleOffset == 0)
            sawPostBreakKick = true;
    assert(sawPostBreakKick);

    engine.apply(MidiCommand::Stop);
    assert(engine.processBlock(512, events.data(), events.size()) == 0);
}
