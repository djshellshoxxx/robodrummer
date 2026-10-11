#include "core/DrumMidiMap.h"
#include <cassert>

int main() {
    using namespace robodrummer;
    DrumMidiMap m;
    assert(m.noteFor(DrumInstrument::Kick) == 36);
    assert(m.noteFor(DrumInstrument::ClosedHat) == 42);
    assert(m.noteFor(DrumInstrument::Clap) == 39);
    assert(m.channel() == 10 && m.zeroBasedChannel() == 9);
    assert(m.noteLengthSamples(48000.0) == 4800);

    assert(m.setNote(DrumInstrument::Kick, 35));
    assert(!m.setNote(DrumInstrument::Kick, 128) && m.noteFor(DrumInstrument::Kick) == 35);
    assert(!m.setChannel(0) && !m.setChannel(17) && m.setChannel(1));
    assert(!m.setNoteLengthMs(1.0f) && m.setNoteLengthMs(250.0f));

    const auto text = m.serialize();
    auto r = DrumMidiMap::parse(text);
    assert(r.ok && r.map == m);

    r = DrumMidiMap::parse("kick=40; Snare = 41 ;ch=3");
    assert(r.ok && r.map.noteFor(DrumInstrument::Kick) == 40 && r.map.noteFor(DrumInstrument::Snare) == 41);
    assert(r.map.channel() == 3 && r.map.noteFor(DrumInstrument::Ride) == 51);

    assert(DrumMidiMap::parse("").ok);
    assert(!DrumMidiMap::parse("kick=128").ok);
    assert(!DrumMidiMap::parse("kick=-1").ok);
    assert(!DrumMidiMap::parse("kick=1;kick=2").ok);
    assert(!DrumMidiMap::parse("bogus=1").ok);
    assert(!DrumMidiMap::parse("kick").ok);
    assert(!DrumMidiMap::parse("ch=17").ok);
    assert(!DrumMidiMap::parse("len=0").ok);
    assert(!DrumMidiMap::parse("kick=abc").ok);
    assert(!DrumMidiMap::parse(std::string(600, 'a')).ok);

    assert(drumInstrumentFromName("FloorTom") == DrumInstrument::FloorTom);
    assert(!drumInstrumentFromName("gong"));
    return 0;
}
