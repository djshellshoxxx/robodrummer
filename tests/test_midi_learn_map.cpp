#include "core/MidiLearnMap.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    MidiLearnMap map;
    assert(map.commandForNote(36) == MidiCommand::Fill);
    assert(map.commandForNote(49) == MidiCommand::EndJam);
    assert(!map.commandForNote(99).has_value());

    assert(map.assign(MidiCommand::Fill, 60));
    assert(map.commandForNote(60) == MidiCommand::Fill);
    assert(!map.commandForNote(36).has_value());
    assert(map.noteForCommand(MidiCommand::Fill) == 60);

    // Reusing a note unassigns the previous target rather than creating ambiguity.
    assert(map.assign(MidiCommand::Crash, 60));
    assert(map.commandForNote(60) == MidiCommand::Crash);
    assert(map.noteForCommand(MidiCommand::Fill) == -1);

    assert(!map.assign(MidiCommand::Fill, -1));
    assert(!map.assign(MidiCommand::Fill, 128));

    map.resetDefaults();
    assert(map.noteForCommand(MidiCommand::Fill) == 36);
    assert(map.noteForCommand(MidiCommand::EndJam) == 49);
}
