#pragma once
#include "core/MidiBlockPolicy.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// The processor harness supplies its existing assertion callback. The views
// must match JUCE's normal MIDI1 classification while retaining no ownership.
template <typename Check>
void checkMidiViewParity(Check&& check) {
    const auto compare = [&](const std::uint8_t* bytes, int size) {
        const juce::MidiMessage owning(bytes, size, 0.0);
        const robodrummer::MidiMessageView view(bytes, size);
        check(view.getChannel() == owning.getChannel());
        check(view.isMidiClock() == owning.isMidiClock());
        check(view.isMidiStart() == owning.isMidiStart());
        check(view.isMidiContinue() == owning.isMidiContinue());
        check(view.isMidiStop() == owning.isMidiStop());
        check(view.isNoteOn() == owning.isNoteOn());
        check(view.isNoteOff() == owning.isNoteOff());
        check(view.isController() == owning.isController());
        check(view.isAllNotesOff() == owning.isAllNotesOff());
        check(view.isAllSoundOff() == owning.isAllSoundOff());
        if (owning.isNoteOnOrOff()) {
            check(view.getNoteNumber() == owning.getNoteNumber());
            check(view.getVelocity() == owning.getVelocity());
        }
        if (owning.isController()) {
            check(view.getControllerNumber() == owning.getControllerNumber());
            check(view.getControllerValue() == owning.getControllerValue());
        }
    };
    for (int channel=0; channel<16; ++channel) {
        for (const int status : {0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0}) {
            for (const int datum : {0, 11, 45, 120, 123, 127}) {
                const std::array<std::uint8_t, 3> bytes{
                    static_cast<std::uint8_t>(status | channel), static_cast<std::uint8_t>(datum),
                    static_cast<std::uint8_t>(datum)};
                compare(bytes.data(), status==0xc0 || status==0xd0 ? 2 : 3);
            }
        }
    }
    for (const std::uint8_t status : {0xf8, 0xfa, 0xfb, 0xfc, 0xfe, 0xff}) compare(&status, 1);
    std::array<std::uint8_t, 32> sysex{};
    sysex.front()=0xf0; sysex.back()=0xf7;
    compare(sysex.data(), static_cast<int>(sysex.size()));
}
