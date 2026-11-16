#include "core/MidiLearn.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
    using namespace robodrummer;
    MidiLearnMap map;
    constexpr int note = 70;
    constexpr int fill = static_cast<int>(MidiCommand::Fill);
    constexpr int crash = static_cast<int>(MidiCommand::Crash);
    std::atomic<bool> start{false}, done{false};
    std::atomic<int> mixed{0};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int repeat = 0; repeat < 200000; ++repeat) {
            map.bindNote(note, fill, 1);
            map.bindNote(note, crash, 2);
        }
        done.store(true, std::memory_order_release);
    });
    start.store(true, std::memory_order_release);
    do {
        const int onOne = map.commandForNote(note, 1);
        const int onTwo = map.commandForNote(note, 2);
        if ((onOne != MidiLearnMap::NoCommand && onOne != fill) ||
            (onTwo != MidiLearnMap::NoCommand && onTwo != crash)) ++mixed;
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    std::printf("mixed channel/command bindings: %d\n", mixed.load());
    assert(mixed.load() == 0);
}
