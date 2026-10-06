#include "plugin/MidiCommandMapper.h"
#include <cassert>
int main() {
    using namespace robodrummer;
    assert(mapNoteOn(36, 1.0f) == MidiCommand::Fill);
    assert(mapNoteOn(37, 1.0f) == MidiCommand::NextSection);
    assert(mapNoteOn(40, 1.0f) == MidiCommand::IntensityUp);
    assert(mapNoteOn(41, 1.0f) == MidiCommand::IntensityDown);
    assert(mapNoteOn(42, 1.0f) == MidiCommand::HalfTime);
    assert(mapNoteOn(43, 1.0f) == MidiCommand::DoubleTime);
    assert(mapNoteOn(47, 1.0f) == MidiCommand::ResetListening);
    assert(mapNoteOn(48, 1.0f) == MidiCommand::SoloSupport);
    assert(mapNoteOn(49, 1.0f) == MidiCommand::EndJam);
    assert(!mapNoteOn(99, 1.0f).has_value());
    assert(!mapNoteOn(36, 0.0f).has_value());
}
