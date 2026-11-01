#include "core/MidiLearn.h"
#include <array>
#include <atomic>
#include <cassert>
#include <thread>

int main() {
    using namespace robodrummer;
    constexpr int note = 70;
    constexpr int fill = static_cast<int>(MidiCommand::Fill);
    constexpr int crash = static_cast<int>(MidiCommand::Crash);
    MidiLearnMap map;
    map.bindNote(note, fill, 1);
    auto binding = map.noteBindingForNote(note);
    assert(binding.command == fill && binding.channel == 1);
    map.bindNote(note, crash, 2);
    binding = map.noteBindingForNote(note);
    assert(binding.command == crash && binding.channel == 2);
    assert(map.commandForNote(note, 1) == MidiLearnMap::NoCommand);
    assert(map.commandForNote(note, 2) == crash);

    // Each captured pair is independently serializable through the existing
    // note/command/channel schema, even while audio/UI writers rebind/reset.
    std::atomic<bool> done{false};
    std::atomic<int> badPairs{0};
    std::thread writer([&] {
        for (int repeat = 0; repeat < 100000; ++repeat) {
            map.bindNote(note, fill, 1);
            map.bindNote(note, crash, 2);
            if ((repeat & 63) == 0) map.resetNoteDefaults();
        }
        done.store(true, std::memory_order_release);
    });
    int observations = 0;
    while (!done.load(std::memory_order_acquire) || observations < 1000) {
        const auto current = map.noteBindingForNote(note);
        if (current.command != MidiLearnMap::NoCommand &&
            !(current.command == fill && current.channel == 1) &&
            !(current.command == crash && current.channel == 2)) ++badPairs;
        MidiLearnMap restored;
        if (current.command != MidiLearnMap::NoCommand)
            restored.bindNote(note, current.command, current.channel);
        const auto roundTrip = restored.noteBindingForNote(note);
        if (current.command != MidiLearnMap::NoCommand &&
            (roundTrip.command != current.command || roundTrip.channel != current.channel)) ++badPairs;
        ++observations;
    }
    writer.join();
    assert(badPairs.load() == 0);
    map.resetNoteDefaults();
    const auto defaultFill = map.noteBindingForNote(36);
    assert(defaultFill.command == fill && defaultFill.channel == 16);
    const auto invalid = map.noteBindingForNote(-1);
    assert(invalid.command == MidiLearnMap::NoCommand && invalid.channel == 16);
}
