#pragma once
#include "plugin/PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace robodrummer::test {

struct AuthorityPlayHead final : juce::AudioPlayHead {
    double bpm{120.0}, ppq{};
    int numerator{4}, denominator{4};
    bool playing{true}, hasMeter{true};
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setBpm(bpm); position.setPpqPosition(ppq); position.setIsPlaying(playing);
        if (hasMeter) position.setTimeSignature(TimeSignature{numerator, denominator});
        return position;
    }
};

inline void setAuthorityParameter(RoboDrummerAudioProcessor& processor, const char* id, float plain) {
    if (auto* parameter = processor.getParameters().getParameter(id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
        parameter->endChangeGesture();
    }
}

struct AuthorityRig {
    std::unique_ptr<RoboDrummerAudioProcessor> processor{std::make_unique<RoboDrummerAudioProcessor>()};
    AuthorityPlayHead playHead;
    juce::AudioBuffer<float> audio;
    juce::MidiBuffer midi;
    int block;
    long long cursor{};
    explicit AuthorityRig(int blockSize) : block(blockSize) {
        processor->setPlayHead(&playHead);
        processor->prepareToPlay(48000.0, blockSize);
        audio.setSize(std::max(2, processor->getTotalNumOutputChannels()), blockSize);
        midi.ensureSize(16384);
        setAuthorityParameter(*processor, "outputMode", 1.0f);
        setAuthorityParameter(*processor, "tempoSource", 1.0f);
        setAuthorityParameter(*processor, "bpm", 120.0f);
    }
    ~AuthorityRig() { processor->releaseResources(); }
    template <typename Signal, typename Audit>
    bool processAudited(Signal&& signal, Audit&& audit) {
        audio.clear(); midi.clear();
        for (int sample = 0; sample < block; ++sample) {
            const float value = signal(cursor + sample);
            audio.setSample(0, sample, value); audio.setSample(1, sample, value);
        }
        const bool clean = audit([&] { processor->processBlock(audio, midi); });
        cursor += block;
        playHead.ppq += static_cast<double>(block) / 48000.0 * playHead.bpm / 60.0;
        return clean;
    }
    template <typename Signal>
    void process(Signal&& signal) {
        processAudited(signal, [](auto&& callback) { callback(); return true; });
    }
};

struct AdaptiveAcquisition {
    bool joined{}, locked{}, finite{true}, cursorMatches{true};
    int noteOns{}, confidentBlocks{};
    double firstLockSeconds{-1.0}, firstJoinSeconds{-1.0}, bpm{}, phase{};
    long long firstNoteSample{-1};
    double firstNotePhaseError{};
    float tempoConfidence{}, beatConfidence{};
};

inline AdaptiveAcquisition characterizeAdaptiveAcquisition(int block, double onsetOffsetBeats,
                                                           double seconds = 20.0, int leadershipMode = 1, bool offbeatAttack = false,
                                                           bool emptyCallbacks = false) {
    AuthorityRig rig(block);
    setAuthorityParameter(*rig.processor, "leadershipMode", static_cast<float>(leadershipMode));
    setAuthorityParameter(*rig.processor, "humanize", 0.0f);
    AdaptiveAcquisition result;
    juce::AudioBuffer<float> empty(2, 0);
    juce::MidiBuffer emptyMidi;
    emptyMidi.ensureSize(16384);
    constexpr double period = 24000.0;
    const auto guitar = [&](long long sample) {
        const double position = static_cast<double>(sample) - onsetOffsetBeats * period;
        if (position < 0.0) return 0.0f;
        const double within = std::fmod(position, period);
        const auto beat = static_cast<long long>(position / period);
        const double amplitude = beat % 4 == 0 ? 0.8 : 0.45;
        const double extra = static_cast<double>(sample) - 159000.0; // a final rejected eighth-note attack before admission
        const double offbeat = offbeatAttack && extra >= 0.0 && extra < 8000.0
            ? 0.45 * std::exp(-extra / 2200.0) * std::sin(6.2831853 * 196.0 * extra / 48000.0) : 0.0;
        return static_cast<float>(offbeat + amplitude * std::exp(-within / 2200.0)
            * std::sin(6.2831853 * 196.0 * within / 48000.0) * (within < 8000.0 ? 1.0 : 0.0));
    };
    const auto blocks = static_cast<long long>(std::ceil(seconds * 48000.0 / block));
    for (long long iteration = 0; iteration < blocks; ++iteration) {
        rig.process(guitar);
        result.locked = rig.processor->isGuitarTrackerLocked();
        result.joined = rig.processor->hasAdaptiveJoined();
        result.tempoConfidence = rig.processor->getGuitarTempoConfidence();
        result.beatConfidence = rig.processor->getGuitarBeatConfidence();
        result.bpm = rig.processor->getDetectedGuitarBpm();
        result.phase = rig.processor->getGuitarBeatPhase();
        result.cursorMatches &= rig.processor->getAnalysisSampleCursor() == rig.cursor;
        if (result.locked && result.firstLockSeconds < 0) result.firstLockSeconds = static_cast<double>(rig.cursor) / 48000.0;
        if (result.joined && result.firstJoinSeconds < 0) result.firstJoinSeconds = static_cast<double>(rig.cursor) / 48000.0;
        if (result.locked && result.tempoConfidence >= 0.60f && result.beatConfidence >= 0.55f) ++result.confidentBlocks;
        for (const auto message : rig.midi) if (message.getMessage().isNoteOn()) {
            ++result.noteOns;
            if (result.firstNoteSample < 0) {
                result.firstNoteSample = rig.cursor - block + message.samplePosition;
                const double beats = static_cast<double>(result.firstNoteSample) / period - onsetOffsetBeats;
                const double phase = beats - std::floor(beats);
                result.firstNotePhaseError = std::min(phase, 1.0 - phase);
            }
        }
        for (int channel = 0; channel < rig.audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < block; ++sample)
                result.finite &= std::isfinite(rig.audio.getSample(channel, sample));
        if (emptyCallbacks) {
            emptyMidi.clear();
            rig.processor->processBlock(empty, emptyMidi);
        }
    }
    return result;
}

// A tracker that is confidently locked must not wait indefinitely because the
// host samples its phase at only a few repeating callback boundaries. Large
// callbacks must enter on a real beat inside the buffer, not simply widen the
// admission window and emit at an arbitrary callback-start phase.
template <typename Check>
void runAdaptiveJoinBoundaryChecks(Check&& check) {
    struct Case { int block; double offset; int mode; bool offbeat{}, empty{}; };
    for (const auto entry : {Case{512, 0.125, 1}, Case{4096, 0.125, 1},
                             Case{40000, 0.0, 1}, Case{40000, 0.125, 1},
                             Case{40000, 0.125, 2}, Case{40000, 0.125, 0}, Case{40000, 0.125, 1, true},
                             Case{40000, 0.125, 1, false, true}}) {
        const auto result = characterizeAdaptiveAcquisition(entry.block, entry.offset, 8.0, entry.mode, entry.offbeat, entry.empty);
        check(result.finite, "adaptive entry remains finite for ordinary and oversized callback sizes");
        check(result.cursorMatches, "analysis display cursor follows the exact end of multi-chunk host callbacks");
        check(result.locked && result.confidentBlocks >= 3 && result.firstLockSeconds < 4.0,
              "steady synthetic guitar reaches a confident lock independently of callback size");
        check(result.joined && result.noteOns > 5 && result.firstJoinSeconds >= 0.0 && result.firstJoinSeconds <= 6.0,
              "confident adaptive modes join and generate drums without callback-phase starvation");
        if (entry.block == 40000 && entry.mode != 0)
            check(result.firstNoteSample >= 0 && result.firstNotePhaseError <= 0.08,
                  "oversized adaptive entry schedules its first generated MIDI hit on the predicted beat inside the callback");
    }
}

// The 60 BPM fixture puts the real accepted entry at sample 36,116 of a 40,000-sample
// callback, beyond the fixed scratch capacity. Audit only the real callback, never fixture/export setup.
template <typename Check, typename Audit>
void runAdaptiveJoinTimelineChecks(Check&& check, Audit&& audit) {
    const auto guitar = [](long long sample) {
        const double position = static_cast<double>(sample) - 36000.0;
        if (position < 0.0) return 0.0f;
        const double within = std::fmod(position, 48000.0);
        const auto beat = static_cast<long long>(position / 48000.0);
        return static_cast<float>((beat % 4 == 0 ? 0.8 : 0.45) * std::exp(-within / 2200.0)
            * std::sin(6.2831853 * 196.0 * within / 48000.0) * (within < 8000.0 ? 1.0 : 0.0));
    };
    int plainEntry = -1;
    for (bool practice : {false, true}) {
        AuthorityRig rig(40000);
        setAuthorityParameter(*rig.processor, "leadershipMode", 1.0f);
        setAuthorityParameter(*rig.processor, "humanize", 0.0f);
        setAuthorityParameter(*rig.processor, "outputMode", 2.0f);
        if (practice) {
            setAuthorityParameter(*rig.processor, "practiceEnabled", 1.0f);
            setAuthorityParameter(*rig.processor, "pracCountInLen", 4.0f); // one-bar count-in begins on entry beat one
        }
        bool clean = true;
        int first = -1, firstAudio = -1, firstNote = -1;
        for (int block = 0; block < 24 && first < 0; ++block) {
            clean &= rig.processAudited(guitar, audit);
            for (const auto event : rig.midi) if (event.getMessage().isNoteOn()) {
                first = event.samplePosition; firstNote = event.getMessage().getNoteNumber(); break;
            }
            if (first >= 0) for (int sample = 0; sample < rig.block; ++sample)
                if (std::abs(rig.audio.getSample(0, sample)) + std::abs(rig.audio.getSample(1, sample)) > 1.0e-7f) {
                    firstAudio = sample; break;
                }
        }
        check(clean, "oversized adaptive MIDI/audio/practice callbacks allocate and free no heap memory");
        check(first > 32768 && first < rig.block, "trusted entry actually lands beyond the fixed audio scratch capacity");
        check(firstAudio >= first && firstAudio <= first + 32,
              "internal audio starts with MIDI at the scheduled suffix, including beyond sample 32768");
        juce::MemoryBlock bytes;
        const auto capture = rig.processor->buildCaptureFile(0, bytes);
        if (practice) {
            check(first == plainEntry && firstNote == 56, "practice count-in click uses the same translated entry offset as the groove");
            check(capture.status == robodrummer::CaptureStatus::Empty, "count-in clicks remain excluded from generated drum capture");
        } else {
            plainEntry = first;
            juce::MemoryInputStream stream(bytes.getData(), bytes.getSize(), false);
            juce::MidiFile file;
            bool read = file.readFrom(stream);
            double firstTick = -1.0;
            if (read) for (int track = 0; track < file.getNumTracks(); ++track)
                for (int event = 0; event < file.getTrack(track)->getNumEvents(); ++event) {
                    const auto& message = file.getTrack(track)->getEventPointer(event)->message;
                    if (message.isNoteOn() && (firstTick < 0.0 || message.getTimeStamp() < firstTick)) firstTick = message.getTimeStamp();
                }
            const double expected = std::round(first * robodrummer::kMidiPpq * rig.processor->getEffectiveDrummerBpm() / 60.0 / 48000.0);
            check(capture.status == robodrummer::CaptureStatus::Ok && read && std::abs(firstTick - expected) <= 1.0,
                  "captured MIDI retains the exact scheduled leading silence and first-note offset");
        }
    }
    for (int reset = 0; reset < 5; ++reset) {
        AuthorityRig rig(40000);
        setAuthorityParameter(*rig.processor, "leadershipMode", 1.0f);
        juce::MemoryBlock state;
        rig.processor->getStateInformation(state);
        for (int block = 0; block < 6; ++block) rig.process(guitar);
        check(!rig.processor->hasAdaptiveJoined() && rig.processor->isGuitarTrackerLocked(), "lifecycle fixture has a confident future entry armed");
        if (reset == 0) rig.processor->resetJamPhase();
        if (reset == 1) rig.processor->prepareToPlay(48000.0, 40000);
        if (reset == 2) setAuthorityParameter(*rig.processor, "leadershipMode", 2.0f);
        if (reset == 3) rig.processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        if (reset == 4) {
            auto layout = rig.processor->getBusesLayout();
            layout.inputBuses.set(0, juce::AudioChannelSet::disabled());
            check(rig.processor->setBusesLayout(layout), "lifecycle fixture disables guitar input through the supported layout API");
        }
        rig.process(guitar);
        bool note = false;
        for (const auto event : rig.midi) note |= event.getMessage().isNoteOn();
        check(!rig.processor->hasAdaptiveJoined() && !note, "prepare/mode/listening/state/no-input changes discard stale pending entries");
    }
}

// Characterize the existing policy: Internal BPM controls tempo/transport/PPQ,
// while meter still follows host signature unless the manual override is set.
template <typename Check>
void runInternalTempoMeterChecks(Check&& check) {
    AuthorityRig a(512), b(512);
    a.playHead.bpm = 71.0; a.playHead.ppq = 0.0; a.playHead.playing = true;
    b.playHead.bpm = 217.0; b.playHead.ppq = 1.0e12; b.playHead.playing = false;
    a.playHead.numerator = b.playHead.numerator = 7;
    a.playHead.denominator = b.playHead.denominator = 8;
    int noteOns = 0;
    bool exactMidi = true;
    const auto silence = [](long long) { return 0.0f; };
    for (int iteration = 0; iteration < 360; ++iteration) {
        if (iteration == 60) {
            a.playHead.numerator = b.playHead.numerator = 3;
            a.playHead.denominator = b.playHead.denominator = 4;
        }
        b.playHead.playing = iteration % 2 == 0;
        b.playHead.ppq = iteration % 2 == 0 ? 1.0e12 : -1.0e12;
        a.process(silence); b.process(silence);
        if (iteration == 0)
            check(a.processor->getEffectiveMeterNumerator() == 7 && a.processor->getEffectiveMeterDenominator() == 8,
                  "Internal BPM initially retains the available host 7/8 signature");
        auto left = a.midi.cbegin(), right = b.midi.cbegin();
        for (; left != a.midi.cend() && right != b.midi.cend(); ++left, ++right) {
            const auto x = *left, y = *right;
            exactMidi &= x.samplePosition == y.samplePosition && x.numBytes == y.numBytes
                && std::equal(x.data, x.data + x.numBytes, y.data);
        }
        exactMidi &= left == a.midi.cend() && right == b.midi.cend();
        for (const auto event : a.midi) if (event.getMessage().isNoteOn()) ++noteOns;
    }
    check(exactMidi && noteOns > 20, "Internal BPM ignores conflicting host tempo, PPQ jumps and transport in exact callback MIDI output");
    check(std::abs(a.processor->getEffectiveDrummerBpm() - 120.0) < 0.001
          && std::abs(b.processor->getEffectiveDrummerBpm() - 120.0) < 0.001,
          "Internal BPM remains authoritative despite conflicting host BPM");
    check(a.processor->getEffectiveMeterNumerator() == 3 && a.processor->getEffectiveMeterDenominator() == 4
          && b.processor->getEffectiveMeterNumerator() == 3 && b.processor->getEffectiveMeterDenominator() == 4,
          "Internal BPM retains and updates the host time signature independently of tempo authority");
    setAuthorityParameter(*a.processor, "manualMeterEnabled", 1.0f);
    setAuthorityParameter(*a.processor, "meterNumerator", 3.0f); // choice index 3 -> five beats
    setAuthorityParameter(*a.processor, "meterDenominator", 2.0f); // choice index 2 -> eighth note
    a.playHead.numerator = 7; a.playHead.denominator = 16; a.process(silence);
    check(a.processor->getEffectiveMeterNumerator() == 5 && a.processor->getEffectiveMeterDenominator() == 8,
          "manual meter overrides host signature while using Internal BPM");
    setAuthorityParameter(*a.processor, "manualMeterEnabled", 0.0f);
    a.playHead.hasMeter = false; a.process(silence);
    check(a.processor->getEffectiveMeterNumerator() == 4 && a.processor->getEffectiveMeterDenominator() == 4,
          "Internal BPM falls back to 4/4 when neither host nor manual meter is available");
    AuthorityRig host(512);
    setAuthorityParameter(*host.processor, "tempoSource", 0.0f);
    host.playHead.bpm = 71.0; host.playHead.playing = false;
    int stoppedNotes = 0, resumedNotes = 0;
    for (int iteration = 0; iteration < 120; ++iteration) {
        host.process(silence);
        for (const auto event : host.midi) if (event.getMessage().isNoteOn()) ++stoppedNotes;
    }
    check(stoppedNotes == 0 && std::abs(host.processor->getEffectiveDrummerBpm() - 71.0) < 0.001,
          "positive control: host/internal mode actually obeys the conflicting host tempo and stopped transport");
    host.playHead.playing = true;
    for (int iteration = 0; iteration < 120; ++iteration) {
        host.process(silence);
        for (const auto event : host.midi) if (event.getMessage().isNoteOn()) ++resumedNotes;
    }
    check(resumedNotes > 0, "positive control: host transport resume generates drums in host/internal mode");
}

} // namespace robodrummer::test
