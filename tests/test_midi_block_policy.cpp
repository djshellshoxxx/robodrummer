#include "core/MidiBlockPolicy.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

int main() {
    using namespace robodrummer;
    static_assert(std::is_trivially_destructible_v<MidiMessageView>);
    std::array<std::uint8_t, 3> note{0x91, 61, 100};
    MidiMessageView on(note.data(), static_cast<int>(note.size()));
    assert(on.isNoteOn() && !on.isNoteOff() && on.getChannel() == 2);
    assert(on.getNoteNumber() == 61 && on.getVelocity() == 100);
    note[2] = 0;
    assert(!on.isNoteOn() && on.isNoteOff());
    note[0] = 0x81;
    assert(on.isNoteOff());
    note = {0xbf, 123, 0};
    assert(on.isController() && on.isAllNotesOff() && !on.isAllSoundOff());
    note[1] = 120;
    assert(on.isAllSoundOff());
    note[1] = 11; note[2] = 127;
    assert(on.getControllerNumber() == 11 && on.getControllerValue() == 127);
    const std::uint8_t clock=0xf8, start=0xfa, resume=0xfb, stop=0xfc;
    assert(MidiMessageView(&clock, 1).isMidiClock());
    assert(MidiMessageView(&start, 1).isMidiStart());
    assert(MidiMessageView(&resume, 1).isMidiContinue());
    assert(MidiMessageView(&stop, 1).isMidiStop());
    assert(MidiMessageView(&stop, 1).getChannel() == 0);
    assert(!MidiMessageView(nullptr, 100).isNoteOn());
    assert(!MidiMessageView(note.data(), 1).isController());
    assert(!MidiMessageView(note.data(), -1).isNoteOff());

    // Oversized and cumulative SysEx are viewed without copying, then rejected
    // before a JUCE buffer could grow. Short controls retain their own budget.
    std::array<std::uint8_t, 16384> sysex{};
    sysex.front()=0xf0; sysex.back()=0xf7;
    MidiMessageView longMessage(sysex.data(), static_cast<int>(sysex.size()));
    assert(longMessage.data() == sysex.data() && longMessage.size() == sysex.size());
    assert(longMessage.getChannel() == 0 && !longMessage.isNoteOn());
    MidiBlockPolicy policy;
    assert(!policy.admitOutput(longMessage.size(), MidiOutputClass::Ordinary));
    assert(!policy.admitOutput(std::numeric_limits<std::size_t>::max(), MidiOutputClass::Ordinary));
    assert(policy.admitOutput(32, MidiOutputClass::Ordinary));
    int accepted=0;
    while (policy.admitOutput(1024, MidiOutputClass::Ordinary)) ++accepted;
    assert(accepted == 4);
    for (std::size_t hit=0; hit < MidiBlockPolicy::MaxGeneratedEvents; ++hit)
        assert(policy.admitOutput(3, MidiOutputClass::Generated));
    assert(!policy.admitOutput(3, MidiOutputClass::Generated));
    assert(policy.admitOutput(1, MidiOutputClass::Critical));
    assert(policy.admitOutput(3, MidiOutputClass::Critical));
    assert(policy.outputBytes() <= MidiBlockPolicy::OutputCapacityBytes);
    assert(policy.outputDropped() == 4);
    assert(!policy.needsPanic());
    MidiBlockPolicy noteOffPressure;
    int releaseMessages=0;
    while (noteOffPressure.admitOutput(3, MidiOutputClass::Critical)) ++releaseMessages;
    assert(releaseMessages == static_cast<int>(MidiBlockPolicy::CriticalBytes / 11));
    assert(noteOffPressure.criticalOutputOverflowed() && noteOffPressure.needsPanic());
    assert(MidiMessageView(&stop, 1).requiresCriticalOutput());
    assert(on.requiresCriticalOutput() == on.isNoteOff());

    // The caller enters a deterministic stop/panic path on a false scanNext;
    // no parsing or copying of the remaining unbounded host events is needed.
    for (std::size_t index=0; index<MidiBlockPolicy::MaxInputEvents; ++index)
        assert(policy.scanNext());
    assert(!policy.scanNext() && policy.inputOverflowed());
    assert(!policy.scanNext());
    policy.resetOutput();
    assert(policy.outputBytes() == 0 && policy.inputOverflowed());
    // A full 16-channel all-sound-off + all-notes-off panic fits after reset.
    for (int message=0; message<32; ++message)
        assert(policy.admitOutput(3, MidiOutputClass::Critical));
    MidiBlockPolicy nextBlock;
    assert(!nextBlock.inputOverflowed() && nextBlock.scanNext());
}
