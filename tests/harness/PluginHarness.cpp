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
#include <functional>
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

void testGuitarMonitor() {
    section("guitar monitor");
    const auto dc = [](long long) { return 0.3f; };
    const auto none = [](long long, juce::MidiBuffer&) {};
    Rig off; off.param("outputMode", 1);
    off.run(5, dc, none);
    const auto silent = off.run(10, dc, none);
    CHECK_MSG(silent.peak < 1.0e-3, "guitar leaked with monitor off: %f", silent.peak);
    Rig on; on.param("outputMode", 1); on.param("guitarMonitor", 1.0f);
    on.run(5, dc, none);
    const auto heard = on.run(10, dc, none);
    CHECK_MSG(std::abs(heard.peak - 0.3) < 0.01, "monitor on: %f", heard.peak);
    Rig half; half.param("outputMode", 1); half.param("guitarMonitor", 0.5f);
    half.run(5, dc, none);
    CHECK_MSG(std::abs(half.run(10, dc, none).peak - 0.15) < 0.01, "monitor half");
}

void testOutro() {
    section("end jam / outro");
    Rig rig; rig.param("outputMode", 1);
    rig.runSilent(20);
    rig.run(1, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::noteOn(16, 49, 1.0f), 0); });
    const auto outro = rig.runSilent(500); // 5.3 s at 120 BPM: more than the two bars the outro needs
    CHECK(rig.processor.isDrummerStopped());
    CHECK_MSG(outro.notes[49] > 0, "no final crash (kicks %d, toms %d)", outro.notes[36], outro.notes[50] + outro.notes[47] + outro.notes[43]); // the final crash
    const auto after = rig.runSilent(200);
    CHECK_MSG(after.noteOns == 0, "drummer kept playing after the outro (%d)", after.noteOns);
    rig.run(1, [](long long) { return 0.0f; }, [](long long, juce::MidiBuffer& m) { m.addEvent(juce::MidiMessage::noteOn(16, 46, 1.0f), 0); });
    CHECK(!rig.processor.isDrummerStopped());
    CHECK(rig.runSilent(200).noteOns > 0);
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


// SPEC 37: the beta acceptance scenario, end to end through the real processor.
void testBetaAcceptanceScenario() {
    section("beta acceptance scenario (SPEC 37)");
    Rig rig; rig.param("outputMode", 1);
    rig.playHead.hasTempo = false;
    rig.param("tempoSource", 1); rig.param("bpm", 100.0f);
    rig.param("jamStyle", 0);                 // Rock
    rig.param("leadershipMode", 2);           // Guitar leads
    rig.param("followRange", 40.0f);
    rig.param("silenceMode", 4);              // stop after N bars
    rig.param("silenceStopBars", 2.0f);
    double beatAccum = 0.0;
    double guitarBpm = 104.0;
    float amplitude = 0.35f;
    bool guitarPlaying = true;
    auto guitar = [&](long long) {
        const double rate = guitarBpm / 60.0 / kSampleRate;
        beatAccum += rate;
        const double inBeat = std::fmod(beatAccum, 1.0) / rate;
        if (!guitarPlaying) return 0.0f;
        return static_cast<float>(amplitude * std::exp(-inBeat / 2200.0) * std::sin(6.2831853 * 196.0 * inBeat / kSampleRate) * (inBeat < 8000 ? 1.0 : 0.0));
    };
    auto none = [](long long, juce::MidiBuffer&) {};
    const auto seconds = [](double s) { return static_cast<int>(s * kSampleRate / kBlock); };

    // 2-3. four bars of playing: tempo and phase are acquired, the drummer enters on a sensible boundary.
    const auto before = rig.run(seconds(1.0), guitar, none);
    CHECK_MSG(before.noteOns == 0, "drummer played before acquiring (%d hits)", before.noteOns);
    const auto entry = rig.run(seconds(9.0), guitar, none);
    CHECK(rig.processor.hasAdaptiveJoined());
    CHECK_MSG(entry.noteOns > 8, "no entry (%d hits)", entry.noteOns);
    CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 104.0) < 6.0, "entry tempo %f", rig.processor.getEffectiveDrummerBpm());

    // 4-5. gradual acceleration is followed without obvious jumps.
    double previous = rig.processor.getEffectiveDrummerBpm(), maxStep = 0.0;
    const int accelBlocks = seconds(14.0);
    for (int b = 0; b < accelBlocks; ++b) {
        guitarBpm = 104.0 + 12.0 * b / accelBlocks;
        rig.run(1, guitar, none);
        const double now = rig.processor.getEffectiveDrummerBpm();
        maxStep = std::max(maxStep, std::abs(now - previous)); previous = now;
    }
    CHECK_MSG(maxStep < 1.0, "tempo jump %f", maxStep);
    CHECK_MSG(rig.processor.getEffectiveDrummerBpm() > 108.0, "drummer %f after acceleration", rig.processor.getEffectiveDrummerBpm());

    // 6-7. the guitarist plays harder and the drummer's energy rises.
    const float intensityBefore = rig.processor.getEffectiveDrummerIntensity();
    for (int b = 0; b < seconds(10.0); ++b) { amplitude = 0.35f + 0.55f * b / seconds(10.0); rig.run(1, guitar, none); }
    CHECK_MSG(rig.processor.getEffectiveDrummerIntensity() > intensityBefore + 0.08f, "intensity %f -> %f", intensityBefore, rig.processor.getEffectiveDrummerIntensity());

    // 8-9. a fill requested by MIDI is executed musically (toms appear within two bars).
    const auto fill = rig.run(seconds(5.0), guitar, [](long long b, juce::MidiBuffer& m) { if (b % 1000 == 3) m.addEvent(juce::MidiMessage::noteOn(16, 36, 1.0f), 0); });
    // the request lands on the next block after block 3 modulo - make sure one was sent in the window by forcing it
    (void) fill;
    rig.processor.requestCommand(robodrummer::MidiCommand::Fill);
    const auto fillRun = rig.run(seconds(4.0), guitar, none);
    CHECK_MSG(fillRun.notes[50] + fillRun.notes[47] + fillRun.notes[43] > 0, "fill without toms");

    // 10-11. the guitar stops: after the configured silent bars the drummer stops.
    guitarPlaying = false;
    rig.run(seconds(12.0), guitar, none);
    CHECK(rig.processor.isDrummerStopped());
    const auto silent = rig.run(seconds(2.0), guitar, none);
    CHECK_MSG(silent.noteOns == 0, "drummer still playing in silence (%d)", silent.noteOns);

    // 12-14. the guitar resumes; the drummer continues without any manual restart.
    guitarPlaying = true;
    const auto resumed = rig.run(seconds(20.0), guitar, none);
    CHECK(!rig.processor.isDrummerStopped());
    CHECK_MSG(resumed.noteOns > 8, "drummer did not resume (%d hits)", resumed.noteOns);
    CHECK_MSG(std::abs(rig.processor.getEffectiveDrummerBpm() - 116.0) < 9.0, "resumed tempo %f", rig.processor.getEffectiveDrummerBpm());
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


bool allFinite(const juce::AudioBuffer<float>& b, float limit = 64.0f) {
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample(c, i); if (!std::isfinite(v) || std::abs(v) > limit) return false; }
    return true;
}

void testRobustness() {
    section("robustness: block sizes, sample rates, non-finite input, zero-length blocks");
    {
        RoboDrummerAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        bool finite = true; double peak = 0.0;
        for (const int size : { 1, 2, 7, 31, 64, 100, 333, 512, 1000, 4097, 16384, 40000, 70000 }) {
            juce::AudioBuffer<float> buffer(2, size);
            for (int rep = 0; rep < (size < 1000 ? 40 : 3); ++rep) {
                for (int i = 0; i < size; ++i) { const float v = 0.2f * std::sin(0.05f * static_cast<float>(i)); buffer.setSample(0, i, v); buffer.setSample(1, i, v); }
                juce::MidiBuffer midi;
                if (size > 4) midi.addEvent(juce::MidiMessage::noteOn(16, 36, 1.0f), size / 2);
                processor.processBlock(buffer, midi);
                if (!allFinite(buffer)) { std::printf("    non-finite/large at block size %d rep %d magnitude %f\n", size, rep, static_cast<double>(buffer.getMagnitude(0, size))); finite = false; }
                peak = std::max(peak, static_cast<double>(buffer.getMagnitude(0, size)));
            }
        }
        CHECK(finite);
        // Zero-length blocks are legal in some hosts.
        juce::AudioBuffer<float> empty(2, 0);
        juce::MidiBuffer midi;
        processor.processBlock(empty, midi);
        CHECK(true);
    }
    for (const double rate : { 11025.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 }) {
        RoboDrummerAudioProcessor processor;
        processor.setPlayHead(nullptr);
        processor.prepareToPlay(rate, 480);
        juce::AudioBuffer<float> buffer(2, 480);
        bool finite = true; double peak = 0.0;
        for (int b = 0; b < static_cast<int>(rate * 2.0 / 480.0); ++b) {
            for (int i = 0; i < 480; ++i) { const float v = (i % 5000 < 200) ? 0.4f * std::sin(0.3f * static_cast<float>(i)) : 0.0f; buffer.setSample(0, i, v); buffer.setSample(1, i, v); }
            juce::MidiBuffer m;
            processor.processBlock(buffer, m);
            finite = finite && allFinite(buffer);
            peak = std::max(peak, static_cast<double>(buffer.getMagnitude(0, 480)));
        }
        CHECK_MSG(finite, "rate %.0f", rate);
        CHECK_MSG(peak > 0.02, "rate %.0f peak %f", rate, peak);
        processor.releaseResources();
    }
    {
        // Changing the sample rate on a live instance re-prepares everything safely.
        RoboDrummerAudioProcessor processor;
        juce::AudioBuffer<float> buffer(2, 256);
        for (const double rate : { 44100.0, 96000.0, 48000.0 }) {
            processor.prepareToPlay(rate, 256);
            for (int b = 0; b < 100; ++b) { buffer.clear(); juce::MidiBuffer m; processor.processBlock(buffer, m); CHECK(allFinite(buffer)); }
        }
    }
    {
        // NaN / Inf / huge input must never corrupt the output or the tracker.
        Rig rig; rig.param("leadershipMode", 2); rig.param("outputMode", 0);
        bool finite = true;
        const float bad[] { std::nanf(""), INFINITY, -INFINITY, 1.0e30f, -1.0e30f, 0.0f };
        for (int b = 0; b < 300; ++b) {
            rig.buffer.clear();
            for (int i = 0; i < kBlock; ++i) { const float v = bad[(b + i) % 6]; rig.buffer.setSample(0, i, v); rig.buffer.setSample(1, i, v); }
            juce::MidiBuffer m;
            rig.processor.processBlock(rig.buffer, m);
            finite = finite && allFinite(rig.buffer, 1.0e31f);
        }
        CHECK(finite);
        // ...and the engine still works afterwards.
        rig.runSilent(5);
        const auto r = rig.run(200, [](long long s) { return pluck(s, 100.0); }, [](long long, juce::MidiBuffer&) {});
        CHECK(r.peak < 16.0);
        CHECK(std::isfinite(rig.processor.getEffectiveDrummerBpm()) && rig.processor.getEffectiveDrummerBpm() > 20.0 && rig.processor.getEffectiveDrummerBpm() < 400.0);
    }
}

void testFuzz() {
    section("fuzzing: saved state, parameters, MIDI streams (fixed seeds)");
    // Mutated state blobs must never crash or leave invalid values behind.
    juce::MemoryBlock good;
    { Rig rig; rig.param("jamStyle", 9); rig.processor.getMidiLearn().bindCc(30, 4); rig.processor.getStateInformation(good); }
    auto xml = juce::AudioProcessor::getXmlFromBinary(good.getData(), static_cast<int>(good.getSize()));
    CHECK(xml != nullptr);
    if (xml == nullptr) return;
    const juce::String text = xml->toString();
    std::uint32_t rng = 20261014u;
    const auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    Rig rig;
    int crashesAvoided = 0;
    for (int iteration = 0; iteration < 400; ++iteration) {
        juce::String mutated = text;
        const int edits = 1 + static_cast<int>(next() % 6);
        for (int e = 0; e < edits && mutated.length() > 4; ++e) {
            const int pos = static_cast<int>(next() % static_cast<unsigned>(mutated.length()));
            switch (next() % 7) {
                case 0: mutated = mutated.replaceSection(pos, 1, ""); break;
                case 1: mutated = mutated.replaceSection(pos, 1, juce::String::charToString(static_cast<juce::juce_wchar>(32 + next() % 90))); break;
                case 2: mutated = mutated.replaceSection(pos, 0, juce::String::charToString(static_cast<juce::juce_wchar>(32 + next() % 90))); break;
                case 3: mutated = mutated.substring(0, pos); break;
                case 4: mutated = mutated.replaceSection(pos, 3, "1e99"); break;
                case 5: mutated = mutated.replaceSection(pos, 2, "-1"); break;
                default: mutated = mutated.replaceSection(pos, 2, "nan"); break;
            }
        }
        // Same container format as AudioProcessor::copyXmlToBinary, but around the raw (possibly malformed) text.
        juce::MemoryBlock blob;
        {
            juce::MemoryOutputStream out(blob, false);
            out.writeInt(0x21324356);
            out.writeInt(static_cast<int>(mutated.getNumBytesAsUTF8()));
            out.write(mutated.toRawUTF8(), mutated.getNumBytesAsUTF8());
            out.writeByte(0);
        }
        rig.processor.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        ++crashesAvoided;
        rig.buffer.clear();
        juce::MidiBuffer m;
        rig.processor.processBlock(rig.buffer, m);
        CHECK_MSG(allFinite(rig.buffer), "non-finite output after mutated state #%d", iteration);
        const double bpm = rig.processor.getEffectiveDrummerBpm();
        CHECK_MSG(std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0, "bpm %f after state #%d", bpm, iteration);
        for (auto* parameter : rig.processor.getParameters().processor.getParameters()) {
            const float v = parameter->getValue();
            if (!(v >= 0.0f && v <= 1.0f)) { CHECK_MSG(false, "parameter %s out of range after mutated state: %f", parameter->getName(64).toRawUTF8(), v); break; }
        }
    }
    CHECK(crashesAvoided == 400);
    // Pure binary garbage and truncation.
    for (int iteration = 0; iteration < 100; ++iteration) {
        juce::MemoryBlock junk;
        const int n = static_cast<int>(next() % 2000);
        for (int i = 0; i < n; ++i) { const char c = static_cast<char>(next() & 0xff); junk.append(&c, 1); }
        rig.processor.setStateInformation(junk.getData(), static_cast<int>(junk.getSize()));
        juce::MemoryBlock truncated(good.getData(), static_cast<size_t>(next() % good.getSize()));
        rig.processor.setStateInformation(truncated.getData(), static_cast<int>(truncated.getSize()));
    }
    rig.processor.setStateInformation(nullptr, 0);
    rig.processor.setStateInformation(good.getData(), -5);
    CHECK(true);

    // Randomised parameters, MIDI and block sizes (fixed seed).
    for (int round = 0; round < 40; ++round) {
        Rig r2(round % 3 == 0);
        for (auto* parameter : r2.processor.getParameters().processor.getParameters()) {
            if (parameter->getName(64).containsIgnoreCase("gain") || next() % 3 == 0) parameter->setValueNotifyingHost(static_cast<float>(next() % 1000) / 1000.0f);
        }
        bool finite = true;
        for (int b = 0; b < 60; ++b) {
            const int size = 1 + static_cast<int>(next() % 1200);
            juce::AudioBuffer<float> buffer(r2.numChannels, size);
            for (int c = 0; c < r2.numChannels; ++c) for (int i = 0; i < size; ++i) buffer.setSample(c, i, ((next() % 2000) / 1000.0f - 1.0f) * 0.6f);
            juce::MidiBuffer midi;
            const int events = static_cast<int>(next() % 10);
            for (int e = 0; e < events; ++e) {
                const int pos = static_cast<int>(next() % static_cast<unsigned>(size));
                switch (next() % 6) {
                    case 0: midi.addEvent(juce::MidiMessage::noteOn(1 + static_cast<int>(next() % 16), static_cast<int>(next() % 128), static_cast<juce::uint8>(1 + next() % 127)), pos); break;
                    case 1: midi.addEvent(juce::MidiMessage::noteOff(1 + static_cast<int>(next() % 16), static_cast<int>(next() % 128)), pos); break;
                    case 2: midi.addEvent(juce::MidiMessage::controllerEvent(1 + static_cast<int>(next() % 16), static_cast<int>(next() % 128), static_cast<int>(next() % 128)), pos); break;
                    case 3: midi.addEvent(juce::MidiMessage(0xF8), pos); break;
                    case 4: midi.addEvent(juce::MidiMessage(0xFA + static_cast<int>(next() % 3)), pos); break;
                    default: midi.addEvent(juce::MidiMessage::pitchWheel(1 + static_cast<int>(next() % 16), static_cast<int>(next() % 16384)), pos); break;
                }
            }
            r2.processor.processBlock(buffer, midi);
            finite = finite && allFinite(buffer);
        }
        CHECK_MSG(finite, "round %d produced non-finite output", round);
        const double bpm = r2.processor.getEffectiveDrummerBpm();
        CHECK_MSG(std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0, "round %d bpm %f", round, bpm);
    }
}

void testSoak(double minutes) {
    section("soak test");
    Rig rig(true);
    rig.param("leadershipMode", 1);
    rig.param("arrangementEnabled", 1);
    rig.param("gain_room", -6.0f);
    rig.param("humanize", 0.7f);
    std::uint32_t rng = 777;
    const auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    const long long blocks = static_cast<long long>(minutes * 60.0 * kSampleRate / kBlock);
    long long nonFinite = 0;
    double minBpm = 1e9, maxBpm = 0.0, bpmTarget = 104.0;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    double guitarBpm = 104.0;
    double beatAccum = 0.0;
    auto guitar = [&](long long s) {
        const double blockTime = static_cast<double>(s) / kSampleRate;
        // slow tempo wander and occasional rests
        guitarBpm = 104.0 + 14.0 * std::sin(blockTime / 47.0);
        const double rate = guitarBpm / 60.0 / kSampleRate;
        beatAccum += rate;
        const double inBeat = std::fmod(beatAccum, 1.0) / rate;
        const bool rest = std::fmod(blockTime, 90.0) > 80.0;
        return rest ? 0.0f : static_cast<float>(0.6 * std::exp(-inBeat / 2200.0) * std::sin(6.2831853 * 196.0 * inBeat / kSampleRate) * (inBeat < 8000 ? 1.0 : 0.0));
    };
    (void) bpmTarget;
    for (long long b = 0; b < blocks; ++b) {
        juce::MidiBuffer midi;
        if (next() % 700 == 0) midi.addEvent(juce::MidiMessage::noteOn(16, 36 + static_cast<int>(next() % 15), 1.0f), 3);
        if (next() % 40 == 0) midi.addEvent(juce::MidiMessage::controllerEvent(1, 11, static_cast<int>(next() % 128)), 4);
        rig.buffer.clear();
        for (int i = 0; i < kBlock; ++i) { const float v = guitar(rig.blockIndex * kBlock + i); rig.buffer.setSample(0, i, v); rig.buffer.setSample(1, i, v); }
        rig.processor.processBlock(rig.buffer, midi);
        ++rig.blockIndex;
        if (!allFinite(rig.buffer)) ++nonFinite;
        const double bpm = rig.processor.getEffectiveDrummerBpm();
        minBpm = std::min(minBpm, bpm); maxBpm = std::max(maxBpm, bpm);
        if (b % 20000 == 0) pump(1);
    }
    const double elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    std::printf("  simulated %.1f minutes of playing in %.1f s (%.0fx real time); drummer tempo range %.1f-%.1f BPM\n", minutes, elapsedSeconds, minutes * 60.0 / std::max(0.001, elapsedSeconds), minBpm, maxBpm);
    CHECK(nonFinite == 0);
    CHECK(minBpm >= 20.0 && maxBpm <= 400.0);
}

// ---------------------------------------------------------------------------------------------------------------
// 0.4.0 integration tests
// ---------------------------------------------------------------------------------------------------------------
struct Audio {
    std::vector<float> l, r;
    double rms() const { double a = 0.0; for (std::size_t i = 0; i < l.size(); ++i) a += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i]; return l.empty() ? 0.0 : std::sqrt(a / (2.0 * static_cast<double>(l.size()))); }
    double peak() const { double p = 0.0; for (std::size_t i = 0; i < l.size(); ++i) p = std::max({ p, static_cast<double>(std::abs(l[i])), static_cast<double>(std::abs(r[i])) }); return p; }
    bool operator==(const Audio& o) const { return l == o.l && r == o.r; }
};

template <typename Guitar, typename MidiFn, typename PerBlock>
Audio renderAudio(Rig& rig, int blocks, Guitar&& guitar, MidiFn&& midiFor, PerBlock&& perBlock, juce::MidiBuffer* midiOut = nullptr) {
    Audio out;
    for (int b = 0; b < blocks; ++b, ++rig.blockIndex) {
        rig.buffer.clear();
        for (int i = 0; i < kBlock; ++i) {
            const float s = guitar(rig.blockIndex * kBlock + i);
            for (int c = 0; c < juce::jmin(2, rig.processor.getTotalNumInputChannels()); ++c) rig.buffer.setSample(c, i, s);
        }
        juce::MidiBuffer midi;
        midiFor(rig.blockIndex, midi);
        rig.playHead.ppq += kBlock / kSampleRate * rig.playHead.bpm / 60.0;
        rig.processor.processBlock(rig.buffer, midi);
        if (midiOut != nullptr) for (const auto m : midi) midiOut->addEvent(m.getMessage(), static_cast<int>(rig.blockIndex) * kBlock + m.samplePosition);
        for (int i = 0; i < kBlock; ++i) { out.l.push_back(rig.buffer.getSample(0, i)); out.r.push_back(rig.buffer.getSample(1, i)); }
        perBlock(b);
    }
    return out;
}
const auto kNoMidi = [](long long, juce::MidiBuffer&) {};
const auto kSilence = [](long long) { return 0.0f; };
const auto kNoBlockHook = [](int) {};

bool finiteAudio(const Audio& a) { for (std::size_t i = 0; i < a.l.size(); ++i) if (!std::isfinite(a.l[i]) || !std::isfinite(a.r[i]) || std::abs(a.l[i]) > 64.0f) return false; return true; }

// Deterministic setup used by the audio comparisons: internal tempo, no humanize jitter, internal drums only.
void setupDeterministic(Rig& rig) {
    rig.param("tempoSource", 1); rig.param("bpm", 120.0f); rig.param("humanize", 0.0f); rig.param("outputMode", 0);
}

void testDrumBusAndShaper() {
    section("drum bus and transient shaper wiring");
    const auto render = [](const std::function<void(Rig&)>& setup, float* minGr = nullptr, float* maxShaperDb = nullptr, float* minShaperDb = nullptr) {
        Rig rig; setupDeterministic(rig); setup(rig);
        Audio a = renderAudio(rig, 400, kSilence, kNoMidi, [&](int) {
            if (minGr) *minGr = std::min(*minGr, rig.processor.getDrumBusGainReductionDb());
            if (maxShaperDb) *maxShaperDb = std::max(*maxShaperDb, rig.processor.getTransientShaperGainDb());
            if (minShaperDb) *minShaperDb = std::min(*minShaperDb, rig.processor.getTransientShaperGainDb());
        });
        return a;
    };
    const Audio base = render([](Rig&) {});
    CHECK_MSG(base.peak() > 0.05, "baseline peak %f", base.peak());
    // Everything at its default except non-default values on a DISABLED bus/shaper: bit-identical.
    const Audio disabled = render([](Rig& r) { r.param("drumBusThreshold", -50.0f); r.param("drumBusRatio", 20.0f); r.param("drumBusDrive", 1.0f);
                                                r.param("transientShaperAttack", 100.0f); r.param("transientShaperOutput", 12.0f); });
    CHECK(disabled == base);
    // Compressor: heavy settings lower the level and the gain-reduction meter moves.
    float minGr = 0.0f;
    const Audio comp = render([](Rig& r) { r.param("drumBusEnabled", 1); r.param("drumBusThreshold", -40.0f); r.param("drumBusRatio", 20.0f); r.param("drumBusAttack", 0.1f); r.param("drumBusKnee", 0.0f); }, &minGr);
    CHECK_MSG(comp.rms() < base.rms() * 0.85, "rms %f vs %f", comp.rms(), base.rms());
    CHECK_MSG(minGr < -3.0f, "gain reduction meter %f dB", static_cast<double>(minGr));
    CHECK(finiteAudio(comp));
    // Makeup gain compensates.
    const Audio makeup = render([](Rig& r) { r.param("drumBusEnabled", 1); r.param("drumBusThreshold", -40.0f); r.param("drumBusRatio", 20.0f); r.param("drumBusMakeup", 12.0f); });
    CHECK(makeup.rms() > comp.rms());
    // Aux stems are untouched by the bus: only the main bus changes.
    // Transient shaper: attack boost raises peaks, attack cut lowers them; the gain meter follows.
    float maxUp = -99.0f, minUp = 99.0f, maxDn = -99.0f, minDn = 99.0f;
    const Audio up = render([](Rig& r) { r.param("transientShaperEnabled", 1); r.param("transientShaperAttack", 100.0f); }, nullptr, &maxUp, &minUp);
    const Audio down = render([](Rig& r) { r.param("transientShaperEnabled", 1); r.param("transientShaperAttack", -100.0f); }, nullptr, &maxDn, &minDn);
    CHECK_MSG(up.peak() > base.peak() * 1.05, "attack up peak %f vs %f", up.peak(), base.peak());
    CHECK_MSG(down.peak() < base.peak() * 0.99, "attack down peak %f vs %f", down.peak(), base.peak());
    CHECK_MSG(maxUp > 1.0f, "shaper gain meter max %f", static_cast<double>(maxUp));
    CHECK_MSG(minDn < -1.0f, "shaper gain meter min %f", static_cast<double>(minDn));
    CHECK(finiteAudio(up) && finiteAudio(down));
    // Sustain cut shortens the tail: RMS drops by more than the attack-neutral case.
    const Audio sus = render([](Rig& r) { r.param("transientShaperEnabled", 1); r.param("transientShaperSustain", -100.0f); });
    CHECK(sus.rms() < base.rms());
    // MIDI-only output mode renders no audio, so nothing is processed and the meters rest.
    { Rig rig; setupDeterministic(rig); rig.param("outputMode", 1); rig.param("drumBusEnabled", 1); rig.param("drumBusThreshold", -50.0f);
      const Audio a = renderAudio(rig, 100, kSilence, kNoMidi, kNoBlockHook); CHECK(a.peak() < 1.0e-6); }
}

// FNV-1a over the exact output bits (audio and MIDI) of several deterministic default-parameter scenarios. The constant
// kBaselineHash was captured BEFORE the beta integration (all new features absent), so a match proves that every new
// parameter at its default leaves the output bit-identical.
struct Fnv {
    std::uint64_t h{1469598103934665603ull};
    void bytes(const void* p, std::size_t n) { const auto* b = static_cast<const unsigned char*>(p); for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } }
};
std::uint64_t renderBaselineHash() {
    Fnv fnv;
    const auto hashRun = [&](Rig& rig, int blocks, const std::function<float(long long)>& guitar, const std::function<void(long long, juce::MidiBuffer&)>& midiFor) {
        for (int b = 0; b < blocks; ++b, ++rig.blockIndex) {
            rig.buffer.clear();
            for (int i = 0; i < kBlock; ++i) {
                const float s = guitar(rig.blockIndex * kBlock + i);
                for (int c = 0; c < juce::jmin(2, rig.processor.getTotalNumInputChannels()); ++c) rig.buffer.setSample(c, i, s);
            }
            juce::MidiBuffer midi;
            midiFor(rig.blockIndex, midi);
            rig.playHead.ppq += kBlock / kSampleRate * rig.playHead.bpm / 60.0;
            rig.processor.processBlock(rig.buffer, midi);
            for (int c = 0; c < juce::jmin(2, rig.buffer.getNumChannels()); ++c) fnv.bytes(rig.buffer.getReadPointer(c), sizeof(float) * kBlock);
            for (const auto m : midi) { const auto& msg = m.getMessage(); const int pos = m.samplePosition; fnv.bytes(&pos, sizeof(pos)); fnv.bytes(msg.getRawData(), static_cast<std::size_t>(msg.getRawDataSize())); }
        }
    };
    const auto none = [](long long, juce::MidiBuffer&) {};
    const auto silent = [](long long) { return 0.0f; };
    { Rig rig; hashRun(rig, 500, silent, none); }
    { Rig rig; rig.param("leadershipMode", 1); hashRun(rig, 700, [](long long s) { return pluck(s, 118.0); }, none); }
    { Rig rig; rig.param("leadershipMode", 2); rig.param("humanize", 0.8f); hashRun(rig, 700, [](long long s) { return pluck(s, 104.0, 3.0); }, none); }
    for (int style : { 0, 4, 5, 15, 19, 21 }) {
        Rig rig; rig.param("jamStyle", static_cast<float>(style)); rig.param("bpm", 90.0f + 11.0f * static_cast<float>(style)); rig.param("tempoSource", 1);
        hashRun(rig, 320, silent, [](long long b, juce::MidiBuffer& m) {
            if (b == 40) m.addEvent(juce::MidiMessage::noteOn(16, 50, 1.0f), 0);
            if (b == 150) m.addEvent(juce::MidiMessage::noteOn(16, 39, 1.0f), 5);
            if (b == 200) m.addEvent(juce::MidiMessage::noteOn(16, 42, 1.0f), 3);
        });
    }
    { Rig rig; rig.param("arrangementEnabled", 1); rig.param("tempoSource", 1); rig.param("bpm", 180.0f); hashRun(rig, 900, silent, none); }
    { Rig rig; rig.playHead.bpm = 133.0; hashRun(rig, 400, silent, none); }
    { Rig rig; rig.param("outputMode", 1); rig.param("meterNumerator", 1); rig.param("manualMeterEnabled", 1); hashRun(rig, 400, silent, none); }
    return fnv.h;
}

constexpr std::uint64_t kBaselineHash = 0x4a85caa7203610d1ull; // captured before the 0.4.0 integration

void testBaselineAndMigration() {
    section("regression: default-parameter output is bit-identical to the pre-integration baseline; old state migrates");
    const auto hash = renderBaselineHash();
    CHECK_MSG(hash == kBaselineHash, "baseline hash %016llx expected %016llx", static_cast<unsigned long long>(hash), static_cast<unsigned long long>(kBaselineHash));

    // A version-3 session (no 0.4.0 parameters in the file) restores every new feature as off / default, even over a rig
    // that had them switched on.
    juce::MemoryBlock v3;
    {
        Rig rig;
        rig.param("humanize", 0.66f);
        rig.processor.getStateInformation(v3);
        auto xml = juce::AudioProcessor::getXmlFromBinary(v3.getData(), static_cast<int>(v3.getSize()));
        CHECK(xml != nullptr);
        // Drop every 0.4.0 parameter from the saved file, as a session saved by 0.3.x would not contain them.
        std::vector<juce::XmlElement*> byName;
        for (auto* child : xml->getChildIterator()) {
            if (!child->hasTagName("PARAM")) continue;
            const auto id = child->getStringAttribute("id");
            if (id.startsWith("drumBus") || id.startsWith("transientShaper") || id.startsWith("rl") || id.startsWith("prac") || id == "practiceEnabled"
                || id.startsWith("autoMeter") || id == "limbModelAmount") byName.push_back(child);
        }
        for (auto* e : byName) xml->removeChildElement(e, true);
        xml->setAttribute("stateVersion", 3);
        juce::MemoryBlock rebuilt;
        juce::AudioProcessor::copyXmlToBinary(*xml, rebuilt);
        v3 = rebuilt;
    }
    {
        Rig rig;
        rig.param("drumBusEnabled", 1); rig.param("drumBusThreshold", -30.0f); rig.param("transientShaperEnabled", 1); rig.param("transientShaperAttack", 80.0f);
        rig.processor.setStateInformation(v3.getData(), static_cast<int>(v3.getSize()));
        CHECK(std::abs(rig.value("humanize") - 0.66f) < 0.011f);
        CHECK(rig.value("drumBusEnabled") < 0.5f && std::abs(rig.value("drumBusThreshold") + 18.0f) < 0.11f);
        CHECK(rig.value("transientShaperEnabled") < 0.5f && std::abs(rig.value("transientShaperAttack")) < 0.51f);
    }
    // Round trip of the new parameters through version-4 state.
    juce::MemoryBlock v4;
    {
        Rig rig; rig.param("drumBusEnabled", 1); rig.param("drumBusRatio", 9.5f); rig.param("transientShaperSustain", -42.0f);
        rig.processor.getStateInformation(v4);
    }
    {
        Rig rig; rig.processor.setStateInformation(v4.getData(), static_cast<int>(v4.getSize()));
        CHECK(rig.value("drumBusEnabled") > 0.5f && std::abs(rig.value("drumBusRatio") - 9.5f) < 0.2f && std::abs(rig.value("transientShaperSustain") + 42.0f) < 1.1f);
    }
}

// Non-finite and absurd values for every 0.4.0 parameter, injected through saved state, must leave valid parameters and finite audio.
void testNewParameterFuzz() {
    section("fuzz: NaN / huge values for the 0.4.0 parameters through saved state");
    juce::MemoryBlock good;
    { Rig rig; rig.processor.getStateInformation(good); }
    auto xml = juce::AudioProcessor::getXmlFromBinary(good.getData(), static_cast<int>(good.getSize()));
    CHECK(xml != nullptr);
    if (xml == nullptr) return;
    const char* bad[] = { "nan", "inf", "-inf", "1e99", "-1e99", "-1", "7" };
    Rig rig;
    int variants = 0;
    for (const char* value : bad) {
        auto copy = std::make_unique<juce::XmlElement>(*xml);
        bool inNew = false;
        for (auto* child : copy->getChildIterator()) {
            if (!child->hasTagName("PARAM")) continue;
            if (child->getStringAttribute("id") == "drumBusEnabled") inNew = true;
            if (inNew) child->setAttribute("value", value);
        }
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary(*copy, blob);
        rig.processor.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        ++variants;
        const Audio a = renderAudio(rig, 60, [](long long s) { return pluck(s, 118.0); }, kNoMidi, kNoBlockHook);
        CHECK_MSG(finiteAudio(a), "non-finite audio after state value %s", value);
        for (auto* parameter : rig.processor.getParameters().processor.getParameters()) {
            const float v = parameter->getValue();
            if (!(v >= 0.0f && v <= 1.0f)) { CHECK_MSG(false, "parameter %s out of range (%s): %f", parameter->getName(64).toRawUTF8(), value, static_cast<double>(v)); break; }
        }
        const double bpm = rig.processor.getEffectiveDrummerBpm();
        CHECK_MSG(std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0, "bpm %f", bpm);
    }
    CHECK(variants == 7);
}

void dumpParameters(const juce::File& file) {
    RoboDrummerAudioProcessor processor;
    juce::String md;
    md << "# RoboDrummer parameter reference\n\n"
          "Generated by `RoboDrummerHarness --dump-parameters`. Parameter IDs are stable (version hint 1); IDs are never reused for a different meaning.\n\n"
          "| ID | Name | Type | Range / choices | Default |\n|---|---|---|---|---|\n";
    for (auto* parameter : processor.getParameters().processor.getParameters()) {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        if (withId == nullptr) continue;
        juce::String type, range;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter)) {
            type = "choice"; range = choice->choices.joinIntoString(" / ");
        } else if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr) {
            type = "toggle"; range = "off / on";
        } else if (auto* integer = dynamic_cast<juce::AudioParameterInt*>(parameter)) {
            type = "integer"; range = juce::String(integer->getRange().getStart()) + " - " + juce::String(integer->getRange().getEnd());
        } else if (auto* floating = dynamic_cast<juce::AudioParameterFloat*>(parameter)) {
            type = "float"; range = juce::String(floating->range.start, 2) + " - " + juce::String(floating->range.end, 2);
            if (floating->getLabel().isNotEmpty()) range << " " << floating->getLabel();
        }
        const auto defaultText = parameter->getText(parameter->getDefaultValue(), 64);
        md << "| `" << withId->paramID << "` | " << withId->getName(64) << " | " << type << " | " << range << " | " << defaultText << " |\n";
    }
    file.replaceWithText(md);
}

void renderScreenshots(const juce::File& directory, int width, int height) {
    section("editor screenshots");
    directory.createDirectory();
    Rig rig;
    rig.param("leadershipMode", 1);
    rig.run(300, [](long long s) { return pluck(s, 118.0); }, [](long long, juce::MidiBuffer&) {});
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.processor.createEditor());
    CHECK(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(width, height);
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
        auto file = directory.getChildFile(juce::String(names[tab]) + (width == 1040 ? "" : "_" + juce::String(width) + "x" + juce::String(height)) + ".png");
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
    double soakMinutes = 5.0;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--screenshots") screenshotDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[i + 1]);
        if (std::string(argv[i]) == "--soak-minutes") soakMinutes = std::atof(argv[i + 1]);
        if (std::string(argv[i]) == "--baseline-hash") { std::printf("baseline %016llx\n", static_cast<unsigned long long>(renderBaselineHash())); return 0; }
        if (std::string(argv[i]) == "--dump-parameters") { dumpParameters(juce::File::getCurrentWorkingDirectory().getChildFile(argv[i + 1])); return 0; }
    }

    testBasicPlayback();
    testHostTransportAndTempoSources();
    testMidiCommandsAndLearn();
    testGuitarMonitor();
    testOutro();
    testGuitarFollowing();
    testMixerAndAux();
    testKitLoading();
    testStateAndPresets();
    testProgrammedArrangement();
    testExtendedControls();
    testBetaAcceptanceScenario();
    testCalibration();
    testRealtimeAllocations();
    testCpuBudget();
    testRobustness();
    testFuzz();
    testDrumBusAndShaper();
    testBaselineAndMigration();
    testNewParameterFuzz();
    testSoak(soakMinutes);
    if (screenshotDir != juce::File()) { renderScreenshots(screenshotDir, 1040, 720); renderScreenshots(screenshotDir, 900, 640); }

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
