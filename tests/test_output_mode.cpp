#include "core/OutputMode.h"
#include <cassert>

int main() {
    using namespace robodrummer;
    assert(rendersInternalAudio(OutputMode::InternalDrums));
    assert(!writesGeneratedMidi(OutputMode::InternalDrums));

    assert(!rendersInternalAudio(OutputMode::MidiOnly));
    assert(writesGeneratedMidi(OutputMode::MidiOnly));

    assert(rendersInternalAudio(OutputMode::InternalAndMidi));
    assert(writesGeneratedMidi(OutputMode::InternalAndMidi));
}
