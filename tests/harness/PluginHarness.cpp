// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Headless end-to-end checks of the real AudioProcessor: audio, MIDI, tempo sources, kits, mixer, presets and
// state, plus an allocation audit of processBlock. Run with --screenshots <dir> to also render every editor tab.

#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "plugin/PresetManager.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::atomic<long long> gAllocations{0};
static std::atomic<bool> gCounting{false};
void* operator new(std::size_t n) { if (gCounting.load(std::memory_order_relaxed)) gAllocations.fetch_add(1); if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (gCounting.load(std::memory_order_relaxed)) gAllocations.fetch_add(1); if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
int gFailures = 0;
int gChecks = 0;
#define CHECK(cond) do { ++gChecks; if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, ...) do { ++gChecks; if (!(cond)) { ++gFailures; std::printf("  FAIL %s:%d  %s  -> ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

struct TestPlayHead final : juce::AudioPlayHead {
    bool playing{true};
    double bpm{120.0};
    double ppq{0.0};
    bool hasTempo{true};
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        if (hasTempo) info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
        return info;
    }
};

struct Result {
    double peak{0.0};
    int noteOns{0};
    std::array<int, 128> notes{};
    int drumNoteOns() const { return noteOns; }
};

struct Rig {
    RoboDrummerAudioProcessor processor;
    juce::AudioBuffer<float> buffer;
    TestPlayHead playHead;
    long long blockIndex{0};
    int numChannels{2};

    explicit Rig(bool withAux = false) {
        processor.setPlayHead(&playHead);
        if (withAux) {
            auto layout = processor.getBusesLayout();
            for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference(i) = juce::AudioChannelSet::stereo();
            processor.setBusesLayout(layout);
        }
        numChannels = juce::jmax(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
        processor.prepareToPlay(kSampleRate, kBlock);
        buffer.setSize(numChannels, kBlock);
    }
    ~Rig() { processor.releaseResources(); }

    void param(const char* id, float plain) {
        if (auto* p = processor.getParameters().getParameter(id)) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    }
    float value(const char* id) { return processor.getParameters().getRawParameterValue(id)->load(); }

    // Runs numBlocks blocks. guitar(sampleIndex) supplies the input signal; midiFor(blockIndex) the MIDI.
    template <typename Guitar, typename MidiFn>
    Result run(int numBlocks, Guitar&& guitar, MidiFn&& midiFor, std::array<double, 10>* channelPeaks = nullptr) {
        Result result;
        for (int b = 0; b < numBlocks; ++b, ++blockIndex) {
            buffer.clear();
            for (int i = 0; i < kBlock; ++i) {
                const float s = guitar(blockIndex * kBlock + i);
                for (int c = 0; c < juce::jmin(2, processor.getTotalNumInputChannels()); ++c) buffer.setSample(c, i, s);
            }
            juce::MidiBuffer midi;
            midiFor(blockIndex, midi);
            playHead.ppq += kBlock / kSampleRate * playHead.bpm / 60.0;
            processor.processBlock(buffer, midi);
            for (const auto m : midi) {
                const auto msg = m.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 10) { ++result.noteOns; ++result.notes[static_cast<std::size_t>(msg.getNoteNumber())]; }
            }
            for (int c = 0; c < buffer.getNumChannels(); ++c) {
                const double mag = buffer.getMagnitude(c, 0, kBlock);
                if (c < 2) result.peak = std::max(result.peak, mag);
                if (channelPeaks != nullptr && c < 10) (*channelPeaks)[static_cast<std::size_t>(c)] = std::max((*channelPeaks)[static_cast<std::size_t>(c)], mag);
            }
        }
        return result;
    }
    Result runSilent(int numBlocks) { return run(numBlocks, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer&) {}); }
};

// Plucked-note burst: a decaying 200 Hz tone with a sharp attack.
float pluck(long long sample, double bpm, double accentEvery = 4.0, double offsetBeats = 0.0) {
    const double period = kSampleRate * 60.0 / bpm;
    const double pos = static_cast<double>(sample) - offsetBeats * period;
    if (pos < 0.0) return 0.0f;
    const double inBeat = std::fmod(pos, period);
    const long long beat = static_cast<long long>(pos / period);
    const double amp = (std::fmod(static_cast<double>(beat), accentEvery) < 0.5) ? 0.8 : 0.45;
    return static_cast<float>(amp * std::exp(-inBeat / 2200.0) * std::sin(6.2831853 * 196.0 * inBeat / kSampleRate) * (inBeat < 8000 ? 1.0 : 0.0));
}

void pump(int milliseconds) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

void section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------------------------------------------------------
void testBasicPlayback() {
    section("basic playback and output modes");
    Rig rig;
    rig.param("outputMode", 0);
    const auto internal = rig.runSilent(400);
    CHECK_MSG(internal.peak > 0.05, "peak %f", internal.peak);
    CHECK(internal.noteOns == 0);

    Rig midiOnly;
    midiOnly.param("outputMode", 1);
    const auto m = midiOnly.runSilent(400);
    CHECK_MSG(m.peak < 1.0e-6, "peak %f", m.peak);
    CHECK_MSG(m.noteOns > 10, "noteOns %d", m.noteOns);
    CHECK(m.notes[36] > 0 && m.notes[38] > 0 && (m.notes[42] > 0 || m.notes[46] > 0 || m.notes[51] > 0));

    // 400 blocks = 4.27 s at 120 BPM -> 8 beats -> backbeat snares on beats 2 and 4 of two bars (about 4).
    CHECK_MSG(m.notes[38] >= 3 && m.notes[38] <= 6, "snares %d", m.notes[38]);
}

void testHostTransportAndTempoSources() {
    section("tempo sources");
    {
        Rig rig; rig.param("outputMode", 1);
        rig.playHead.bpm = 90.0;
        rig.runSilent(50);
        CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 90.0) < 0.5, "bpm %f", rig.processor.getEffectiveDrummerBpm());
        rig.param("tempoSource", 1); // internal only ignores the host tempo
        rig.param("bpm", 133.0f);
        rig.runSilent(50);
        CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 133.0) < 0.5, "bpm %f", rig.processor.getEffectiveDrummerBpm());
        rig.playHead.playing = false; // internal mode keeps playing when the host stops
        CHECK(rig.runSilent(100).noteOns > 0);
        rig.param("tempoSource", 0);
        rig.playHead.playing = false;
        rig.runSilent(10);
        CHECK(rig.runSilent(100).noteOns == 0);
    }
    {
        Rig rig; rig.param("outputMode", 1); rig.param("tempoSource", 2);
        rig.playHead.hasTempo = false;
        // MIDI clock at 90 BPM = 24 ppqn -> 2000 samples per tick; plus a Start message.
        const double samplesPerTick = kSampleRate * 60.0 / (90.0 * 24.0);
        double nextTick = 0.0;
        auto clockMidi = [&](long long block, juce::MidiBuffer& midi) {
            const double start = static_cast<double>(block) * kBlock;
            if (block == 0) midi.addEvent(juce::MidiMessage(0xFA), 0);
            while (nextTick < start + kBlock) {
                midi.addEvent(juce::MidiMessage(0xF8), static_cast<int>(nextTick - start));
                nextTick += samplesPerTick;
            }
        };
        const auto r = rig.run(300, [](long long) { return 0.0f; }, clockMidi);
        CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 90.0) < 1.5, "bpm %f", rig.processor.getEffectiveDrummerBpm());
        CHECK(rig.processor.isMidiClockActive());
        CHECK(r.noteOns > 5);
        // MIDI Stop silences the drummer, Continue resumes.
        rig.run(2, [](long long) { return 0.0f; }, [&](long long block, juce::MidiBuffer& midi) { clockMidi(block, midi); if (block == rig.blockIndex) midi.addEvent(juce::MidiMessage(0xFC), 0); });
        const auto stopped = rig.run(150, [](long long) { return 0.0f; }, clockMidi);
        CHECK_MSG(stopped.noteOns == 0, "noteOns %d", stopped.noteOns);
        const auto resumed = rig.run(150, [](long long) { return 0.0f; }, [&](long long block, juce::MidiBuffer& midi) { clockMidi(block, midi); if (block == rig.blockIndex) midi.addEvent(juce::MidiMessage(0xFB), 0); });
        CHECK(resumed.noteOns > 0);
    }
}

void testMidiCommandsAndLearn() {
    section("MIDI commands, learn and controllers");
    Rig rig; rig.param("outputMode", 1);
    rig.runSilent(20);
    // Immediate fill via channel-16 note 50 produces toms on channel 10.
    const auto fill = rig.run(200, [](long long) { return 0.0f; }, [](long long b, juce::MidiBuffer& m) { if (b == 25) m.addEvent(juce::MidiMessage::noteOn(16, 50, 1.0f), 0); });
    CHECK_MSG(fill.notes[50] + fill.notes[47] + fill.notes[43] > 0, "toms %d", fill.notes[50] + fill.notes[47] + fill.notes[43]);
    // Channel-16 messages are consumed; other notes pass through.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(16, 36, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 1);
    rig.buffer.clear();
    rig.processor.processBlock(rig.buffer, midi);
    bool sawThrough = false, saw16 = false;
    for (const auto m : midi) { sawThrough |= m.getMessage().getChannel() == 1 && m.getMessage().getNoteNumber() == 60; saw16 |= m.getMessage().getChannel() == 16; }
    CHECK(sawThrough && !saw16);

    // Default expression pedal: CC11 -> Intensity
    rig.run(1, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::controllerEvent(1, 11, 127), 0); });
    pump(120);
    CHECK_MSG(rig.value("intensity") > 0.95f, "intensity %f", rig.value("intensity"));
    // MIDI learn a controller to Humanize.
    int humanizeTarget = -1;
    for (int t = 0; t < robodrummer::kLearnTargetCount; ++t) if (std::string(robodrummer::kLearnTargets[static_cast<std::size_t>(t)].id) == "humanize") humanizeTarget = t;
    CHECK(humanizeTarget >= 0);
    rig.processor.getMidiLearn().armTarget(humanizeTarget);
    rig.run(1, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::controllerEvent(3, 74, 0), 0); });
    pump(120);
    CHECK(rig.processor.getMidiLearn().ccForTarget(humanizeTarget) == 74);
    CHECK_MSG(rig.value("humanize") < 0.02f, "humanize %f", rig.value("humanize"));
    rig.run(1, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::controllerEvent(3, 74, 127), 0); });
    pump(120);
    CHECK_MSG(rig.value("humanize") > 0.98f, "humanize %f", rig.value("humanize"));
    // MIDI learn a note for Crash on channel 2.
    rig.processor.getMidiLearn().armCommand(static_cast<int>(robodrummer::MidiCommand::Crash));
    rig.run(1, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::noteOn(2, 70, 1.0f), 0); });
    CHECK(rig.processor.getMidiLearn().commandForNote(70, 2) == static_cast<int>(robodrummer::MidiCommand::Crash));
    const auto crash = rig.run(5, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::noteOn(2, 70, 1.0f), 0); });
    CHECK(crash.notes[49] > 0);
}

void testGuitarFollowing() {
    section("guitarist leads: acquire, join, follow");
    Rig rig; rig.param("outputMode", 1);
    rig.playHead.hasTempo = false;
    rig.param("leadershipMode", 2);
    rig.param("tempoSource", 1);
    rig.param("bpm", 100.0f);
    rig.param("followRange", 40.0f);
    const auto early = rig.run(60, [](long long s) { return pluck(s, 108.0); }, [](long long, juce::MidiBuffer&) {});
    CHECK_MSG(early.noteOns == 0, "joined too early, %d hits", early.noteOns);
    const auto later = rig.run(1400, [](long long s) { return pluck(s, 108.0); }, [](long long, juce::MidiBuffer&) {});
    CHECK(rig.processor.isGuitarTrackerLocked());
    CHECK(rig.processor.hasAdaptiveJoined());
    CHECK_MSG(std::abs(rig.processor.getDetectedGuitarBpm() - 108.0) < 4.0, "guitar bpm %f", rig.processor.getDetectedGuitarBpm());
    CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 108.0) < 6.0, "drummer bpm %f", rig.processor.getEffectiveDrummerBpm());
    CHECK(later.noteOns > 10);
    // Gradual acceleration (108 -> 128 BPM over ~14 s) is followed without large jumps.
    double maxStep = 0.0, previous = rig.processor.getEffectiveDrummerBpm();
    const long long startSample = rig.blockIndex * kBlock;
    double beatAccum = 0.0;
    auto accelerating = [&](long long s) {
        const double progress = std::min(1.0, static_cast<double>(s - startSample) / (1200.0 * kBlock));
        const double bpm = 108.0 + 20.0 * progress;
        const double rate = bpm / 60.0 / kSampleRate;
        beatAccum += rate;
        const double inBeat = std::fmod(beatAccum, 1.0) / rate; // samples since the last beat
        return static_cast<float>(0.7 * std::exp(-inBeat / 2200.0) * std::sin(6.2831853 * 196.0 * inBeat / kSampleRate) * (inBeat < 8000 ? 1.0 : 0.0));
    };
    for (int b = 0; b < 1200; ++b) {
        rig.run(1, accelerating, [](long long, juce::MidiBuffer&) {});
        const double now = rig.processor.getEffectiveDrummerBpm();
        maxStep = std::max(maxStep, std::abs(now - previous));
        previous = now;
        if (std::getenv("RD_TRACE") != nullptr && b % 100 == 0)
            std::printf("    b=%d guitar=%.1f conf=%.2f/%.2f locked=%d auth=%.2f drummer=%.1f\n", b, rig.processor.getDetectedGuitarBpm(), rig.processor.getGuitarTempoConfidence(), rig.processor.getGuitarBeatConfidence(), rig.processor.isGuitarTrackerLocked() ? 1 : 0, rig.processor.getEffectiveGuitarAuthority(), now);
    }
    CHECK_MSG(maxStep < 1.0, "max per-block bpm step %f", maxStep);
    CHECK_MSG(rig.processor.getEffectiveDrummerBpm() > 112.0, "follow bpm %f", rig.processor.getEffectiveDrummerBpm());
    // Listening reset clears the join state.
    rig.processor.resetJamPhase();
    rig.runSilent(2);
    CHECK(!rig.processor.hasAdaptiveJoined());
}

void testMixerAndAux() {
    section("mixer, room and aux outputs");
    {
        Rig rig; rig.param("outputMode", 0);
        rig.param("gain_master", -60.0f);
        rig.runSilent(5); // let the master gain ramp finish
        const auto muted = rig.runSilent(200);
        CHECK_MSG(muted.peak < 1.0e-4, "peak %f", muted.peak);
        rig.param("gain_master", 0.0f);
        rig.param("mute_snare", 1); rig.param("mute_hihat", 1); rig.param("mute_ride", 1); rig.param("mute_cymbals", 1); rig.param("mute_toms", 1);
        rig.runSilent(5);
        const auto kickOnly = rig.runSilent(200);
        CHECK(kickOnly.peak > 0.05);
        rig.param("mute_snare", 0); rig.param("mute_hihat", 0); rig.param("mute_ride", 0); rig.param("mute_cymbals", 0); rig.param("mute_toms", 0);
        rig.param("solo_snare", 1);
        rig.runSilent(5);
        const auto snareOnly = rig.runSilent(200);
        CHECK(snareOnly.peak > 0.02 && snareOnly.peak < kickOnly.peak + 0.5);
    }
    {
        Rig rig(true);
        CHECK(rig.numChannels == 10);
        rig.param("outputMode", 0);
        std::array<double, 10> peaks{};
        rig.run(200, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer&) {}, &peaks);
        CHECK_MSG(peaks[0] > 0.02 && peaks[1] > 0.02, "main %f", peaks[0]);
        CHECK_MSG(peaks[2] > 0.02, "kick aux %f", peaks[2]);
        CHECK_MSG(peaks[4] > 0.02, "snare aux %f", peaks[4]);
        CHECK_MSG(peaks[8] > 0.005, "hats aux %f", peaks[8]);
        // Routed voices leave the main mix: with every aux enabled the main mix only holds the room return.
        Rig rig2(true); rig2.param("outputMode", 0); rig2.param("gain_room", -60.0f);
        std::array<double, 10> peaks2{};
        rig2.run(200, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer&) {}, &peaks2);
        CHECK_MSG(peaks2[0] < 1.0e-4, "main should be empty, %f", peaks2[0]);
    }
    {
        Rig rig; rig.param("outputMode", 0); rig.param("gain_room", 6.0f); rig.param("roomSize", 0.9f);
        rig.runSilent(5);
        const auto wet = rig.runSilent(200);
        CHECK(wet.peak > 0.05);
    }
}

void testKitLoading() {
    section("user kit loading (WAV)");
    auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("robodrummer-harness-kit");
    folder.deleteRecursively();
    folder.createDirectory();
    juce::WavAudioFormat wav;
    const auto writeWav = [&](const juce::String& name, float level, double rate) {
        juce::AudioBuffer<float> b(1, 2400);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, level * std::exp(-i / 800.0f));
        auto stream = std::make_unique<juce::FileOutputStream>(folder.getChildFile(name));
        stream->setPosition(0); stream->truncate();
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 1, 16, {}, 0));
        if (writer != nullptr) { stream.release(); writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    };
    writeWav("kick_v1.wav", 0.2f, 44100.0);   // soft layer, different sample rate (gets resampled)
    writeWav("kick_v2.wav", 0.9f, 44100.0);   // hard layer
    writeWav("snare.wav", 0.5f, 48000.0);
    writeWav("notes.txt.wav", 0.5f, 48000.0); // unrecognised: must be ignored
    Rig rig; rig.param("outputMode", 0);
    rig.processor.setUserKitFolder(folder);
    rig.param("kit", static_cast<float>(robodrummer::kUserKitIndex));
    for (int i = 0; i < 60 && !rig.processor.getKitStatus().contains("samples on"); ++i) { pump(50); rig.runSilent(2); }
    const auto status = rig.processor.getKitStatus();
    CHECK_MSG(status.contains("3 samples on 2 instruments"), "status: %s", status.toRawUTF8());
    CHECK_MSG(status.contains("Built-in sounds fill in"), "status: %s", status.toRawUTF8());
    rig.runSilent(4);
    const auto r = rig.runSilent(300);
    CHECK(r.peak > 0.05);
    rig.processor.requestKitReload();
    pump(100);
    rig.param("kit", 2); // back to a built-in kit
    pump(300);
    rig.runSilent(10);
    CHECK_MSG(rig.processor.getKitStatus().contains("Big Room"), "status: %s", rig.processor.getKitStatus().toRawUTF8());
    folder.deleteRecursively();
}

void testStateAndPresets() {
    section("state round trip and presets");
    juce::MemoryBlock state;
    {
        Rig rig;
        rig.param("jamStyle", 15); rig.param("humanize", 0.77f); rig.param("gain_kick", -7.5f); rig.param("tempoSource", 2);
        robodrummer::ArrangementSlot slot;
        slot.section.style = robodrummer::JamStyle::Disco; slot.section.bars = 12; slot.section.intensityTarget = 0.9f;
        slot.section.timeFeel = robodrummer::SectionTimeFeel::HalfTime; slot.section.transition = robodrummer::SectionTransition::Break;
        slot.section.personality = robodrummer::DrummerPersonality::Busy; slot.section.fillFrequency = 0.8f; slot.section.leadership = 0.25f;
        rig.processor.setArrangementSlot(2, slot);
        rig.processor.setArrangementName(2, "Big Chorus");
        rig.processor.getMidiLearn().bindCc(21, 5);
        rig.processor.getMidiLearn().bindNote(80, static_cast<int>(robodrummer::MidiCommand::Break), 3);
        rig.processor.setUserKitFolder(juce::File::getSpecialLocation(juce::File::tempDirectory));
        rig.processor.getStateInformation(state);
    }
    {
        Rig rig;
        rig.processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        CHECK(std::abs(rig.value("jamStyle") - 15.0f) < 0.01f);
        CHECK(std::abs(rig.value("humanize") - 0.77f) < 0.011f);
        CHECK(std::abs(rig.value("gain_kick") + 7.5f) < 0.11f);
        CHECK(rig.value("tempoSource") > 1.5f);
        const auto slot = rig.processor.getArrangementSlot(2);
        CHECK(slot.section.style == robodrummer::JamStyle::Disco && slot.section.bars == 12);
        CHECK(slot.section.timeFeel == robodrummer::SectionTimeFeel::HalfTime && slot.section.transition == robodrummer::SectionTransition::Break);
        CHECK(slot.section.personality == robodrummer::DrummerPersonality::Busy);
        CHECK(std::abs(slot.section.fillFrequency - 0.8f) < 1.0e-4f && std::abs(slot.section.leadership - 0.25f) < 1.0e-4f);
        CHECK(rig.processor.getArrangementName(2) == "Big Chorus");
        CHECK(rig.processor.getMidiLearn().ccForTarget(5) == 21);
        CHECK(rig.processor.getMidiLearn().commandForNote(80, 3) == static_cast<int>(robodrummer::MidiCommand::Break));
        CHECK(rig.processor.getUserKitFolder() == juce::File::getSpecialLocation(juce::File::tempDirectory));
        // garbage state must not crash or corrupt
        const char junk[] = "not xml";
        rig.processor.setStateInformation(junk, static_cast<int>(sizeof(junk)));
        CHECK(std::abs(rig.value("humanize") - 0.77f) < 0.011f);
    }
    {
        // Presets: a Drummer preset only touches its own parameters.
        Rig rig;
        using robodrummer::PresetManager; using robodrummer::PresetType;
        rig.param("humanize", 0.93f); rig.param("complexity", 0.11f); rig.param("jamStyle", 3);
        CHECK(PresetManager::saveUser(rig.processor, PresetType::Drummer, "harness test preset"));
        rig.param("humanize", 0.1f); rig.param("jamStyle", 7);
        bool found = false;
        for (const auto& e : PresetManager::list(PresetType::Drummer)) {
            if (e.name == "harness test preset" && !e.factory) { found = true; CHECK(PresetManager::load(rig.processor, PresetType::Drummer, e)); }
        }
        CHECK(found);
        CHECK(std::abs(rig.value("humanize") - 0.93f) < 0.011f && std::abs(rig.value("complexity") - 0.11f) < 0.011f);
        CHECK(std::abs(rig.value("jamStyle") - 7.0f) < 0.01f);
        CHECK(PresetManager::deleteUser(PresetType::Drummer, "harness test preset"));
        // Factory presets and programs.
        CHECK(PresetManager::factoryProgramCount() >= 8);
        rig.processor.setCurrentProgram(5);
        CHECK(rig.processor.getProgramName(5).isNotEmpty());
        for (int t = 0; t < robodrummer::kPresetTypeCount; ++t) {
            const auto type = static_cast<PresetType>(t);
            for (const auto& e : PresetManager::list(type)) if (e.factory) CHECK_MSG(PresetManager::load(rig.processor, type, e), "factory preset %s", e.name.toRawUTF8());
        }
        const auto song = rig.processor.getArrangementSlot(1);
        CHECK(song.section.bars > 0);
    }
}

void testProgrammedArrangement() {
    section("programmed arrangement");
    Rig rig; rig.param("outputMode", 1); rig.param("tempoSource", 1); rig.param("bpm", 240.0f);
    robodrummer::ArrangementSlot a, b;
    a.section.style = robodrummer::JamStyle::Rock; a.section.bars = 2; a.section.intensityTarget = 0.3f;
    b.section.style = robodrummer::JamStyle::Reggae; b.section.bars = 2; b.section.intensityTarget = 0.7f;
    b.section.timeFeel = robodrummer::SectionTimeFeel::HalfTime; b.section.transition = robodrummer::SectionTransition::Crash;
    rig.processor.setArrangementSlot(0, a); rig.processor.setArrangementSlot(1, b);
    for (int i = 2; i < 6; ++i) { robodrummer::ArrangementSlot off; off.enabled = false; rig.processor.setArrangementSlot(i, off); }
    rig.param("arrangementEnabled", 1);
    rig.runSilent(5);
    CHECK(rig.processor.getCurrentArrangementSection() == 0);
    rig.runSilent(200); // 240 BPM, 4/4: 1 bar = 1 s = ~94 blocks -> 2 bars passed
    CHECK_MSG(rig.processor.getCurrentArrangementSection() == 1, "section %d", rig.processor.getCurrentArrangementSection());
    CHECK_MSG(rig.processor.getTimeScale() < 0.75, "timeScale %f", rig.processor.getTimeScale());
    pump(150);
    CHECK_MSG(std::abs(rig.value("jamStyle") - 15.0f) < 0.01f, "style param %f", rig.value("jamStyle"));
    CHECK(rig.processor.isArrangementEnabled());
    // manual navigation
    rig.processor.requestCommand(robodrummer::MidiCommand::PreviousSection);
    rig.runSilent(2);
    CHECK(rig.processor.getCurrentArrangementSection() == 0);
}

void testExtendedControls() {
    section("follow controls, kit tuning, MIDI output map and channel");
    {
        Rig rig; rig.param("outputMode", 1); rig.param("midiOutChannel", 3.0f);
        rig.processor.setMidiDrumNote(robodrummer::DrumInstrument::Kick, 35);
        juce::Array<int> channels;
        bool sawKick35 = false;
        for (int b = 0; b < 200; ++b, ++rig.blockIndex) {
            rig.buffer.clear();
            juce::MidiBuffer midi;
            rig.processor.processBlock(rig.buffer, midi);
            for (const auto m : midi) if (m.getMessage().isNoteOn()) { channels.addIfNotAlreadyThere(m.getMessage().getChannel()); sawKick35 |= m.getMessage().getNoteNumber() == 35; }
        }
        CHECK(channels.size() == 1 && channels[0] == 3);
        CHECK(sawKick35);
        rig.processor.resetMidiDrumMap();
        CHECK(rig.processor.getMidiDrumNote(robodrummer::DrumInstrument::Kick) == 36);
    }
    {
        // Kit tuning changes the pitch (and so the audio) without changing the timing.
        Rig a, b; a.param("outputMode", 0); b.param("outputMode", 0); b.param("kitTune", 12.0f);
        a.runSilent(10); b.runSilent(10);
        const auto ra = a.runSilent(100), rb = b.runSilent(100);
        CHECK(ra.peak > 0.05 && rb.peak > 0.05);
    }
    {
        // Follow strength 0 removes all guitar authority.
        Rig rig; rig.param("outputMode", 1); rig.playHead.hasTempo = false; rig.param("tempoSource", 1); rig.param("bpm", 100.0f);
        rig.param("leadershipMode", 2); rig.param("followRange", 40.0f); rig.param("followStrength", 0.0f);
        rig.run(1200, [](long long s) { return pluck(s, 118.0); }, [](long long, juce::MidiBuffer&) {});
        CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 100.0) < 0.5, "bpm %f", rig.processor.getEffectiveDrummerBpm());
        CHECK(rig.processor.getEffectiveGuitarAuthority() < 0.01f);
        CHECK(rig.processor.getLookaheadMs() > 5.0);
        const auto rich = rig.processor.getRichHypotheses();
        CHECK(rich[0].bpm > 40.0 && rich[0].supportingEvents > 4);
        CHECK(rig.processor.getPerformanceState().regularity > 0.5f);
    }
    {
        // Drum map and new parameters round-trip through state.
        juce::MemoryBlock state;
        { Rig rig; rig.processor.setMidiDrumNote(robodrummer::DrumInstrument::Snare, 40); rig.param("tempoInertia", 0.9f); rig.processor.getStateInformation(state); }
        Rig rig; rig.processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        CHECK(rig.processor.getMidiDrumNote(robodrummer::DrumInstrument::Snare) == 40);
        CHECK(std::abs(rig.value("tempoInertia") - 0.9f) < 0.011f);
    }
}

void testCalibration() {
    section("calibration");
    Rig rig;
    rig.processor.startCalibration();
    std::uint32_t rng = 7;
    rig.run(800, [&](long long s) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) / 8388608.0f - 1.0f) * 0.002f;
        return noise + pluck(s, 100.0);
    }, [](long long, juce::MidiBuffer&) {});
    const auto cal = rig.processor.getCalibration();
    CHECK_MSG(cal.state == 2, "state %d", cal.state);
    CHECK(cal.result.valid && cal.result.noiseFloorDb < -50.0f && cal.result.onsetsPerSecond > 0.8f);
    rig.processor.applyCalibrationSuggestions();
    CHECK(rig.value("gateThreshold") > -90.0f);
}

void testRealtimeAllocations() {
    section("real-time allocation audit");
    Rig rig(true);
    rig.param("leadershipMode", 1);
    rig.param("tempoSource", 2);
    rig.param("arrangementEnabled", 1);
    rig.param("gateThreshold", -50.0f);
    rig.param("autoGain", 1);
    rig.param("humanize", 0.8f);
    rig.param("fillLength", 3);
    rig.param("gain_room", -6.0f);
    rig.playHead.hasTempo = true;
    auto midiFn = [](long long b, juce::MidiBuffer& m) {
        if (b % 3 == 0) m.addEvent(juce::MidiMessage(0xF8), 10);
        if (b == 3) m.addEvent(juce::MidiMessage(0xFA), 0);
        if (b % 97 == 0) m.addEvent(juce::MidiMessage::noteOn(16, 36, 1.0f), 5);
        if (b % 131 == 0) m.addEvent(juce::MidiMessage::noteOn(16, 44, 1.0f), 5);
        if (b % 149 == 0) m.addEvent(juce::MidiMessage::noteOn(16, 42, 1.0f), 5);
        if (b % 61 == 0) m.addEvent(juce::MidiMessage::controllerEvent(1, 11, static_cast<int>(b % 127)), 7);
        if (b % 173 == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 9);
    };
    // Warm-up: grows any lazily-sized internal storage once.
    rig.run(600, [](long long s) { return pluck(s, 110.0); }, midiFn);
    gAllocations = 0;
    gCounting = true;
    rig.run(1500, [](long long s) { return pluck(s, 110.0); }, midiFn);
    gCounting = false;
    CHECK_MSG(gAllocations.load() == 0, "%lld allocations on the audio thread", gAllocations.load());
}

void testCpuBudget() {
    section("CPU budget");
    Rig rig(true);
    rig.param("leadershipMode", 2); rig.param("gain_room", -6.0f);
    rig.run(200, [](long long s) { return pluck(s, 120.0); }, [](long long, juce::MidiBuffer&) {});
    const int blocks = 4000;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    rig.run(blocks, [](long long s) { return pluck(s, 120.0); }, [](long long, juce::MidiBuffer&) {});
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;
    const double audioMs = blocks * kBlock / kSampleRate * 1000.0;
    const double load = elapsedMs / audioMs;
    std::printf("  processing load: %.2f%% of real time (%.1f ms for %.0f ms of audio)\n", load * 100.0, elapsedMs, audioMs);
    CHECK_MSG(load < 0.25, "load %.3f", load);
}

void renderScreenshots(const juce::File& directory) {
    section("editor screenshots");
    directory.createDirectory();
    Rig rig;
    rig.param("leadershipMode", 1);
    rig.run(300, [](long long s) { return pluck(s, 118.0); }, [](long long, juce::MidiBuffer&) {});
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.processor.createEditor());
    CHECK(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(1040, 720);
    static const char* names[] = { "live", "kit", "drummer", "style", "song", "midi", "analysis", "settings" };
    auto& ed = static_cast<RoboDrummerAudioProcessorEditor&>(*editor);
    for (int tab = 0; tab < 8; ++tab) {
        // click the tab button via its visible child component
        int found = 0;
        for (auto* child : ed.getChildren()) {
            if (auto* button = dynamic_cast<juce::TextButton*>(child)) {
                if (button->getButtonText().equalsIgnoreCase(juce::String(juce::StringArray{"LIVE","KIT","DRUMMER","STYLE","SONG","MIDI","ANALYSIS","SETTINGS"}[tab]))) { button->triggerClick(); ++found; }
            }
        }
        CHECK(found == 1);
        int toggledTabs = 0;
        for (auto* child : ed.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child))
                if (button->getRadioGroupId() == 9001 && button->getToggleState()) ++toggledTabs;
        if (toggledTabs != 1) {
            for (auto* child : ed.getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*>(child))
                    if (button->getRadioGroupId() == 9001) std::printf("    tab %s toggled=%d clickToggles=%d\n", button->getButtonText().toRawUTF8(), button->getToggleState() ? 1 : 0, button->getClickingTogglesState() ? 1 : 0);
        }
        CHECK_MSG(toggledTabs == 1, "toggled tabs %d", toggledTabs);
        rig.run(60, [](long long s) { return pluck(s, 118.0); }, [](long long, juce::MidiBuffer&) {});
        pump(150);
        const auto image = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto file = directory.getChildFile(juce::String(names[tab]) + ".png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        if (out.openedOk()) png.writeImageToStream(image, out);
        CHECK(file.getSize() > 1000);
    }
}
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::File screenshotDir;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--screenshots") screenshotDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[i + 1]);

    testBasicPlayback();
    testHostTransportAndTempoSources();
    testMidiCommandsAndLearn();
    testGuitarFollowing();
    testMixerAndAux();
    testKitLoading();
    testStateAndPresets();
    testProgrammedArrangement();
    testExtendedControls();
    testCalibration();
    testRealtimeAllocations();
    testCpuBudget();
    if (screenshotDir != juce::File()) renderScreenshots(screenshotDir);

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
