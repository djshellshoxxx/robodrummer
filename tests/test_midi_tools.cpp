#include "core/MidiClockTracker.h"
#include "core/MidiLearn.h"
#include "core/TapTempo.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace robodrummer;
    // Tap tempo
    {
        TapTempo t;
        assert(!t.tap(0.0));
        const auto bpm = t.tap(0.5);
        assert(bpm && std::abs(*bpm - 120.0) < 0.01);
        t.tap(1.0); const auto b4 = t.tap(1.5);
        assert(b4 && std::abs(*b4 - 120.0) < 0.01);
        // A long gap restarts the measurement.
        assert(!t.tap(10.0));
        const auto slow = t.tap(11.0);
        assert(slow && std::abs(*slow - 60.0) < 0.01);
        // A wild outlier restarts as well instead of dragging the average.
        t.tap(12.0); t.tap(13.0);
        assert(!t.tap(13.1));
        TapTempo fast; fast.tap(0.0);
        const auto clampedFast = fast.tap(0.05);
        assert(clampedFast && *clampedFast == 240.0);
    }
    // MIDI clock
    {
        MidiClockTracker clock; clock.prepare(48000.0);
        assert(!clock.valid(0));
        // 120 BPM => 24 ppqn => tick every 1000 samples at 48 kHz.
        for (int i = 0; i < 60; ++i) clock.tick(static_cast<long long>(i) * 1000);
        assert(clock.valid(59 * 1000 + 100));
        assert(std::abs(clock.bpm() - 120.0) < 0.5);
        assert(!clock.valid(59 * 1000 + 48000)); // ticks stopped arriving
        clock.start(); assert(clock.running() && clock.consumeStart() && !clock.consumeStart());
        clock.stop(); assert(!clock.running());
        // Tempo change to 90 BPM (1333.33 samples/tick) is tracked.
        long long t = 59000;
        for (int i = 0; i < 80; ++i) { t += 1333; clock.tick(t); }
        assert(std::abs(clock.bpm() - 90.0) < 1.5);
    }
    // MIDI learn
    {
        MidiLearnMap map;
        assert(map.commandForNote(36, 16) == static_cast<int>(MidiCommand::Fill));
        assert(map.commandForNote(36, 1) == MidiLearnMap::NoCommand); // defaults live on channel 16 only
        assert(map.commandForNote(50, 16) == static_cast<int>(MidiCommand::ImmediateFill));
        assert(map.handleCc(11) == MidiLearnMap::NoTarget);
        map.bindCc(11, 3);
        assert(map.handleCc(11) == 3 && map.ccForTarget(3) == 11);
        map.bindCc(20, 3); // a target has exactly one controller
        assert(map.ccForTarget(3) == 20 && map.targetForCc(11) == MidiLearnMap::NoTarget);
        map.armTarget(7);
        assert(map.armedTarget() == 7);
        assert(map.handleCc(64) == 7 && map.armedTarget() == MidiLearnMap::NoTarget);
        assert(map.handleCc(64) == 7);
        map.unbindTarget(7); assert(map.handleCc(64) == MidiLearnMap::NoTarget);
        // Learning a command from a footswitch note on channel 1.
        map.armCommand(static_cast<int>(MidiCommand::Crash));
        assert(map.handleNote(60, 1) == MidiLearnMap::NoCommand); // the learning press does not fire the command
        assert(map.handleNote(60, 1) == static_cast<int>(MidiCommand::Crash));
        assert(map.handleNote(60, 16) == MidiLearnMap::NoCommand);
        assert(map.commandForNote(39, 16) == MidiLearnMap::NoCommand); // the old default binding was replaced
        map.cancelLearn(); map.resetNoteDefaults();
        assert(map.commandForNote(39, 16) == static_cast<int>(MidiCommand::Crash));
    }
}
