// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetManager.h"
#include "analysis/DynamicsFollower.h"
#include "analysis/PhraseGrooveModifier.h"
#include "core/LiveControlPolicy.h"
#include "core/StyleLibrary.h"
#include <array>
#include <chrono>
#include <cmath>

namespace robodrummer {
const std::array<LearnTarget, kLearnTargetCount> kLearnTargets {{
    { ParamIDs::intensity, "Intensity" },
    { ParamIDs::leadership, "Leadership" },
    { ParamIDs::bpm, "Internal BPM" },
    { ParamIDs::followRange, "Follow Range" },
    { ParamIDs::dynamicFollow, "Dynamic Follow" },
    { ParamIDs::humanize, "Humanize" },
    { ParamIDs::feel, "Feel (push / lay back)" },
    { ParamIDs::complexity, "Complexity" },
    { ParamIDs::fillFrequency, "Fill Frequency" },
    { ParamIDs::fillComplexity, "Fill Complexity" },
    { ParamIDs::fillSurprise, "Fill Surprise" },
    { ParamIDs::crashTendency, "Crash Tendency" },
    { ParamIDs::roomSize, "Room Size" },
    { ParamIDs::mixGain[0], "Kick Level" },
    { ParamIDs::mixGain[1], "Snare Level" },
    { ParamIDs::mixGain[2], "Toms Level" },
    { ParamIDs::mixGain[3], "Hi-Hat Level" },
    { ParamIDs::mixGain[4], "Ride Level" },
    { ParamIDs::mixGain[5], "Cymbals Level" },
    { ParamIDs::mixGain[6], "Room Level" },
    { ParamIDs::mixGain[7], "Master Level" },
    { ParamIDs::leadershipMode, "Timing Mode" },
    { ParamIDs::jamStyle, "Jam Style" },
    { ParamIDs::outputMode, "Output Mode" },
    { ParamIDs::drummer, "Drummer Personality" },
    { ParamIDs::kit, "Kit" },
    { ParamIDs::tempoSource, "Tempo Source" },
    { ParamIDs::followResponse, "Follow Response" },
    { ParamIDs::fillLength, "Fill Length" },
    { ParamIDs::arrangementEnabled, "Programmed Arrangement" },
    { ParamIDs::inputGain, "Input Gain" },
    { ParamIDs::gateThreshold, "Noise Gate" },
    { ParamIDs::silenceMode, "Guitar Silence" },
    { ParamIDs::jamMemoryEnabled, "Learn This Jam" }
}};

const std::array<const char*, MidiLearnMap::CommandCount> kCommandNames {
    "Fill", "Next Section", "Previous Section", "Crash", "Intensity Up", "Intensity Down", "Half-Time", "Double-Time",
    "Break", "Stop", "Resume", "Reset Listening", "Solo Support", "End Jam", "Immediate Fill",
    "Practice On/Off", "Loop On/Off", "Trainer On/Off", "Trainer Restart", "Count-in On/Off"
};
}

namespace {
constexpr std::array<const char*, 15> kParameterIds {
    robodrummer::ParamIDs::bpm,
    robodrummer::ParamIDs::intensity,
    robodrummer::ParamIDs::outputMode,
    robodrummer::ParamIDs::leadershipMode,
    robodrummer::ParamIDs::leadership,
    robodrummer::ParamIDs::followRange,
    robodrummer::ParamIDs::dynamicFollow,
    robodrummer::ParamIDs::jamStyle,
    robodrummer::ParamIDs::arrangementEnabled,
    robodrummer::ParamIDs::jamMemoryEnabled,
    robodrummer::ParamIDs::manualMeterEnabled,
    robodrummer::ParamIDs::meterNumerator,
    robodrummer::ParamIDs::meterDenominator,
    robodrummer::ParamIDs::silenceMode,
    robodrummer::ParamIDs::silenceStopBars
};

constexpr std::array<int, 4> kMeterDenominators { 2, 4, 8, 16 };
constexpr int kMinMeterNumerator = 2;
constexpr int kMaxMeterNumerator = 12;
constexpr int kCommandCount = robodrummer::MidiLearnMap::CommandCount;

int denominatorToIndex(int denominator) noexcept {
    for (std::size_t i = 0; i < kMeterDenominators.size(); ++i)
        if (kMeterDenominators[i] == denominator) return static_cast<int>(i);
    return 1;
}

namespace PracIdx {
enum : std::size_t { Enabled, CountLen, CountSound, CountStartOnly, LoopOn, LoopMode, LoopBars, SecFirst, SecLast, TrainOn, TrainStart,
                     TrainTarget, TrainStep, TrainEvery, TrainUnit, TrainDir, TrainGate, TrainGateMin, TrainRest, TrainStopEnd };
}
constexpr std::array<const char*, 20> kPracticeIds {
    robodrummer::ParamIDs::practiceEnabled, robodrummer::ParamIDs::pracCountInLen, robodrummer::ParamIDs::pracCountInSound, robodrummer::ParamIDs::pracCountInStartOnly, robodrummer::ParamIDs::pracLoopOn,
    robodrummer::ParamIDs::pracLoopMode, robodrummer::ParamIDs::pracLoopBars, robodrummer::ParamIDs::pracLoopSecFirst, robodrummer::ParamIDs::pracLoopSecLast, robodrummer::ParamIDs::pracTrainOn,
    robodrummer::ParamIDs::pracTrainStart, robodrummer::ParamIDs::pracTrainTarget, robodrummer::ParamIDs::pracTrainStep, robodrummer::ParamIDs::pracTrainEvery, robodrummer::ParamIDs::pracTrainUnit,
    robodrummer::ParamIDs::pracTrainDir, robodrummer::ParamIDs::pracTrainGate, robodrummer::ParamIDs::pracTrainGateMin, robodrummer::ParamIDs::pracTrainRest, robodrummer::ParamIDs::pracTrainStopEnd };
constexpr unsigned kClickTag = 0xC11C0001u; // DrumEvent::sequence marker for practice clicks (kept out of MIDI capture)

int choiceIndex(const std::atomic<float>* raw, int count) noexcept {
    return raw == nullptr ? 0 : juce::jlimit(0, count - 1, juce::roundToInt(raw->load(std::memory_order_relaxed)));
}
}

RoboDrummerAudioProcessor::RoboDrummerAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Guitar / Analysis", juce::AudioChannelSet::stereo(), true)
          .withOutput("Drums + Monitor", juce::AudioChannelSet::stereo(), true)
          .withOutput("Kick", juce::AudioChannelSet::stereo(), false)
          .withOutput("Snare", juce::AudioChannelSet::stereo(), false)
          .withOutput("Toms", juce::AudioChannelSet::stereo(), false)
          .withOutput("Hats + Cymbals", juce::AudioChannelSet::stereo(), false)),
      parameters_(*this, nullptr, "RoboDrummerParams", createParameterLayout()) {
    namespace ids = robodrummer::ParamIDs;
    for (auto& v : pendingCc_) v.store(-1.0f);
    installStarterArrangement();
    for (const auto* id : kParameterIds)
        parameters_.addParameterListener(id, this);
    const auto cache = [this](const char* id) { return parameters_.getRawParameterValue(id); };
    rawTempoSource_ = cache(ids::tempoSource);
    rawFollowResponse_ = cache(ids::followResponse);
    rawHumanize_ = cache(ids::humanize);
    rawFeel_ = cache(ids::feel);
    rawComplexity_ = cache(ids::complexity);
    rawFillFrequency_ = cache(ids::fillFrequency);
    rawFillLength_ = cache(ids::fillLength);
    rawFillComplexity_ = cache(ids::fillComplexity);
    rawFillSurprise_ = cache(ids::fillSurprise);
    rawDrummer_ = cache(ids::drummer);
    rawCrash_ = cache(ids::crashTendency);
    rawInputMode_ = cache(ids::inputMode);
    rawInputGain_ = cache(ids::inputGain);
    rawAutoGain_ = cache(ids::autoGain);
    rawGate_ = cache(ids::gateThreshold);
    rawKit_ = cache(ids::kit);
    rawRoomSize_ = cache(ids::roomSize);
    rawFollowStrength_ = cache(ids::followStrength);
    rawTempoInertia_ = cache(ids::tempoInertia);
    rawBeatCorrection_ = cache(ids::beatCorrection);
    rawConfidenceSensitivity_ = cache(ids::confidenceSensitivity);
    rawKitTune_ = cache(ids::kitTune);
    rawMidiOutChannel_ = cache(ids::midiOutChannel);
    rawGuitarMonitor_ = cache(ids::guitarMonitor);
    rawAutoMeterEnabled_ = cache(ids::autoMeterEnabled);
    rawAutoMeterThreshold_ = cache(ids::autoMeterThreshold);
    rawLimbAmount_ = cache(ids::limbModelAmount);
    for (std::size_t i = 0; i < kPracticeIds.size(); ++i) rawPrac_[i] = cache(kPracticeIds[i]);
    rawRlAmount_ = cache(ids::rlAmount);
    rawRlSensitivity_ = cache(ids::rlSensitivity);
    rawRlAccent_ = cache(ids::rlAccentCrash);
    dspParams_ = { cache(ids::drumBusEnabled), cache(ids::drumBusThreshold), cache(ids::drumBusRatio), cache(ids::drumBusKnee),
                   cache(ids::drumBusAttack), cache(ids::drumBusRelease), cache(ids::drumBusMakeup), cache(ids::drumBusDetect),
                   cache(ids::drumBusScHpf), cache(ids::drumBusMix), cache(ids::drumBusDrive), cache(ids::transientShaperEnabled),
                   cache(ids::transientShaperAttack), cache(ids::transientShaperSustain), cache(ids::transientShaperMix),
                   cache(ids::transientShaperOutput) };
    // The standalone app is a jam tool, so the guitar is monitored by default; in a host it stays on its own track.
    if (wrapperType == wrapperType_Standalone)
        if (auto* monitor = parameters_.getParameter(ids::guitarMonitor)) monitor->setValueNotifyingHost(1.0f);
    resetMidiDrumMap();
    for (std::size_t i = 0; i < 8; ++i) { rawMixGain_[i] = cache(ids::mixGain[i]); rawMixMute_[i] = cache(ids::mixMute[i]); }
    for (std::size_t i = 0; i < 7; ++i) rawMixPan_[i] = cache(ids::mixPan[i]);
    for (std::size_t i = 0; i < 6; ++i) rawMixSolo_[i] = cache(ids::mixSolo[i]);
    resetMidiMappingsToDefault();
    syncAllParametersToAtomics();
    sweepStaleCaptureFiles();
    {
        std::error_code ignored;
        captureDirectory_ = std::filesystem::temp_directory_path(ignored) / "RoboDrummer" / "Capture" /
                            juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()).toStdString();
    }
    startTimerHz(30);
}

RoboDrummerAudioProcessor::~RoboDrummerAudioProcessor() {
    stopTimer();
    // Drag files must outlive the drop (the target may copy asynchronously), so only files older than 5 minutes go now.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!captureDirectory_.empty() && fs::exists(captureDirectory_, ec)) {
            const auto cutoff = fs::file_time_type::clock::now() - std::chrono::minutes(5);
            for (const auto& entry : fs::directory_iterator(captureDirectory_, ec)) {
                std::error_code fileError;
                if (entry.is_regular_file(fileError) && entry.last_write_time(fileError) < cutoff) fs::remove(entry.path(), fileError);
            }
            fs::remove(captureDirectory_, ec); // only succeeds when empty
        }
    }
    stopKitLoader();
    for (const auto* id : kParameterIds)
        parameters_.removeParameterListener(id, this);
    player_ = nullptr;
    playerHandoff_.resetWhenStopped();
}

juce::AudioProcessorValueTreeState::ParameterLayout RoboDrummerAudioProcessor::createParameterLayout() {
    namespace ids = robodrummer::ParamIDs;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto percent = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })
        .withValueFromStringFunction([](const juce::String& t) { return t.retainCharacters("0123456789.-").getFloatValue() / 100.0f; });
    const auto decibels = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return v <= -59.9f ? juce::String("-inf") : juce::String(v, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& t) { return t.containsIgnoreCase("inf") ? -60.0f : t.retainCharacters("0123456789.-").getFloatValue(); });
    const auto millis = juce::AudioParameterFloatAttributes().withLabel("ms");

    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::bpm, 1}, "Internal BPM",
        juce::NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f, juce::AudioParameterFloatAttributes().withLabel("BPM")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::intensity, 1}, "Intensity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::outputMode, 1}, "Output Mode",
        juce::StringArray{"Internal Drums", "MIDI Only", "Internal + MIDI"}, 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::leadershipMode, 1}, "Timing Mode",
        juce::StringArray{"Drummer Leads", "Hybrid", "Guitarist Leads"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::leadership, 1}, "Leadership",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::followRange, 1}, "Follow Range",
        juce::NormalisableRange<float>(0.0f, 80.0f, 1.0f), 15.0f, juce::AudioParameterFloatAttributes().withLabel("BPM")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::dynamicFollow, 1}, "Dynamic Follow",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.6f, percent));
    juce::StringArray styleNames;
    for (const auto* name : robodrummer::kJamStyleNames) styleNames.add(name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::jamStyle, 1}, "Jam Style", styleNames, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::arrangementEnabled, 1}, "Programmed Arrangement", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::jamMemoryEnabled, 1}, "Learn This Jam", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::manualMeterEnabled, 1}, "Manual Meter", false));
    juce::StringArray numerators;
    for (int n = kMinMeterNumerator; n <= kMaxMeterNumerator; ++n)
        numerators.add(juce::String(n));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::meterNumerator, 1}, "Meter Numerator",
        numerators, 4 - kMinMeterNumerator));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::meterDenominator, 1}, "Meter Denominator",
        juce::StringArray{"2", "4", "8", "16"}, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::silenceMode, 1}, "Guitar Silence",
        juce::StringArray{"Keep playing", "Reduce intensity", "Hold groove", "Fill during silence", "Stop after bars", "Wait for resume"}, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(ParameterID{ids::silenceStopBars, 1}, "Silence Stop Bars", 1, 16, 2,
        juce::AudioParameterIntAttributes().withLabel("bars")));

    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::tempoSource, 1}, "Tempo Source",
        juce::StringArray{"Host / Internal", "Internal BPM only", "MIDI Clock"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::followResponse, 1}, "Follow Response",
        juce::StringArray{"Very Stable", "Stable", "Balanced", "Responsive", "Very Responsive"}, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::humanize, 1}, "Humanize",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::feel, 1}, "Feel",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.5f), 0.0f, millis));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::complexity, 1}, "Complexity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::fillFrequency, 1}, "Fill Frequency",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::fillLength, 1}, "Fill Length",
        juce::StringArray{"1 beat", "2 beats", "1 bar", "2 bars"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::fillComplexity, 1}, "Fill Complexity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::fillSurprise, 1}, "Fill Surprise",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent));
    juce::StringArray personalities;
    for (const auto* name : robodrummer::kDrummerPersonalityNames) personalities.add(name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::drummer, 1}, "Drummer", personalities, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::crashTendency, 1}, "Crash Tendency",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::inputMode, 1}, "Guitar Input",
        juce::StringArray{"Auto (mono / stereo sum)", "Left only", "Right only"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::inputGain, 1}, "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 36.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::autoGain, 1}, "Auto Gain", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::gateThreshold, 1}, "Noise Gate",
        juce::NormalisableRange<float>(-90.0f, -20.0f, 0.5f), -90.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return v <= -80.0f ? juce::String("Off") : juce::String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.trim().equalsIgnoreCase("Off") ? -90.0f : text.retainCharacters("0123456789.-").getFloatValue(); })));
    juce::StringArray kits;
    for (const auto* name : robodrummer::kKitNames) kits.add(name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::kit, 1}, "Kit", kits, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::roomSize, 1}, "Room Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::followStrength, 1}, "Follow Strength",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::tempoInertia, 1}, "Tempo Inertia",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::beatCorrection, 1}, "Beat Correction Speed",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::confidenceSensitivity, 1}, "Confidence Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::kitTune, 1}, "Kit Tuning",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("st")));
    layout.add(std::make_unique<juce::AudioParameterInt>(ParameterID{ids::midiOutChannel, 1}, "MIDI Output Channel", 1, 16, 10));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::guitarMonitor, 1}, "Guitar Monitor",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent));
    for (std::size_t i = 0; i < robodrummer::kMixerChannelCount; ++i) {
        const juce::String name = robodrummer::kMixerChannelNames[i];
        const float defaultDb = i == static_cast<std::size_t>(robodrummer::MixerChannel::Room) ? -12.0f : 0.0f;
        layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::mixGain[i], 1}, name + " Level",
            juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f, 2.5f), defaultDb, decibels));
        if (i < 7)
            layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::mixPan[i], 1}, name + " Pan",
                juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::mixMute[i], 1}, name + " Mute", false));
        if (i < 6)
            layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::mixSolo[i], 1}, name + " Solo", false));
    }

    // ---- 0.4.0 additions (appended; all off / neutral by default) ----
    const auto labelled = [](const char* unit) { return juce::AudioParameterFloatAttributes().withLabel(unit); };
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::drumBusEnabled, 1}, "Drum Bus", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusThreshold, 1}, "Bus Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -18.0f, labelled("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusRatio, 1}, "Bus Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f, 0.5f), 4.0f, labelled(":1")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusKnee, 1}, "Bus Knee",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f, labelled("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusAttack, 1}, "Bus Attack",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f, 0.4f), 10.0f, labelled("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusRelease, 1}, "Bus Release",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.4f), 120.0f, labelled("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusMakeup, 1}, "Bus Makeup",
        juce::NormalisableRange<float>(-12.0f, 24.0f, 0.1f), 0.0f, labelled("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::drumBusDetect, 1}, "Bus Detector",
        juce::StringArray{"Peak", "RMS"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusScHpf, 1}, "Bus Sidechain HPF",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return v < 20.0f ? juce::String("Off") : juce::String(juce::roundToInt(v)) + " Hz"; })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusMix, 1}, "Bus Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::drumBusDrive, 1}, "Bus Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::transientShaperEnabled, 1}, "Transient Shaper", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::transientShaperAttack, 1}, "Shaper Attack",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, labelled("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::transientShaperSustain, 1}, "Shaper Sustain",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, labelled("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::transientShaperMix, 1}, "Shaper Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f, labelled("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::transientShaperOutput, 1}, "Shaper Output",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f, labelled("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::rlAmount, 1}, "Riff Lock",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f, labelled("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::rlSensitivity, 1}, "Riff Sensitivity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, labelled("%")));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::rlAccentCrash, 1}, "Riff Crash Accents", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::autoMeterEnabled, 1}, "Auto Meter", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::autoMeterThreshold, 1}, "Auto-meter Confidence",
        juce::NormalisableRange<float>(0.5f, 0.95f, 0.01f), 0.75f, percent));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::limbModelAmount, 1}, "Playability",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f, labelled("%")));
    // Practice mode (0.4.0): everything defaults to off.
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::practiceEnabled, 1}, "Practice Mode", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::pracCountInLen, 1}, "Count-in",
        juce::StringArray{"Off", "1 beat", "2 beats", "4 beats", "1 bar"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::pracCountInSound, 1}, "Count-in Sound",
        juce::StringArray{"Click", "Sticks"}, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::pracCountInStartOnly, 1}, "Count-in Only On Start", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::pracLoopOn, 1}, "Practice Loop", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::pracLoopMode, 1}, "Loop Mode",
        juce::StringArray{"Bars", "Section range"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::pracLoopBars, 1}, "Loop Length",
        juce::StringArray{"1 bar", "2 bars", "4 bars", "8 bars"}, 2));
    layout.add(std::make_unique<juce::AudioParameterInt>(ParameterID{ids::pracLoopSecFirst, 1}, "Loop First Section", 1, 16, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(ParameterID{ids::pracLoopSecLast, 1}, "Loop Last Section", 1, 16, 1));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::pracTrainOn, 1}, "Tempo Trainer", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::pracTrainStart, 1}, "Trainer Start BPM",
        juce::NormalisableRange<float>(20.0f, 400.0f, 1.0f), 80.0f, labelled("BPM")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::pracTrainTarget, 1}, "Trainer Target BPM",
        juce::NormalisableRange<float>(20.0f, 400.0f, 1.0f), 120.0f, labelled("BPM")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::pracTrainStep, 1}, "Trainer Step",
        juce::NormalisableRange<float>(1.0f, 40.0f, 1.0f), 5.0f, labelled("BPM")));
    layout.add(std::make_unique<juce::AudioParameterInt>(ParameterID{ids::pracTrainEvery, 1}, "Trainer Every", 1, 64, 4));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::pracTrainUnit, 1}, "Trainer Unit",
        juce::StringArray{"Bars", "Loop passes"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::pracTrainDir, 1}, "Trainer Direction",
        juce::StringArray{"Up", "Down", "Ping-pong"}, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::pracTrainGate, 1}, "Trainer Only When In Time", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::pracTrainGateMin, 1}, "Trainer Gate Confidence",
        juce::NormalisableRange<float>(0.2f, 0.95f, 0.01f), 0.6f, percent));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::pracTrainRest, 1}, "Trainer Rest Bar", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::pracTrainStopEnd, 1}, "Trainer Stop At Target", false));
    return layout;
}

void RoboDrummerAudioProcessor::parameterChanged(const juce::String& id, float value) {
    namespace ids = robodrummer::ParamIDs;
    const int index = juce::roundToInt(value);
    if (id == ids::bpm) setInternalBpm(value);
    else if (id == ids::intensity) setIntensity(value);
    else if (id == ids::outputMode) setOutputMode(static_cast<robodrummer::OutputMode>(juce::jlimit(0, 2, index)));
    else if (id == ids::leadershipMode) setLeadershipMode(static_cast<robodrummer::LeadershipMode>(juce::jlimit(0, 2, index)));
    else if (id == ids::leadership) setLeadership(value);
    else if (id == ids::followRange) setFollowRange(value);
    else if (id == ids::dynamicFollow) setDynamicFollow(value);
    else if (id == ids::jamStyle) setJamStyle(static_cast<robodrummer::JamStyle>(juce::jlimit(0, robodrummer::kJamStyleCount - 1, index)));
    else if (id == ids::arrangementEnabled) setArrangementEnabled(value >= 0.5f);
    else if (id == ids::jamMemoryEnabled) setJamMemoryEnabled(value >= 0.5f);
    else if (id == ids::manualMeterEnabled) setManualMeterEnabled(value >= 0.5f);
    else if (id == ids::meterNumerator)
        setManualMeter(juce::jlimit(kMinMeterNumerator, kMaxMeterNumerator, index + kMinMeterNumerator),
                       getManualMeterDenominator());
    else if (id == ids::meterDenominator)
        setManualMeter(getManualMeterNumerator(),
                       kMeterDenominators[static_cast<std::size_t>(juce::jlimit(0, 3, index))]);
    else if (id == ids::silenceMode) setSilenceMode(static_cast<robodrummer::SilenceMode>(juce::jlimit(0, 5, index)));
    else if (id == ids::silenceStopBars) setSilenceStopBars(index);
}

// Restored or fuzzed state may carry NaN/inf or out-of-range numbers; they are replaced by the parameter default
// or clamped before anything on the audio thread can read them.
void RoboDrummerAudioProcessor::sanitiseParameters() {
    for (auto* parameter : juce::AudioProcessor::getParameters()) {
        const float value = parameter->getValue();
        if (!std::isfinite(value)) parameter->setValueNotifyingHost(parameter->getDefaultValue());
        else if (value < 0.0f || value > 1.0f) parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
    }
}

void RoboDrummerAudioProcessor::syncAllParametersToAtomics() {
    for (const auto* id : kParameterIds)
        if (const auto* raw = parameters_.getRawParameterValue(id))
            parameterChanged(id, raw->load(std::memory_order_relaxed));
}

float RoboDrummerAudioProcessor::rawValue(const char* id, float fallback) const noexcept {
    const auto* raw = parameters_.getRawParameterValue(id);
    return raw != nullptr ? raw->load(std::memory_order_relaxed) : fallback;
}

void RoboDrummerAudioProcessor::timerCallback() {
    // Publishes values the audio thread changed (MIDI intensity notes, arrangement sections) to the host
    // so automation lanes, generic editors and saved state stay in agreement with what is playing.
    const auto publish = [this](const char* id, float plainValue) {
        if (auto* parameter = parameters_.getParameter(id)) {
            const float normalised = parameter->convertTo0to1(plainValue);
            if (std::abs(parameter->getValue() - normalised) > 1.0e-4f) {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(normalised);
                parameter->endChangeGesture();
            }
        }
    };
    if (intensityNeedsHostSync_.exchange(false, std::memory_order_acq_rel))
        publish(robodrummer::ParamIDs::intensity, intensity_.load(std::memory_order_relaxed));
    if (jamStyleNeedsHostSync_.exchange(false, std::memory_order_acq_rel))
        publish(robodrummer::ParamIDs::jamStyle, static_cast<float>(jamStyle_.load(std::memory_order_relaxed)));

    drainMidiLearnToParameters();
    drainParameterToggles();
    finalizeAutoSetupIfReady();
    capture_.drain();

    // Kit selection and loading happen here (message thread) and on the loader thread, never on the audio thread.
    const int kitChoice = choiceIndex(rawKit_, robodrummer::kKitChoiceCount);
    const bool reload = kitReloadRequested_.exchange(false, std::memory_order_acq_rel);
    if (sampleRate_ > 0.0 && (kitChoice != lastKitChoice_ || (reload && kitChoice == robodrummer::kUserKitIndex))) {
        lastKitChoice_ = kitChoice;
        loadKitAsync(kitChoice);
    }
    playerHandoff_.drainRetired();
}

void RoboDrummerAudioProcessor::drainMidiLearnToParameters() {
    for (std::size_t target = 0; target < pendingCc_.size(); ++target) {
        const float value = pendingCc_[target].exchange(-1.0f, std::memory_order_acq_rel);
        if (value < 0.0f) continue;
        if (auto* parameter = parameters_.getParameter(robodrummer::kLearnTargets[target].id)) {
            // Snap to the parameter's own steps so choices and switches land on valid values.
            const float snapped = parameter->convertTo0to1(parameter->convertFrom0to1(value));
            if (std::abs(parameter->getValue() - snapped) > 1.0e-5f) {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(snapped);
                parameter->endChangeGesture();
            }
        }
    }
}

void RoboDrummerAudioProcessor::setParameterFromState(const char* id, float plainValue) {
    if (auto* parameter = parameters_.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

void RoboDrummerAudioProcessor::stopKitLoader() {
    {
        const juce::ScopedLock submission(kitSubmissionLock_);
        kitSubmissionsEnabled_ = false;
        kitLoadCancelled_.store(true, std::memory_order_release);
    }
    // Lifecycle only: a decoder still running after the grace period must finish
    // before its result mailbox, status lock or processor can be destroyed.
    (void) robodrummer::finishKitLoaderJobs(kitLoader_, kitLoadCancelled_);
}

void RoboDrummerAudioProcessor::loadKitAsync(int kitIndex) {
    const juce::ScopedLock submission(kitSubmissionLock_);
    if (!kitSubmissionsEnabled_) return;
    const double sr = sampleRate_;
    const auto folder = getUserKitFolder();
    kitLoader_.addJob([this, kitIndex, sr, folder] {
        if (kitLoadCancelled_.load(std::memory_order_acquire)) return;
        juce::String report;
        std::unique_ptr<robodrummer::DrumSamplePlayer> built;
        if (kitIndex == robodrummer::kUserKitIndex) {
            built = robodrummer::KitBuilder::loadUserFolder(folder, sr, report);
        } else {
            built = robodrummer::KitBuilder::buildBuiltIn(kitIndex, sr);
            report = robodrummer::kKitNames[static_cast<std::size_t>(juce::jlimit(0, robodrummer::kKitChoiceCount - 1, kitIndex))];
        }
        if (kitLoadCancelled_.load(std::memory_order_acquire)) return;
        {
            const juce::ScopedLock lock(uiTextLock_);
            kitStatus_ = report;
        }
        playerHandoff_.publish(std::move(built));
    });
}

void RoboDrummerAudioProcessor::swapInPendingKit() noexcept {
    if (playerHandoff_.tryAdoptPending()) player_ = playerHandoff_.current();
}

void RoboDrummerAudioProcessor::applyMixerToSampler() noexcept {
    if (player_ == nullptr) return;
    robodrummer::MixerSettings mix;
    for (std::size_t i = 0; i < robodrummer::kMixerChannelCount; ++i) {
        mix.gainDb[i] = rawMixGain_[i] != nullptr ? rawMixGain_[i]->load(std::memory_order_relaxed) : 0.0f;
        mix.mute[i] = rawMixMute_[i] != nullptr && rawMixMute_[i]->load(std::memory_order_relaxed) >= 0.5f;
        if (i < 7) mix.pan[i] = rawMixPan_[i] != nullptr ? rawMixPan_[i]->load(std::memory_order_relaxed) : 0.0f;
        if (i < 6) mix.solo[i] = rawMixSolo_[i] != nullptr && rawMixSolo_[i]->load(std::memory_order_relaxed) >= 0.5f;
    }
    const float tune = rawKitTune_ != nullptr ? rawKitTune_->load(std::memory_order_relaxed) : 0.0f;
    for (int i = 0; i <= static_cast<int>(robodrummer::DrumInstrument::Clap); ++i) {
        const auto instrument = static_cast<robodrummer::DrumInstrument>(i);
        const auto resolved = robodrummer::resolveInstrumentMix(instrument, mix);
        player_->setInstrumentGainPan(instrument, resolved.gain, resolved.pan);
        player_->setInstrumentTuning(instrument, tune);
    }
}

robodrummer::dsp::DrumBusParams RoboDrummerAudioProcessor::readDrumBusParams() const noexcept {
    const auto get = [](const std::atomic<float>* raw, float fallback) { return raw != nullptr ? raw->load(std::memory_order_relaxed) : fallback; };
    robodrummer::dsp::DrumBusParams p;
    p.enabled = get(dspParams_.busEnabled, 0.0f) >= 0.5f;
    p.setThresholdDb(get(dspParams_.busThreshold, -18.0f)).setRatio(get(dspParams_.busRatio, 4.0f)).setKneeDb(get(dspParams_.busKnee, 6.0f))
        .setAttackMs(get(dspParams_.busAttack, 10.0f)).setReleaseMs(get(dspParams_.busRelease, 120.0f))
        .setMakeupDb(get(dspParams_.busMakeup, 0.0f)).setScHpfHz(get(dspParams_.busScHpf, 0.0f))
        .setMix(get(dspParams_.busMix, 1.0f)).setDrive(get(dspParams_.busDrive, 0.0f));
    p.detectRms = get(dspParams_.busDetect, 0.0f) >= 0.5f;
    return p;
}

robodrummer::dsp::TransientShaperParams RoboDrummerAudioProcessor::readShaperParams() const noexcept {
    const auto get = [](const std::atomic<float>* raw, float fallback) { return raw != nullptr ? raw->load(std::memory_order_relaxed) : fallback; };
    robodrummer::dsp::TransientShaperParams p;
    p.enabled = get(dspParams_.shEnabled, 0.0f) >= 0.5f;
    p.setAttackPct(get(dspParams_.shAttack, 0.0f)).setSustainPct(get(dspParams_.shSustain, 0.0f))
        .setMixPct(get(dspParams_.shMix, 100.0f)).setOutputDb(get(dspParams_.shOutput, 0.0f));
    return p;
}

void RoboDrummerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    stopKitLoader(); // gate submissions before changing the rate or sampler lifecycle
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    setLatencySamples(0); // all processing is causal and sample-aligned; nothing is delayed
    jam_.prepare(sampleRate_);
    conditioner_.prepare(sampleRate_);
    calibration_.cancel();
    calibrating_ = false;
    calibrationState_.store(0, std::memory_order_relaxed);
    midiClock_.prepare(sampleRate_);
    midiSampleCursor_ = 0;
    rhythmAnalyzer_.prepare(sampleRate_);
    performanceAnalyzer_.prepare(sampleRate_);
    timingAuthority_.reset(internalBpm_.load(std::memory_order_relaxed));
    resyncPlanner_.reset();
    jamBrain_.reset();
    jamCoordinator_.reset();
    silenceController_.reset();
    silenceIntensityMultiplierAudio_ = 1.0f;
    silenceHoldGrooveAudio_ = false;
    lastSilenceBarIndex_ = -1;
    lastCoordinatorBarIndex_ = -1;
    coordinatorNextCuePending_ = false;
    coordinatorSoloCuePending_ = false;
    coordinatorEndCuePending_ = false;
    coordinatorProgrammedBoundaryPending_ = false;
    coordinatorIntensityBiasAudio_ = 0.0f;
    coordinatorSuppressBusyFillsAudio_ = false;
    jamCoordinationState_.store(static_cast<int>(robodrummer::JamCoordinationState::EstablishingGroove), std::memory_order_relaxed);
    transitionProbability_.store(0.0f, std::memory_order_relaxed);
    endingProbability_.store(0.0f, std::memory_order_relaxed);
    coordinatorSuppressBusyFillsVisible_.store(false, std::memory_order_relaxed);
    silentBarsVisible_.store(0, std::memory_order_relaxed);
    waitingForResume_.store(false, std::memory_order_relaxed);
    jamBrainCooldownSeconds_ = 0.0;
    arrangement_.reset();
    lastArrangementEnabled_ = false;
    lastArrangementBarIndex_ = 0;
    sectionPersonality_ = robodrummer::DrummerPersonality::Balanced;
    sectionFillFrequency_ = sectionLeadership_ = sectionFollowStrength_ = -1.0f;
    publishArrangementSection();
    jam_.setTempo(internalBpm_.load(std::memory_order_relaxed));
    jam_.setIntensity(intensity_.load(std::memory_order_relaxed));
    adaptiveJoined_ = false;
    adaptiveJoinedVisible_.store(false, std::memory_order_relaxed);
    lastAudioMode_ = leadershipMode_.load(std::memory_order_relaxed);
    midiOut_.ensureSize(8192);
    drummerStopped_.store(jam_.state().stopped, std::memory_order_relaxed);
    timeScale_.store(jam_.state().timeScale, std::memory_order_relaxed);

    drumL_.assign(MaxScratchSamples, 0.0f);
    drumR_.assign(MaxScratchSamples, 0.0f);
    roomL_.assign(MaxScratchSamples, 0.0f);
    roomR_.assign(MaxScratchSamples, 0.0f);
    reverb_.setSampleRate(sampleRate_);
    reverb_.reset();
    shaper_.setParams(readShaperParams());
    shaper_.prepare(sampleRate_, samplesPerBlock);
    drumBus_.setParams(readDrumBusParams());
    drumBus_.prepare(sampleRate_, samplesPerBlock);
    setLatencySamples(shaper_.latencySamples() + drumBus_.latencySamples());
    resetAutoMeter();
    autoSetup_.prepare(sampleRate_);
    practice_.reset();
    practiceSessionWanted_ = false;
    resetPracticeRuntime();
    riffLock_.reset();
    lowBand_.prepare(sampleRate_);
    riffBar_ = -1;
    publishRiffSnapshot(riffLock_.snapshot());
    lastRoomSize_ = -1.0f;
    lastMonitorGain_ = rawGuitarMonitor_ != nullptr ? juce::jlimit(0.0f, 1.0f, rawGuitarMonitor_->load()) : 0.0f;
    masterGain_.reset(sampleRate_, 0.02);
    masterGain_.setCurrentAndTargetValue(1.0f);

    // Rebuild the sampler for the new sample rate. Built-in kits are generated synchronously; a user folder
    // starts on the built-in kit and swaps in as soon as the loader thread has read the files.
    lastKitChoice_ = choiceIndex(rawKit_, robodrummer::kKitChoiceCount);
    playerHandoff_.resetWhenStopped(robodrummer::KitBuilder::buildBuiltIn(lastKitChoice_ == robodrummer::kUserKitIndex ? 0 : lastKitChoice_, sampleRate_));
    player_ = playerHandoff_.current();
    {
        const juce::ScopedLock submission(kitSubmissionLock_);
        kitLoadCancelled_.store(false, std::memory_order_release);
        kitSubmissionsEnabled_ = true;
    }
    midiSafetyStopped_ = midiPanicOutputPending_ = midiSafetyHostStopSeen_ = false;
    if (lastKitChoice_ == robodrummer::kUserKitIndex) loadKitAsync(lastKitChoice_);
}

void RoboDrummerAudioProcessor::releaseResources() { if (player_ != nullptr) player_->clear(); }

bool RoboDrummerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    if (!(in == juce::AudioChannelSet::disabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()))
        return false;
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus) {
        const auto& aux = layouts.outputBuses.getReference(bus);
        if (!(aux == juce::AudioChannelSet::disabled() || aux == juce::AudioChannelSet::stereo())) return false;
    }
    return true;
}

void RoboDrummerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    namespace rd = robodrummer;

    const int numSamples = buffer.getNumSamples();
    blockOnsetCount_ = 0;
    // Output channels that have no matching input (mono or disabled guitar input) contain garbage;
    // this also clears any enabled aux output buses.
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);

    // The monitored guitar passes through untouched, except that NaN/Inf samples are replaced by silence so a
    // misbehaving upstream device can never poison the drum mix or the analysis.
    for (int channel = 0; channel < getTotalNumInputChannels(); ++channel) {
        float* samples = buffer.getWritePointer(channel);
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(samples[i])) samples[i] = 0.0f;
    }

    swapInPendingKit();

    // ---- MIDI: clock, MIDI learn, controller mapping and intervention notes -------------------------------------
    // Channel 16 is RoboDrummer's intervention channel; those messages are consumed here and not forwarded, so
    // they cannot trigger a downstream drum instrument. Controllers bound to a parameter are consumed too.
    // Everything else passes through.
    midiOut_.clear();
    rd::MidiBlockPolicy midiPolicy;
    bool midiFailSafe = false;
    const auto emitMidiPanic = [&] {
        midiOut_.clear();
        midiPolicy.resetOutput();
        midiPanicOutputPending_ = numSamples <= 0;
        if (numSamples > 0) {
            const std::uint8_t stop[] = { 0xfc };
            if (midiPolicy.admitOutput(sizeof(stop), rd::MidiOutputClass::Critical)) midiOut_.addEvent(stop, sizeof(stop), 0);
            for (int channel = 1; channel <= 16; ++channel) {
                for (const int controller : { 120, 123 }) {
                    const std::uint8_t bytes[] = { static_cast<std::uint8_t>(0xb0 | (channel - 1)), static_cast<std::uint8_t>(controller), 0 };
                    if (midiPolicy.admitOutput(sizeof(bytes), rd::MidiOutputClass::Critical))
                        midiOut_.addEvent(bytes, sizeof(bytes), 0);
                }
            }
        }
    };
    if (midiPanicOutputPending_ && numSamples > 0) emitMidiPanic();
    const auto forwardInput = [&](const auto& metadata, const rd::MidiMessageView& message) {
        const auto kind = message.requiresCriticalOutput() ? rd::MidiOutputClass::Critical : rd::MidiOutputClass::Ordinary;
        if (midiPolicy.admitOutput(static_cast<std::size_t>(metadata.numBytes), kind))
            midiOut_.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
        else if (midiPolicy.needsPanic() || message.isMidiClock() || message.isMidiStart() || message.isMidiContinue())
            midiFailSafe = true; // never silently desynchronise a downstream clock/transport
    };
    std::array<rd::MidiCommand, 24> midiCommands{};
    std::size_t midiCommandCount = 0;
    for (const auto metadata : midi) {
        if (midiFailSafe || !midiPolicy.scanNext()) { midiFailSafe = true; break; }
        const rd::MidiMessageView msg(metadata.data, metadata.numBytes);
        const long long absoluteSample = midiSampleCursor_ + metadata.samplePosition;
        if (msg.isMidiClock()) {
            midiClock_.tick(absoluteSample);
            forwardInput(metadata, msg);
            continue;
        }
        if (msg.isMidiStart() || msg.isMidiContinue() || msg.isMidiStop()) {
            if (msg.isMidiStart() || msg.isMidiContinue()) {
                if (midiSafetyStopped_) jam_.apply(rd::MidiCommand::Resume);
                midiSafetyStopped_ = false;
                midiSafetyHostStopSeen_ = false;
            }
            if (msg.isMidiStart()) midiClock_.start();
            else if (msg.isMidiContinue()) midiClock_.continueClock();
            else midiClock_.stop();
            forwardInput(metadata, msg);
            continue;
        }
        const int channel = msg.getChannel();
        if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            // Drums are one-shots, but "all sound off / all notes off" must still silence anything ringing.
            if (player_ != nullptr) player_->clear();
            if (channel != rd::MidiLearnMap::DefaultChannel) forwardInput(metadata, msg);
            continue;
        }
        if (msg.isController()) {
            const int cc = msg.getControllerNumber();
            const int target = midiLearn_.handleCc(cc);
            lastMidiKind_.store(1, std::memory_order_relaxed);
            lastMidiData1_.store(cc, std::memory_order_relaxed);
            lastMidiData2_.store(msg.getControllerValue(), std::memory_order_relaxed);
            lastMidiChannel_.store(channel, std::memory_order_relaxed);
            if (target >= 0) {
                pendingCc_[static_cast<std::size_t>(target)].store(static_cast<float>(msg.getControllerValue()) / 127.0f, std::memory_order_release);
                continue;
            }
        } else if (msg.isNoteOn()) {
            const int note = msg.getNoteNumber();
            const bool learning = midiLearn_.armedCommand() != rd::MidiLearnMap::NoCommand;
            const int command = midiLearn_.handleNote(note, channel);
            lastMidiKind_.store(2, std::memory_order_relaxed);
            lastMidiData1_.store(note, std::memory_order_relaxed);
            lastMidiData2_.store(msg.getVelocity(), std::memory_order_relaxed);
            lastMidiChannel_.store(channel, std::memory_order_relaxed);
            if (command >= 0) {
                if (midiCommandCount == midiCommands.size()) { midiFailSafe = true; break; }
                midiCommands[midiCommandCount++] = static_cast<rd::MidiCommand>(command);
            }
            if (learning || command >= 0) continue;
        } else if (msg.isNoteOff()) {
            if (midiLearn_.commandForNote(msg.getNoteNumber(), channel) >= 0) continue;
        }
        if (channel == rd::MidiLearnMap::DefaultChannel) continue;
        forwardInput(metadata, msg);
    }
    if (midiFailSafe) {
        midiSafetyStopped_ = true;
        midiSafetyHostStopSeen_ = false;
        jam_.apply(rd::MidiCommand::Stop);
        midiClock_.stop();
        if (player_ != nullptr) player_->clear();
        midiCommandCount = 0;
        emitMidiPanic();
    }
    const bool clockValid = midiClock_.valid(midiSampleCursor_ + numSamples);
    midiClockActive_.store(clockValid, std::memory_order_relaxed);
    midiClockBpm_.store(clockValid ? midiClock_.bpm() : 0.0, std::memory_order_relaxed);
    midiSampleCursor_ += numSamples;

    // ---- guitar input conditioning and analysis -----------------------------------------------------------------
    rd::RhythmState rhythm{};
    rd::PerformanceState performance{};
    const int calibrationCommand = calibrationCommand_.exchange(0, std::memory_order_acq_rel);
    if (calibrationCommand == 1) {
        calibration_.start(sampleRate_, 8.0);
        calibrating_ = true;
        calibrationState_.store(1, std::memory_order_relaxed);
        calibrationProgress_.store(0.0f, std::memory_order_relaxed);
    } else if (calibrationCommand == 2) {
        calibration_.cancel();
        calibrating_ = false;
        calibrationState_.store(0, std::memory_order_relaxed);
    }

    if (getTotalNumInputChannels() > 0 && numSamples > 0) {
        static constexpr std::array<rd::InputMode, 3> inputModes { rd::InputMode::AutoSum, rd::InputMode::LeftOnly, rd::InputMode::RightOnly };
        conditioner_.setMode(inputModes[static_cast<std::size_t>(choiceIndex(rawInputMode_, 3))]);
        conditioner_.setGainDb(rawInputGain_ != nullptr ? rawInputGain_->load(std::memory_order_relaxed) : 0.0f);
        conditioner_.setAutoGain(rawAutoGain_ != nullptr && rawAutoGain_->load(std::memory_order_relaxed) >= 0.5f);
        conditioner_.setGateThresholdDb(rawGate_ != nullptr ? rawGate_->load(std::memory_order_relaxed) : -90.0f);
        conditioner_.setGateEnabled(!calibrating_); // calibration must see the real noise floor

        const float* left = buffer.getReadPointer(0);
        const float* right = getTotalNumInputChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
        float peak = 0.0f, rmsAccumulator = 0.0f;
        int chunks = 0;
        const bool autoSetupListening = autoSetup_.state() == robodrummer::AutoSetupState::Listening;
        const int inputModeIndex = choiceIndex(rawInputMode_, 3);
        for (int position = 0; position < numSamples; position += AnalysisChunk) {
            const int n = std::min(AnalysisChunk, numSamples - position);
            if (autoSetupListening) {
                // Auto Setup measures the RAW guitar (before gain, gate and conditioning) so proposed gain is absolute.
                for (int i = 0; i < n; ++i) {
                    const float l = left[position + i];
                    const float r = right != nullptr ? right[position + i] : l;
                    autoSetupScratch_[static_cast<std::size_t>(i)] = inputModeIndex == 1 ? l : (inputModeIndex == 2 ? r : 0.5f * (l + r));
                }
                autoSetup_.process(autoSetupScratch_.data(), n);
            }
            conditioner_.process(left + position, right != nullptr ? right + position : nullptr, analysisScratch_.data(), n);
            rhythmAnalyzer_.processBlock(analysisScratch_.data(), n);
            const float lowRatio = lowBand_.processBlock(analysisScratch_.data(), n);
            for (std::size_t i = 0; i < rhythmAnalyzer_.lastBlockOnsetCount(); ++i) {
                const auto& onset = rhythmAnalyzer_.lastBlockOnset(i);
                if (blockOnsetCount_ < blockOnsets_.size())
                    blockOnsets_[blockOnsetCount_++] = { position + static_cast<int>(onset.sampleOffset), onset.strength, lowRatio };
                performanceAnalyzer_.noteOnset(static_cast<double>(rhythmAnalyzer_.lastBlockStartSample() + onset.sampleOffset) / sampleRate_, onset.strength);
            }
            performanceAnalyzer_.processBlock(analysisScratch_.data(), n);
            peak = std::max(peak, conditioner_.lastBlockPeak());
            rmsAccumulator += conditioner_.lastBlockRms();
            ++chunks;

            for (std::size_t i = 0; i < rhythmAnalyzer_.lastBlockOnsetCount(); ++i) {
                const auto& onset = rhythmAnalyzer_.lastBlockOnset(i);
                const auto writer = onsetFifo_.write(1);
                if (writer.blockSize1 > 0)
                    onsetStorage_[static_cast<std::size_t>(writer.startIndex1)] = { rhythmAnalyzer_.lastBlockStartSample() + onset.sampleOffset, onset.strength };
                else if (writer.blockSize2 > 0)
                    onsetStorage_[static_cast<std::size_t>(writer.startIndex2)] = { rhythmAnalyzer_.lastBlockStartSample() + onset.sampleOffset, onset.strength };
            }
            if (calibrating_ && calibration_.process(analysisScratch_.data(), n)) {
                const auto& r = calibration_.result();
                calibrationValues_[0].store(r.noiseFloorDb, std::memory_order_relaxed);
                calibrationValues_[1].store(r.typicalRmsDb, std::memory_order_relaxed);
                calibrationValues_[2].store(r.loudRmsDb, std::memory_order_relaxed);
                calibrationValues_[3].store(r.peakDb, std::memory_order_relaxed);
                calibrationValues_[4].store(r.dynamicRangeDb, std::memory_order_relaxed);
                calibrationValues_[5].store(r.onsetsPerSecond, std::memory_order_relaxed);
                calibrationValues_[6].store(r.meanOnsetStrength, std::memory_order_relaxed);
                calibrationValues_[7].store(r.suggestedGateDb, std::memory_order_relaxed);
                calibrationValues_[8].store(r.suggestedGainDb, std::memory_order_relaxed);
                calibrationState_.store(r.valid ? 2 : 0, std::memory_order_release);
                calibrating_ = false;
            }
        }
        if (calibrating_) calibrationProgress_.store(calibration_.progress(), std::memory_order_relaxed);
        rhythm = rhythmAnalyzer_.state();
        performance = performanceAnalyzer_.state();
        detectedGuitarBpm_.store(rhythm.tempoBpm, std::memory_order_relaxed);
        guitarTempoConfidence_.store(rhythm.tempoConfidence, std::memory_order_relaxed);
        guitarBeatConfidence_.store(rhythm.beatConfidence, std::memory_order_relaxed);
        guitarBeatPhase_.store(rhythm.beatPhase, std::memory_order_relaxed);
        predictedNextGuitarBeatSeconds_.store(rhythm.predictedNextBeatSeconds, std::memory_order_relaxed);
        guitarBeatInBar_.store(rhythm.beatInBar, std::memory_order_relaxed);
        guitarDownbeatConfidence_.store(rhythm.downbeatConfidence, std::memory_order_relaxed);
        guitarTrackerLocked_.store(rhythm.locked, std::memory_order_release);
        guitarIntensity_.store(performance.intensity, std::memory_order_relaxed);
        const float perfFields[13] { performance.rms, performance.peak, performance.activity, performance.intensity, performance.bandLow,
                                     performance.bandMid, performance.bandHigh, performance.spectralFlux, performance.transientDensity,
                                     performance.transientStrength, performance.regularity, performance.accentProbability, performance.silenceProbability };
        for (std::size_t i = 0; i < perfValues_.size(); ++i) perfValues_[i].store(perfFields[i], std::memory_order_relaxed);
        tempoMovement_.store(static_cast<int>(rhythm.movement), std::memory_order_relaxed);
        tempoVelocity_.store(rhythm.tempoVelocity, std::memory_order_relaxed);
        lookaheadMs_.store(robodrummer::recommendedLookaheadMs(numSamples, sampleRate_, std::min(rhythm.beatConfidence, 1.0f)), std::memory_order_relaxed);
        inputPeak_.store(peak, std::memory_order_relaxed);
        inputRms_.store(chunks > 0 ? rmsAccumulator / static_cast<float>(chunks) : 0.0f, std::memory_order_relaxed);
        gateOpen_.store(conditioner_.gateOpen(), std::memory_order_relaxed);
        analysisCursor_.store(static_cast<long long>(rhythmAnalyzer_.lastBlockStartSample()) + numSamples, std::memory_order_relaxed);

        candidateCountdown_ -= numSamples;
        if (candidateCountdown_ <= 0) {
            const auto candidates = rhythmAnalyzer_.tempoTracker().topHypotheses();
            const auto rich = rhythmAnalyzer_.richHypotheses();
            for (std::size_t i = 0; i < 3; ++i) {
                candidateBpm_[i].store(candidates[i].bpm, std::memory_order_relaxed);
                candidateConfidence_[i].store(candidates[i].confidence, std::memory_order_relaxed);
                richValues_[i * 6 + 0].store(static_cast<float>(rich[i].bpm), std::memory_order_relaxed);
                richValues_[i * 6 + 1].store(rich[i].confidence, std::memory_order_relaxed);
                richValues_[i * 6 + 2].store(static_cast<float>(rich[i].supportingEvents), std::memory_order_relaxed);
                richValues_[i * 6 + 3].store(rich[i].consistency, std::memory_order_relaxed);
                richValues_[i * 6 + 4].store(static_cast<float>(rich[i].ageSeconds), std::memory_order_relaxed);
                richValues_[i * 6 + 5].store(static_cast<float>(rich[i].relationToBest), std::memory_order_relaxed);
            }
            candidateCountdown_ = static_cast<int>(sampleRate_ * 0.1);
        }
    }

    // Guitar monitor: the analysis above already read the untouched input; what reaches the output is level-controlled
    // (and a mono guitar is placed on both channels). In a host the guitar normally has its own track, so it defaults to off.
    if (getTotalNumInputChannels() > 0 && numSamples > 0) {
        const float monitorGain = rawGuitarMonitor_ != nullptr ? juce::jlimit(0.0f, 1.0f, rawGuitarMonitor_->load(std::memory_order_relaxed)) : 0.0f;
        if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
            buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
        for (int channel = 0; channel < std::min(2, buffer.getNumChannels()); ++channel)
            buffer.applyGainRamp(channel, 0, numSamples, lastMonitorGain_, monitorGain);
        lastMonitorGain_ = monitorGain;
    }

    // ---- tempo source, transport and meter ---------------------------------------------------------------------
    const int tempoSource = choiceIndex(rawTempoSource_, 3); // 0 host/internal, 1 internal only, 2 MIDI clock
    double baseBpm = internalBpm_.load(std::memory_order_relaxed);
    int numerator = 4;
    int denominator = 4;
    bool hostMeterAvailable = false;
    int hostNumerator = 4;
    int hostDenominator = 4;
    bool playing = true;
    bool hostPositionAvailable = false;
    bool hasPpq = false;
    double hostPpq = 0.0;

    lastHostTempoValid_.store(false, std::memory_order_relaxed);
    lastHostPpqValid_.store(false, std::memory_order_relaxed);

    if (auto* hostPlayHead = getPlayHead()) {
        if (auto pos = hostPlayHead->getPosition()) {
            hostPositionAvailable = true;
            const auto hostBpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            const auto sig = pos->getTimeSignature();
            if (hostBpm.hasValue() && tempoSource != 1) baseBpm = *hostBpm;
            if (sig.hasValue()) {
                hostMeterAvailable = true;
                hostNumerator = sig->numerator;
                hostDenominator = sig->denominator;
                numerator = hostNumerator;
                denominator = hostDenominator;
            }
            playing = pos->getIsPlaying();
            hasPpq = ppq.hasValue();
            hostPpq = hasPpq ? *ppq : 0.0;

            lastHostBpm_.store(hostBpm.hasValue() ? *hostBpm : baseBpm, std::memory_order_relaxed);
            lastHostPpq_.store(hostPpq, std::memory_order_relaxed);
            lastHostNumerator_.store(numerator, std::memory_order_relaxed);
            lastHostDenominator_.store(denominator, std::memory_order_relaxed);
            lastHostPlaying_.store(playing, std::memory_order_relaxed);
            lastHostTempoValid_.store(hostBpm.hasValue(), std::memory_order_release);
            lastHostPpqValid_.store(hasPpq, std::memory_order_release);
        }
    }

    bool transportStopped = false;
    if (tempoSource == 1) {
        // Internal BPM only: free-running, ignores the host transport.
        hostPositionAvailable = false;
        hasPpq = false;
    } else if (tempoSource == 2 && clockValid) {
        baseBpm = midiClock_.bpm();
        hostPositionAvailable = false;
        hasPpq = false;
        transportStopped = midiClock_.transportSeen() && !midiClock_.running();
        if (midiClock_.consumeStart()) jam_.resetPhase();
    } else {
        transportStopped = hostPositionAvailable && !playing;
    }

    robodrummer::MeterSelectionInput meterInput;
    meterInput.hostAvailable = hostMeterAvailable;
    meterInput.hostNumerator = hostNumerator;
    meterInput.hostDenominator = hostDenominator;
    meterInput.manualEnabled = manualMeterEnabled_.load(std::memory_order_relaxed);
    meterInput.manualNumerator = manualMeterNumerator_.load(std::memory_order_relaxed);
    meterInput.manualDenominator = manualMeterDenominator_.load(std::memory_order_relaxed);
    const auto selectedMeter = robodrummer::MeterSelection::resolve(meterInput);
    numerator = selectedMeter.numerator;
    denominator = selectedMeter.denominator;

    // Automatic meter: only ever replaces the Default result (manual and host meters always win).
    {
        const bool autoEnabled = rawAutoMeterEnabled_ != nullptr && rawAutoMeterEnabled_->load(std::memory_order_relaxed) >= 0.5f;
        if (autoEnabled != autoMeterWasEnabled_ || meterInput.manualEnabled != autoMeterLastManual_ || transportStopped != autoMeterLastStopped_) {
            resetAutoMeter();
            autoMeterWasEnabled_ = autoEnabled;
            autoMeterLastManual_ = meterInput.manualEnabled;
            autoMeterLastStopped_ = transportStopped;
        }
        autoMeterEnabledVisible_.store(autoEnabled, std::memory_order_relaxed);
        if (autoEnabled && getTotalNumInputChannels() > 0 && numSamples > 0) {
            // Pulse positions follow the guitar tracker's own beat grid (monotone: a phase wrap adds one pulse).
            if (meterPhaseValid_ && rhythm.beatPhase < meterLastPhase_ - 0.5) ++meterPulseCounter_;
            meterLastPhase_ = rhythm.beatPhase;
            meterPhaseValid_ = true;
            const double positionAtEnd = static_cast<double>(meterPulseCounter_) + rhythm.beatPhase;
            if (rhythm.locked && std::isfinite(positionAtEnd) && rhythm.tempoBpm > 20.0) {
                const double pulsesPerSample = rhythm.tempoBpm / 60.0 / sampleRate_;
                for (std::size_t i = 0; i < blockOnsetCount_; ++i)
                    meterDetector_.observeOnset(positionAtEnd - static_cast<double>(numSamples - blockOnsets_[i].offset) * pulsesPerSample, blockOnsets_[i].strength);
                meterDetector_.advanceTo(positionAtEnd - 0.3); // onset latency: let a pulse close only after its onsets had time to arrive
            }
            const float thresholdRaw = rawAutoMeterThreshold_ != nullptr ? rawAutoMeterThreshold_->load(std::memory_order_relaxed) : 0.75f;
            meterGate_.setThreshold(thresholdRaw);
            const auto suggestion = meterDetector_.suggestion();
            const auto decision = meterGate_.update(meterInput.manualEnabled, hostMeterAvailable, suggestion);
            autoMeterNum_.store(suggestion.numerator, std::memory_order_relaxed);
            autoMeterDen_.store(suggestion.denominator, std::memory_order_relaxed);
            autoMeterPulses_.store(suggestion.pulsesPerBar, std::memory_order_relaxed);
            autoMeterTernary_.store(suggestion.ternary, std::memory_order_relaxed);
            autoMeterConfidence_.store(suggestion.confidence, std::memory_order_relaxed);
            const bool applying = decision.apply && selectedMeter.source == robodrummer::MeterSource::Default;
            autoMeterApplied_.store(applying, std::memory_order_relaxed);
            if (applying) {
                numerator = decision.numerator;
                denominator = decision.denominator;
            }
        } else {
            autoMeterApplied_.store(false, std::memory_order_relaxed);
        }
    }
    effectiveMeterNumerator_.store(numerator, std::memory_order_relaxed);
    effectiveMeterDenominator_.store(denominator, std::memory_order_relaxed);

    rhythmAnalyzer_.setMeterNumerator(numerator);

    const bool arrangementEnabled = arrangementEnabled_.load(std::memory_order_relaxed);

    robodrummer::TimingAuthoritySettings settings;
    settings.mode = getLeadershipMode();
    settings.leadership = sectionLeadership_ >= 0.0f && arrangementEnabled ? sectionLeadership_ : leadership_.load(std::memory_order_relaxed);
    settings.followRangeBpm = followRangeBpm_.load(std::memory_order_relaxed);
    if (sectionFollowStrength_ >= 0.0f && arrangementEnabled)
        settings.followRangeBpm *= static_cast<double>(juce::jlimit(0.0f, 1.0f, sectionFollowStrength_));
    settings.response = static_cast<robodrummer::FollowResponse>(choiceIndex(rawFollowResponse_, 5));
    settings.followStrength = rawFollowStrength_ != nullptr ? rawFollowStrength_->load(std::memory_order_relaxed) : 1.0f;
    settings.tempoInertia = rawTempoInertia_ != nullptr ? rawTempoInertia_->load(std::memory_order_relaxed) : 0.5f;
    settings.confidenceSensitivity = rawConfidenceSensitivity_ != nullptr ? rawConfidenceSensitivity_->load(std::memory_order_relaxed) : 0.5f;
    const float beatCorrectionSpeed = rawBeatCorrection_ != nullptr ? rawBeatCorrection_->load(std::memory_order_relaxed) : 0.5f;

    // ---- practice mode: session edges and settings. Nothing below runs while the session is off. ----
    const bool practiceWanted = pracValue(PracIdx::Enabled, 0.0f) >= 0.5f;
    if (practiceWanted != practiceSessionWanted_) {
        practiceSessionWanted_ = practiceWanted;
        resetPracticeRuntime();
        if (practiceWanted) practice_.start(); else practice_.stop();
    }
    bool practiceDrivesTempo = false;
    if (practiceSessionWanted_) {
        const bool externalTempo = (tempoSource == 2 && clockValid) || (tempoSource != 1 && lastHostTempoValid_.load(std::memory_order_relaxed));
        updatePracticeSettings(settings, externalTempo);
        practiceDrivesTempo = pracValue(PracIdx::TrainOn, 0.0f) >= 0.5f && practice_.trainerDrivesBase();
        if (practiceDrivesTempo) baseBpm = practice_.trainerBpm();
    }

    const int modeValue = static_cast<int>(settings.mode);
    if (modeValue != lastAudioMode_) {
        adaptiveJoined_ = settings.mode == robodrummer::LeadershipMode::DrummerLeads;
        adaptiveJoinedVisible_.store(adaptiveJoined_, std::memory_order_relaxed);
        lastAudioMode_ = modeValue;
        resyncPlanner_.reset();
        jamBrain_.reset();
        jamBrainCooldownSeconds_ = 0.0;
        phraseState_.store(static_cast<int>(robodrummer::PhraseState::Stable), std::memory_order_relaxed);
        phraseFillStrength_.store(0.0f, std::memory_order_relaxed);
        phraseBoundary_.store(false, std::memory_order_relaxed);
        if (settings.mode != robodrummer::LeadershipMode::DrummerLeads)
            timingAuthority_.reset(baseBpm);
    }

    const double blockSeconds = numSamples > 0 ? static_cast<double>(numSamples) / sampleRate_ : 0.0;
    const auto authority = timingAuthority_.update(baseBpm, rhythm, settings, blockSeconds);
    effectiveDrummerBpm_.store(authority.outputBpm, std::memory_order_relaxed);
    effectiveGuitarAuthority_.store(authority.effectiveGuitarAuthority, std::memory_order_relaxed);

    const auto arrangementRevision = arrangementRevision_.load(std::memory_order_acquire);
    if (arrangementRevision != appliedArrangementRevision_) {
        // Editing a slot must not restart playback: keep the current position (clamped to the new list).
        const auto playingIndex = arrangement_.currentSectionIndex();
        const auto barsIntoSection = arrangement_.barsIntoSection();
        rebuildArrangementFromSlots();
        arrangement_.restorePosition(playingIndex, barsIntoSection);
        publishArrangementSection();
        appliedArrangementRevision_ = arrangementRevision;
        if (arrangementEnabled)
            applyCurrentArrangementSection();
    }

    if (arrangementEnabled != lastArrangementEnabled_) {
        arrangement_.reset();
        lastArrangementBarIndex_ = jam_.currentBarIndex();
        publishArrangementSection();
        lastArrangementEnabled_ = arrangementEnabled;
        if (arrangementEnabled) {
            applyCurrentArrangementSection();
        } else {
            sectionPersonality_ = robodrummer::DrummerPersonality::Balanced;
            sectionFillFrequency_ = sectionLeadership_ = sectionFollowStrength_ = -1.0f;
        }
    }

    if (arrangementEnabled) {
        const auto currentBar = jam_.currentBarIndex();
        if (currentBar > lastArrangementBarIndex_) {
            const auto advancedBars = static_cast<std::uint64_t>(currentBar) - static_cast<std::uint64_t>(lastArrangementBarIndex_);
            const auto step = arrangement_.advanceBars(advancedBars);
            lastArrangementBarIndex_ = currentBar;
            if (advancedBars > 1) {
                if (step.sectionChanged) { publishArrangementSection(); applyCurrentArrangementSection(); }
            } else if (step.sectionChanged) {
                enterArrangementSection(true);
            } else if (!arrangement_.empty() && arrangement_.current().autoAdvance &&
                       arrangement_.barsIntoSection() == arrangement_.current().bars - 1 &&
                       arrangement_.currentSectionIndex() + 1 < arrangement_.size()) {
                switch (arrangement_.section(arrangement_.currentSectionIndex() + 1).transition) {
                    case robodrummer::SectionTransition::Fill:
                        if (!jam_.state().fillRequested) jam_.requestFill(0.8f);
                        break;
                    case robodrummer::SectionTransition::Break: jam_.apply(robodrummer::MidiCommand::Break); break;
                    default: break;
                }
            }
        } else if (currentBar < lastArrangementBarIndex_) {
            lastArrangementBarIndex_ = currentBar;
        }
    }

    // ---- drummer character -------------------------------------------------------------------------------------
    namespace ids = robodrummer::ParamIDs;
    robodrummer::DrummerControls character;
    character.complexity = rawComplexity_ != nullptr ? rawComplexity_->load(std::memory_order_relaxed) : 0.5f;
    character.humanize = rawHumanize_ != nullptr ? rawHumanize_->load(std::memory_order_relaxed) : 0.5f;
    character.feelMs = rawFeel_ != nullptr ? rawFeel_->load(std::memory_order_relaxed) : 0.0f;
    character.fillFrequency = arrangementEnabled && sectionFillFrequency_ >= 0.0f
        ? sectionFillFrequency_
        : (rawFillFrequency_ != nullptr ? rawFillFrequency_->load(std::memory_order_relaxed) : 0.5f);
    character.surprise = rawFillSurprise_ != nullptr ? rawFillSurprise_->load(std::memory_order_relaxed) : 0.0f;
    character.crashTendency = rawCrash_ != nullptr ? rawCrash_->load(std::memory_order_relaxed) : 1.0f;
    const auto globalPersonality = static_cast<robodrummer::DrummerPersonality>(choiceIndex(rawDrummer_, robodrummer::kDrummerPersonalityCount));
    const auto personality = arrangementEnabled && sectionPersonality_ != robodrummer::DrummerPersonality::Balanced
        ? sectionPersonality_ : globalPersonality;
    const auto drummerControls = robodrummer::applyPersonality(character, personality);

    robodrummer::FillSettings fillSettings;
    fillSettings.length = static_cast<robodrummer::FillLength>(choiceIndex(rawFillLength_, 4));
    fillSettings.complexity = std::clamp((rawFillComplexity_ != nullptr ? rawFillComplexity_->load(std::memory_order_relaxed) : 0.5f)
                                         + (drummerControls.complexity - character.complexity), 0.0f, 1.0f);
    fillSettings.surprise = drummerControls.surprise;
    jam_.setFillSettings(fillSettings);
    jam_.setHumanize(drummerControls.humanize);
    jam_.setFeelMs(drummerControls.feelMs);
    jam_.setComplexity(drummerControls.complexity);
    jam_.setCrashScale(drummerControls.crashTendency);

    const auto styleProfile = robodrummer::JamStyleProfile::forStyle(getJamStyle());
    const auto memoryRecommendations = sessionMemory_.recommendations();
    const bool memoryCanAdapt = jamMemoryEnabled_.load(std::memory_order_relaxed) && !arrangementEnabled;
    auto memoryPolicy = robodrummer::SessionMemoryPolicy::apply(
        styleProfile.toBrainSettings(),
        dynamicFollow_.load(std::memory_order_relaxed),
        memoryRecommendations,
        memoryCanAdapt);
    // Fill Frequency: 50% keeps the style's own cadence; higher fills earlier and more readily.
    memoryPolicy.brainSettings.fillBias = std::clamp(memoryPolicy.brainSettings.fillBias + (drummerControls.fillFrequency - 0.5f) * 0.5f, -0.5f, 0.6f);
    memoryPolicy.brainSettings.minBarsBeforeFill = std::max(1, juce::roundToInt(
        static_cast<float>(memoryPolicy.brainSettings.minBarsBeforeFill) * (1.6f - drummerControls.fillFrequency * 1.2f)));

    const float manualIntensity = intensity_.load(std::memory_order_relaxed);
    const float followedIntensity = robodrummer::DynamicsFollower::blend(
        manualIntensity,
        performance.intensity,
        memoryPolicy.dynamicFollow,
        authority.effectiveGuitarAuthority,
        settings.mode);
    const float effectiveIntensity = std::clamp(
        followedIntensity * silenceIntensityMultiplierAudio_ + coordinatorIntensityBiasAudio_ + drummerControls.intensityDelta,
        0.0f,
        1.0f);
    effectiveDrummerIntensity_.store(effectiveIntensity, std::memory_order_relaxed);

    if (practiceDrivesTempo) jam_.setTempoPreservingPosition(authority.outputBpm); // a trainer step keeps the groove on the grid
    else jam_.setTempo(authority.outputBpm);
    jam_.setMeter(numerator, denominator);
    {
        const float limbRaw = rawLimbAmount_ != nullptr ? rawLimbAmount_->load(std::memory_order_relaxed) : 0.0f;
        jam_.setLimbAmount(std::isfinite(limbRaw) ? limbRaw / 100.0f : 0.0f);
    }
    jam_.setIntensity(effectiveIntensity);
    const auto phraseState = getPhraseState();
    const float phraseAmount = phraseState == robodrummer::PhraseState::Break ? 1.0f : 0.75f;
    jam_.setStyle(robodrummer::PhraseGrooveModifier::apply(
        robodrummer::styleFor(getJamStyle()), phraseState, phraseAmount));

    if (hasPpq && settings.mode == robodrummer::LeadershipMode::DrummerLeads)
        jam_.syncToPpq(practiceSessionWanted_ && practiceOffsetSamples_ != 0
                           ? hostPpq + static_cast<double>(practiceOffsetSamples_) / (sampleRate_ * 60.0 / std::max(20.0, authority.outputBpm))
                           : hostPpq);

    // Silence behavior follows the drummer's musical bar clock in every timing mode.
    const auto silenceBarIndex = jam_.currentBarIndex();
    if (lastSilenceBarIndex_ < 0)
        lastSilenceBarIndex_ = silenceBarIndex;

    if (silenceBarIndex != lastSilenceBarIndex_) {
        robodrummer::SilenceSettings silenceSettings;
        silenceSettings.mode = getSilenceMode();
        silenceSettings.stopAfterBars = silenceStopBars_.load(std::memory_order_relaxed);
        const auto silenceDecision = silenceController_.update(performance.activity, silenceSettings);

        silenceIntensityMultiplierAudio_ = silenceDecision.intensityMultiplier;
        silenceHoldGrooveAudio_ = silenceDecision.suppressAdaptiveChanges;
        silentBarsVisible_.store(silenceDecision.silentBars, std::memory_order_relaxed);
        waitingForResume_.store(silenceDecision.waitingForResume, std::memory_order_relaxed);

        if (silenceDecision.requestFill && !jam_.state().fillRequested)
            jam_.requestFill(0.65f);
        if (silenceDecision.stopDrums)
            jam_.apply(robodrummer::MidiCommand::Stop);
        if (silenceDecision.resumeDrums) {
            jam_.apply(robodrummer::MidiCommand::Resume);
            if (silenceDecision.markResumeWithCrash)
                jam_.apply(robodrummer::MidiCommand::Crash);
        }

        lastSilenceBarIndex_ = silenceBarIndex;
    }

    if (settings.mode != robodrummer::LeadershipMode::DrummerLeads) {
        const auto phase = phaseFollower_.update(jam_.currentBeatPhase(), rhythm, authority.outputBpm,
                                                 authority.effectiveGuitarAuthority, settings.response, sampleRate_, beatCorrectionSpeed);
        phaseErrorCycles_.store(phase.phaseErrorCycles, std::memory_order_relaxed);
        hardResyncRecommended_.store(phase.hardResyncRecommended, std::memory_order_relaxed);

        const auto resyncAction = resyncPlanner_.update(phase.hardResyncRecommended, rhythm, blockSeconds);
        if (resyncAction == robodrummer::ResyncAction::BreakAndRealign && adaptiveJoined_) {
            jam_.resetPhase();
            jam_.apply(robodrummer::MidiCommand::Crash);
            hardResyncRecommended_.store(false, std::memory_order_relaxed);
        } else if (!phase.hardResyncRecommended && adaptiveJoined_) {
            jam_.nudgePhaseSamples(phase.correctionSamples);
        }

        if (!adaptiveJoined_) {
            const bool confident = rhythm.locked && rhythm.tempoConfidence >= 0.60f && rhythm.beatConfidence >= 0.55f;
            const bool nearBeatBoundary = rhythm.beatPhase <= 0.08 || rhythm.beatPhase >= 0.92;
            if (confident && nearBeatBoundary) {
                jam_.resetPhase();
                adaptiveJoined_ = true;
                adaptiveJoinedVisible_.store(true, std::memory_order_release);
                resyncPlanner_.reset();
            }
        }
    } else {
        adaptiveJoined_ = true;
        adaptiveJoinedVisible_.store(true, std::memory_order_relaxed);
        phaseErrorCycles_.store(0.0, std::memory_order_relaxed);
        hardResyncRecommended_.store(false, std::memory_order_relaxed);
        resyncPlanner_.reset();
    }

    // Slow musical coordination: evaluate one observation near each trusted guitar downbeat.
    // This intentionally runs at bar timescale, separate from block-by-block tempo/phase tracking.
    phraseBoundary_.store(false, std::memory_order_relaxed);
    jamBrainCooldownSeconds_ = std::max(0.0, jamBrainCooldownSeconds_ - blockSeconds);
    if (settings.mode != robodrummer::LeadershipMode::DrummerLeads &&
        adaptiveJoined_ &&
        authority.effectiveGuitarAuthority >= 0.25f &&
        jamBrainCooldownSeconds_ <= 0.0) {
        const bool nearBeatOne = rhythm.beatInBar == 1 &&
                                 (rhythm.beatPhase <= 0.08 || rhythm.beatPhase >= 0.92) &&
                                 rhythm.downbeatConfidence >= 0.40f;
        if (nearBeatOne) {
            robodrummer::JamBarObservation observation;
            observation.intensity = performance.intensity;
            observation.activity = performance.activity;
            observation.beatConfidence = rhythm.beatConfidence;
            observation.downbeatConfidence = rhythm.downbeatConfidence;

            robodrummer::JamBrainDecision decision;
            if (silenceHoldGrooveAudio_) {
                decision.phraseState = getPhraseState();
            } else {
                decision = jamBrain_.update(observation, memoryPolicy.brainSettings);
            }

            phraseState_.store(static_cast<int>(decision.phraseState), std::memory_order_relaxed);
            phraseFillStrength_.store(decision.fillStrength, std::memory_order_relaxed);
            phraseBoundary_.store(decision.phraseBoundary, std::memory_order_relaxed);

            if (decision.requestFill && !coordinatorSuppressBusyFillsAudio_ && !jam_.state().fillRequested)
                jam_.requestFill(decision.fillStrength);

            if (jamMemoryEnabled_.load(std::memory_order_relaxed) && !arrangementEnabled) {
                robodrummer::SessionBarObservation memoryObservation;
                memoryObservation.tempoBpm = authority.outputBpm;
                memoryObservation.intensity = performance.intensity;
                memoryObservation.activity = performance.activity;
                memoryObservation.phraseBoundary = decision.phraseBoundary;
                memoryObservation.manualFillRequested = manualFillSinceMemoryBar_;
                memoryObservation.fillOccurred = manualFillSinceMemoryBar_ || decision.requestFill;
                sessionMemory_.observeBar(memoryObservation);
                manualFillSinceMemoryBar_ = false;

                const auto memoryStats = sessionMemory_.snapshot();
                const auto updatedMemory = sessionMemory_.recommendations();
                jamMemoryBars_.store(memoryStats.barCount, std::memory_order_relaxed);
                jamMemoryConfidence_.store(updatedMemory.confidence, std::memory_order_relaxed);
                jamMemoryAverageTempo_.store(memoryStats.averageTempoBpm, std::memory_order_relaxed);
                jamMemoryAveragePhraseBars_.store(memoryStats.averagePhraseBars, std::memory_order_relaxed);
                jamMemoryFillBias_.store(updatedMemory.fillBiasAdjustment, std::memory_order_relaxed);
                jamMemoryDynamicSensitivity_.store(updatedMemory.dynamicSensitivity, std::memory_order_relaxed);
            }

            const double barSeconds = (60.0 / std::max(20.0, authority.outputBpm)) *
                                      numerator * (4.0 / static_cast<double>(denominator));
            jamBrainCooldownSeconds_ = std::max(0.5, barSeconds * 0.60);
        }
    }

    // High-level jam coordination runs once per musical bar and never modifies the clock.
    const auto coordinatorBarIndex = jam_.currentBarIndex();
    if (coordinatorBarIndex != lastCoordinatorBarIndex_) {
        robodrummer::JamCoordinatorSettings coordinatorSettings;
        if (arrangementEnabled) {
            coordinatorSettings.mode = arrangement_.empty() || arrangement_.current().autoAdvance
                ? robodrummer::JamMode::ProgrammedSong
                : robodrummer::JamMode::SemiStructured;
        } else {
            coordinatorSettings.mode = robodrummer::JamMode::FreeJam;
        }

        robodrummer::JamCoordinatorObservation coordinatorObservation;
        coordinatorObservation.phraseState = getPhraseState();
        coordinatorObservation.activity = performance.activity;
        coordinatorObservation.intensity = performance.intensity;
        coordinatorObservation.trackingConfidence =
            settings.mode == robodrummer::LeadershipMode::DrummerLeads
                ? (hostPositionAvailable || tempoSource != 0 ? 1.0f : rhythm.beatConfidence)
                : std::min(rhythm.beatConfidence, std::max(rhythm.downbeatConfidence, 0.25f));
        coordinatorObservation.phraseBoundary = phraseBoundary_.load(std::memory_order_relaxed);
        coordinatorObservation.nextSectionCue = coordinatorNextCuePending_;
        coordinatorObservation.soloCue = coordinatorSoloCuePending_;
        coordinatorObservation.endCue = coordinatorEndCuePending_;
        coordinatorObservation.programmedBoundaryDue = coordinatorProgrammedBoundaryPending_;

        const auto coordinatorDecision = jamCoordinator_.update(coordinatorObservation, coordinatorSettings);
        coordinatorIntensityBiasAudio_ = coordinatorDecision.intensityBias;
        coordinatorSuppressBusyFillsAudio_ = coordinatorDecision.suppressBusyFills;

        jamCoordinationState_.store(static_cast<int>(coordinatorDecision.state), std::memory_order_relaxed);
        transitionProbability_.store(coordinatorDecision.transitionProbability, std::memory_order_relaxed);
        endingProbability_.store(coordinatorDecision.endingProbability, std::memory_order_relaxed);
        coordinatorSuppressBusyFillsVisible_.store(coordinatorDecision.suppressBusyFills, std::memory_order_relaxed);

        coordinatorNextCuePending_ = false;
        coordinatorSoloCuePending_ = false;
        coordinatorEndCuePending_ = false;
        coordinatorProgrammedBoundaryPending_ = false;
        lastCoordinatorBarIndex_ = coordinatorBarIndex;
    }

    const auto pending = pendingUiCommands_.exchange(0, std::memory_order_acquire);
    for (int bit = 0; !midiFailSafe && bit < kCommandCount; ++bit)
        if ((pending & (1u << static_cast<unsigned>(bit))) != 0)
            handleCommand(static_cast<robodrummer::MidiCommand>(bit), arrangementEnabled);
    for (std::size_t i = 0; i < midiCommandCount; ++i)
        handleCommand(midiCommands[i], arrangementEnabled);

    // Riff Lock: learn the guitar's rhythm on the drummer's bar grid and hand the engine the latest snapshot.
    feedRiffLock(numSamples, rhythm.locked || (hostPositionAvailable && settings.mode == robodrummer::LeadershipMode::DrummerLeads));

    // No new hits while the transport is stopped (Drummer Leads) or before the adaptive modes have
    // joined the guitarist; already-ringing voices still render so tails decay naturally instead of freezing.
    const bool hostStopped = transportStopped && settings.mode == robodrummer::LeadershipMode::DrummerLeads;
    const bool waitingToJoin = settings.mode != robodrummer::LeadershipMode::DrummerLeads && !adaptiveJoined_;
    std::array<robodrummer::DrumEvent, 128> events{};
    std::size_t eventCount = 0;
    if (midiSafetyStopped_ && hostPositionAvailable) {
        if (transportStopped) midiSafetyHostStopSeen_ = true;
        else if (midiSafetyHostStopSeen_) {
            midiSafetyStopped_ = false;
            midiSafetyHostStopSeen_ = false;
            jam_.apply(rd::MidiCommand::Resume);
        }
    }
    if (midiSafetyStopped_) jam_.apply(rd::MidiCommand::Stop);
    if (hostStopped || waitingToJoin || midiSafetyStopped_) {
        if (practiceSessionWanted_) practiceNeedsResume_ = true; // the next running block re-plans the count-in
    } else if (practiceSessionWanted_ && practice_.isActive()) {
        eventCount = processJamWithPractice(numSamples, events.data(), events.size(), rhythm.beatConfidence, arrangementEnabled);
    } else {
        eventCount = jam_.processBlock(numSamples, events.data(), events.size());
    }
    publishPracticeStatus();
    if (jam_.limbRevision() != limbRevisionSeen_) {
        limbRevisionSeen_ = jam_.limbRevision();
        const auto& r = jam_.limbReport();
        limbValues_[0].store(static_cast<int>(r.inputCount), std::memory_order_relaxed);
        limbValues_[1].store(static_cast<int>(r.keptCount), std::memory_order_relaxed);
        limbValues_[2].store(static_cast<int>(r.removedCount), std::memory_order_relaxed);
        for (std::size_t i = 0; i < 4; ++i) limbValues_[3 + i].store(static_cast<int>(r.removedByReason[i]), std::memory_order_relaxed);
        limbValues_[7].store(static_cast<int>(r.nonDefaultLimb), std::memory_order_relaxed);
        limbValues_[8].store(static_cast<int>(r.protectedKept), std::memory_order_relaxed);
        limbValues_[9].store(static_cast<int>(r.protectedDropped), std::memory_order_relaxed);
        limbValues_[10].store(r.overflow ? 1 : 0, std::memory_order_relaxed);
        limbRevisionPublished_.store(limbRevisionSeen_, std::memory_order_release);
    }
    if (jam_.lastRiffResult().applied) {
        riffAdded_.store(jam_.lastRiffResult().added, std::memory_order_relaxed);
        riffRemoved_.store(jam_.lastRiffResult().removed, std::memory_order_relaxed);
    }
    drummerStopped_.store(jam_.state().stopped, std::memory_order_relaxed);
    timeScale_.store(jam_.state().timeScale, std::memory_order_relaxed);
    drummerBeatInBar_.store(jam_.currentBeatInBar(), std::memory_order_relaxed);
    drummerBarNumber_.store(jam_.currentBarIndex() + 1, std::memory_order_relaxed);
    const auto outputMode = getOutputMode();
    const bool renderInternal = robodrummer::rendersInternalAudio(outputMode) && player_ != nullptr;
    const bool writeMidi = robodrummer::writesGeneratedMidi(outputMode);
    if (!renderInternal && player_ != nullptr)
        player_->clear();

    // ---- drum rendering: main mix (with room + master) and optional aux outputs ---------------------------------
    const int renderSamples = std::min(numSamples, MaxScratchSamples);
    float* auxLeft[robodrummer::DrumSamplePlayer::MaxBuses] {};
    float* auxRight[robodrummer::DrumSamplePlayer::MaxBuses] {};
    float* busLeft[robodrummer::DrumSamplePlayer::MaxBuses] {};
    float* busRight[robodrummer::DrumSamplePlayer::MaxBuses] {};
    if (renderInternal) {
        applyMixerToSampler();
        std::fill_n(drumL_.begin(), renderSamples, 0.0f);
        std::fill_n(drumR_.begin(), renderSamples, 0.0f);
        std::fill_n(roomL_.begin(), renderSamples, 0.0f); // receives every voice, whatever its output routing
        std::fill_n(roomR_.begin(), renderSamples, 0.0f);
        for (int aux = 1; aux <= robodrummer::kAuxBusCount; ++aux) {
            auto* bus = getBus(false, aux);
            if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2) {
                const int first = bus->getChannelIndexInProcessBlockBuffer(0);
                auxLeft[aux] = buffer.getWritePointer(first);
                auxRight[aux] = buffer.getWritePointer(first + 1);
            }
        }
    }
    const auto renderSpan = [&](int from, int span) {
        if (!renderInternal || span <= 0 || from >= renderSamples) return;
        span = std::min(span, renderSamples - from);
        busLeft[0] = drumL_.data() + from;
        busRight[0] = drumR_.data() + from;
        for (int aux = 1; aux < static_cast<int>(robodrummer::DrumSamplePlayer::MaxBuses); ++aux) {
            busLeft[aux] = auxLeft[aux] != nullptr ? auxLeft[aux] + from : nullptr;
            busRight[aux] = auxRight[aux] != nullptr ? auxRight[aux] + from : nullptr;
        }
        player_->renderBuses(busLeft, busRight, static_cast<int>(robodrummer::DrumSamplePlayer::MaxBuses), span,
                             roomL_.data() + from, roomR_.data() + from);
    };

    {
        robodrummer::CaptureBlock captureBlock;
        captureBlock.hostPpqValid = hasPpq;
        captureBlock.ppq = hostPpq;
        captureBlock.playing = !(hostStopped || waitingToJoin);
        captureBlock.bpm = authority.outputBpm;
        captureBlock.numerator = numerator;
        captureBlock.denominator = denominator;
        captureBlock.numSamples = numSamples;
        captureBlock.sampleRate = sampleRate_;
        capture_.onBlock(captureBlock);
    }
    int renderedUntil = 0;
    for (std::size_t i = 0; i < eventCount; ++i) {
        const auto offset = juce::jlimit(0, juce::jmax(0, numSamples - 1), events[i].sampleOffset);
        renderSpan(renderedUntil, offset - renderedUntil);
        if (renderInternal)
            player_->trigger(events[i]);

        if (writeMidi) {
            const int note = midiNoteFor(events[i].instrument);
            const int outChannel = rawMidiOutChannel_ != nullptr ? juce::jlimit(1, 16, juce::roundToInt(rawMidiOutChannel_->load(std::memory_order_relaxed))) : 10;
            if (note >= 0 && numSamples > 0) {
                if (midiPolicy.admitOutput(3, rd::MidiOutputClass::Generated) &&
                    midiPolicy.admitOutput(3, rd::MidiOutputClass::Generated)) {
                    midiOut_.addEvent(juce::MidiMessage::noteOn(outChannel, note, events[i].velocity), offset);
                    midiOut_.addEvent(juce::MidiMessage::noteOff(outChannel, note), juce::jmin(numSamples - 1, offset + 1));
                } else {
                    midiFailSafe = true;
                }
            }
        }
        if (events[i].sequence != kClickTag && midiNoteFor(events[i].instrument) >= 0)
            capture_.noteAtSample(offset, events[i].instrument, events[i].velocity);
        renderedUntil = std::max(renderedUntil, offset);
    }
    renderSpan(renderedUntil, numSamples - renderedUntil);

    float outPeak = 0.0f;
    if (renderInternal) {
        // Drum bus chain on the main-bus voices only: shaper first, bus compressor second. The room return is added
        // afterwards (ambience is not pumped) and the master fader applies to the whole mix; aux stems stay dry.
        shaper_.setParams(readShaperParams());
        drumBus_.setParams(readDrumBusParams());
        shaper_.processBlock(drumL_.data(), drumR_.data(), renderSamples);
        drumBus_.processBlock(drumL_.data(), drumR_.data(), renderSamples);
        const auto channelGain = [&](robodrummer::MixerChannel c) {
            const auto i = static_cast<std::size_t>(c);
            const bool muted = rawMixMute_[i] != nullptr && rawMixMute_[i]->load(std::memory_order_relaxed) >= 0.5f;
            const bool anySolo = [&] {
                for (std::size_t s = 0; s < 6; ++s) if (rawMixSolo_[s] != nullptr && rawMixSolo_[s]->load(std::memory_order_relaxed) >= 0.5f) return true;
                return false;
            }();
            return muted || anySolo ? 0.0f : robodrummer::dbToGain(rawMixGain_[i] != nullptr ? rawMixGain_[i]->load(std::memory_order_relaxed) : 0.0f);
        };
        const float roomGain = channelGain(robodrummer::MixerChannel::Room);
        const float roomPan = rawMixPan_[6] != nullptr ? rawMixPan_[6]->load(std::memory_order_relaxed) : 0.0f;
        const auto masterIdx = static_cast<std::size_t>(robodrummer::MixerChannel::Master);
        const bool masterMuted = rawMixMute_[masterIdx] != nullptr && rawMixMute_[masterIdx]->load(std::memory_order_relaxed) >= 0.5f;
        masterGain_.setTargetValue(masterMuted ? 0.0f : robodrummer::dbToGain(rawMixGain_[masterIdx] != nullptr ? rawMixGain_[masterIdx]->load(std::memory_order_relaxed) : 0.0f));

        const bool useRoom = roomGain > 0.0005f;
        if (useRoom) {
            const float size = rawRoomSize_ != nullptr ? rawRoomSize_->load(std::memory_order_relaxed) : 0.5f;
            if (std::abs(size - lastRoomSize_) > 1.0e-4f) {
                juce::Reverb::Parameters parameters;
                parameters.roomSize = 0.2f + 0.75f * size;
                parameters.damping = 0.55f;
                parameters.wetLevel = 1.0f;
                parameters.dryLevel = 0.0f;
                parameters.width = 1.0f;
                reverb_.setParameters(parameters);
                lastRoomSize_ = size;
            }
            reverb_.processStereo(roomL_.data(), roomR_.data(), renderSamples);
        }
        const float roomLeftGain = roomGain * (roomPan > 0.0f ? 1.0f - roomPan : 1.0f);
        const float roomRightGain = roomGain * (roomPan < 0.0f ? 1.0f + roomPan : 1.0f);
        float* outL = buffer.getWritePointer(0);
        float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
        for (int n = 0; n < renderSamples; ++n) {
            const float master = masterGain_.getNextValue();
            float l = drumL_[static_cast<std::size_t>(n)];
            float r = drumR_[static_cast<std::size_t>(n)];
            if (useRoom) {
                l += roomL_[static_cast<std::size_t>(n)] * roomLeftGain;
                r += roomR_[static_cast<std::size_t>(n)] * roomRightGain;
            }
            l *= master; r *= master;
            outPeak = std::max({ outPeak, std::abs(l), std::abs(r) });
            outL[n] += l;
            if (outR != nullptr) outR[n] += r;
        }
    }
    if (midiFailSafe) {
        midiSafetyStopped_ = true;
        midiSafetyHostStopSeen_ = false;
        jam_.apply(rd::MidiCommand::Stop);
        midiClock_.stop();
        midiClockActive_.store(false, std::memory_order_relaxed);
        if (player_ != nullptr) player_->clear();
        buffer.clear();
        drummerStopped_.store(true, std::memory_order_relaxed);
        emitMidiPanic();
    }
    outputPeak_.store(midiFailSafe ? 0.0f : outPeak, std::memory_order_relaxed);
    // Copy instead of swapWith: a swap hands our pre-sized buffer to the host and leaves us with whatever (possibly tiny)
    // buffer the host owned, so the next block's addEvent could allocate on the audio thread.
    midi.clear();
    midi.addEvents(midiOut_, 0, std::max(1, numSamples), 0);
}

void RoboDrummerAudioProcessor::publishRiffSnapshot(const robodrummer::RiffSnapshot& snapshot) noexcept {
    for (std::size_t i = 0; i < riffConsistency_.size(); ++i) {
        riffConsistency_[i].store(snapshot.consistency[i], std::memory_order_relaxed);
        riffStrength_[i].store(snapshot.strength[i], std::memory_order_relaxed);
    }
    riffConfidence_.store(snapshot.confidence, std::memory_order_relaxed);
    riffRepetition_.store(snapshot.repetition, std::memory_order_relaxed);
    riffBarsSeen_.store(snapshot.barsSeen, std::memory_order_relaxed);
    riffSlots_.store(snapshot.grid.slots, std::memory_order_relaxed);
    riffSlotsPerBeat_.store(snapshot.grid.slotsPerBeat, std::memory_order_relaxed);
    riffNumerator_.store(snapshot.grid.numerator, std::memory_order_relaxed);
    riffDenominator_.store(snapshot.grid.denominator, std::memory_order_relaxed);
    riffTriplet_.store(snapshot.grid.triplet, std::memory_order_relaxed);
    riffLocked_.store(snapshot.locked, std::memory_order_release);
}

robodrummer::DrumMap RoboDrummerAudioProcessor::captureDrumMap() const noexcept {
    robodrummer::DrumMap map;
    for (std::size_t i = 0; i < map.note.size(); ++i)
        map.note[i] = static_cast<std::int16_t>(drumNotes_[i].load(std::memory_order_relaxed));
    return map;
}

void RoboDrummerAudioProcessor::drainCapture() { capture_.drain(); }
void RoboDrummerAudioProcessor::requestCaptureClear() { capture_.requestClear(); }

robodrummer::CaptureResult RoboDrummerAudioProcessor::buildCaptureFile(int bars, juce::MemoryBlock& midiFile) {
    capture_.drain();
    robodrummer::CaptureRequest request;
    request.bars = bars;
    request.map = captureDrumMap();
    std::vector<std::uint8_t> bytes;
    const auto result = capture_.exportSmf(request, bytes);
    midiFile.reset();
    if (result.status == robodrummer::CaptureStatus::Ok) midiFile.append(bytes.data(), bytes.size());
    return result;
}

juce::File RoboDrummerAudioProcessor::writeCaptureTempFile(int bars, robodrummer::CaptureResult* resultOut) {
    namespace fs = std::filesystem;
    juce::MemoryBlock data;
    const auto result = buildCaptureFile(bars, data);
    if (resultOut != nullptr) *resultOut = result;
    if (result.status != robodrummer::CaptureStatus::Ok || captureDirectory_.empty()) return {};

    std::error_code ec;
    fs::create_directories(captureDirectory_, ec);
    if (ec) { if (resultOut != nullptr) resultOut->status = robodrummer::CaptureStatus::WriteFailed; return {}; }

    // File names are limited to [A-Za-z0-9_-]; the style, tempo and length make a dropped clip self-describing.
    const auto styleIndex = juce::jlimit(0, robodrummer::kJamStyleCount - 1, static_cast<int>(getJamStyle()));
    const juce::String style = juce::String(robodrummer::kJamStyleNames[static_cast<std::size_t>(styleIndex)]).retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    const auto bpm = juce::roundToInt(effectiveDrummerBpm_.load(std::memory_order_relaxed));
    const juce::String stem = "RoboDrummer_" + (style.isEmpty() ? juce::String("Groove") : style) + "_" + juce::String(bpm) + "bpm_" + juce::String(result.bars) + "bars";
    fs::path path = captureDirectory_ / (stem + ".mid").toStdString();
    for (int n = 2; fs::exists(path, ec) && n < 1000; ++n)
        path = captureDirectory_ / (stem + "_" + juce::String(n) + ".mid").toStdString();
    const juce::File file(juce::String(path.string()));
    if (!file.replaceWithData(data.getData(), data.getSize())) {
        if (resultOut != nullptr) resultOut->status = robodrummer::CaptureStatus::WriteFailed;
        return {};
    }
    // Keep at most 20 drag files per instance (oldest first).
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& entry : fs::directory_iterator(captureDirectory_, ec)) {
        std::error_code fileError;
        if (entry.is_regular_file(fileError)) files.emplace_back(entry.last_write_time(fileError), entry.path());
    }
    std::sort(files.begin(), files.end());
    for (std::size_t i = 0; files.size() - i > 20; ++i) { std::error_code removeError; fs::remove(files[i].second, removeError); }
    return file;
}

juce::String RoboDrummerAudioProcessor::getCaptureStatusText(int bars) {
    capture_.drain();
    robodrummer::CaptureRequest request;
    request.bars = bars;
    request.map = captureDrumMap();
    robodrummer::MidiExportData data;
    const auto result = capture_.buildExport(request, data);
    switch (result.status) {
        case robodrummer::CaptureStatus::Ok: {
            juce::String text = "Ready: " + juce::String(result.bars) + (result.bars == 1 ? " bar (" : " bars (") + juce::String(juce::roundToInt(result.seconds)) + " s)";
            if (capture_.droppedCount() > 0) text << ". Capture full, oldest bars dropped";
            return text;
        }
        case robodrummer::CaptureStatus::Empty: return "Nothing captured yet";
        case robodrummer::CaptureStatus::InvalidRequest: return "Pick a capture length";
        case robodrummer::CaptureStatus::WriteFailed: break;
    }
    return "Could not create MIDI file";
}

void RoboDrummerAudioProcessor::sweepStaleCaptureFiles() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto root = fs::temp_directory_path(ec) / "RoboDrummer" / "Capture";
    if (ec || !fs::exists(root, ec)) return;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24);
    for (const auto& instance : fs::directory_iterator(root, ec)) {
        std::error_code entryError;
        if (!instance.is_directory(entryError)) continue;
        for (const auto& file : fs::directory_iterator(instance.path(), entryError)) {
            std::error_code fileError;
            if (file.is_regular_file(fileError) && file.last_write_time(fileError) < cutoff) fs::remove(file.path(), fileError);
        }
        std::error_code removeError;
        fs::remove(instance.path(), removeError); // only succeeds when the directory is empty
    }
}

void RoboDrummerAudioProcessor::resetAutoMeter() noexcept {
    meterDetector_.reset();
    meterGate_.reset();
    meterPulseCounter_ = 0;
    meterLastPhase_ = 0.0;
    meterPhaseValid_ = false;
    autoMeterApplied_.store(false, std::memory_order_relaxed);
    autoMeterNum_.store(4, std::memory_order_relaxed);
    autoMeterDen_.store(4, std::memory_order_relaxed);
    autoMeterPulses_.store(4, std::memory_order_relaxed);
    autoMeterTernary_.store(false, std::memory_order_relaxed);
    autoMeterConfidence_.store(0.0f, std::memory_order_relaxed);
}

robodrummer::AutoMeterStatus RoboDrummerAudioProcessor::getAutoMeterStatus() const noexcept {
    robodrummer::AutoMeterStatus status;
    status.enabled = autoMeterEnabledVisible_.load(std::memory_order_relaxed);
    status.applied = autoMeterApplied_.load(std::memory_order_relaxed);
    status.numerator = autoMeterNum_.load(std::memory_order_relaxed);
    status.denominator = autoMeterDen_.load(std::memory_order_relaxed);
    status.pulsesPerBar = autoMeterPulses_.load(std::memory_order_relaxed);
    status.confidence = autoMeterConfidence_.load(std::memory_order_relaxed);
    status.ternary = autoMeterTernary_.load(std::memory_order_relaxed);
    return status;
}

juce::String RoboDrummerAudioProcessor::getAutoMeterReadout() const {
    const auto status = getAutoMeterStatus();
    if (!status.enabled) return "Auto meter: off";
    robodrummer::MeterDetector::Suggestion suggestion;
    suggestion.numerator = status.numerator;
    suggestion.denominator = status.denominator;
    suggestion.confidence = status.confidence;
    char text[48];
    robodrummer::AutoMeterGate::formatReadout(text, sizeof(text), suggestion);
    return juce::String(text);
}

robodrummer::LimbSummary RoboDrummerAudioProcessor::getLimbSummary() const noexcept {
    robodrummer::LimbSummary summary;
    summary.revision = limbRevisionPublished_.load(std::memory_order_acquire);
    summary.input = limbValues_[0].load(std::memory_order_relaxed);
    summary.kept = limbValues_[1].load(std::memory_order_relaxed);
    summary.removed = limbValues_[2].load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < 4; ++i) summary.byReason[i] = limbValues_[3 + i].load(std::memory_order_relaxed);
    summary.nonDefaultLimb = limbValues_[7].load(std::memory_order_relaxed);
    summary.protectedKept = limbValues_[8].load(std::memory_order_relaxed);
    summary.protectedDropped = limbValues_[9].load(std::memory_order_relaxed);
    summary.overflow = limbValues_[10].load(std::memory_order_relaxed) != 0;
    return summary;
}

robodrummer::RiffSnapshot RoboDrummerAudioProcessor::getRiffSnapshot() const noexcept {
    robodrummer::RiffSnapshot snapshot;
    for (std::size_t i = 0; i < riffConsistency_.size(); ++i) {
        snapshot.consistency[i] = riffConsistency_[i].load(std::memory_order_relaxed);
        snapshot.strength[i] = riffStrength_[i].load(std::memory_order_relaxed);
    }
    snapshot.confidence = riffConfidence_.load(std::memory_order_relaxed);
    snapshot.repetition = riffRepetition_.load(std::memory_order_relaxed);
    snapshot.barsSeen = riffBarsSeen_.load(std::memory_order_relaxed);
    snapshot.grid.slots = riffSlots_.load(std::memory_order_relaxed);
    snapshot.grid.slotsPerBeat = riffSlotsPerBeat_.load(std::memory_order_relaxed);
    snapshot.grid.numerator = riffNumerator_.load(std::memory_order_relaxed);
    snapshot.grid.denominator = riffDenominator_.load(std::memory_order_relaxed);
    snapshot.grid.triplet = riffTriplet_.load(std::memory_order_relaxed);
    snapshot.locked = riffLocked_.load(std::memory_order_acquire);
    return snapshot;
}

// Audio thread, once per block after the meter and the drummer's bar grid are known. Onsets are quantised onto the
// drummer's grid (that is where the kick will land); a finished bar is committed before the engine generates the next one.
void RoboDrummerAudioProcessor::feedRiffLock(int numSamples, bool trackerLocked) noexcept {
    namespace rd = robodrummer;
    const auto amountRaw = rawRlAmount_ != nullptr ? rawRlAmount_->load(std::memory_order_relaxed) : 0.0f;
    const auto sensitivityRaw = rawRlSensitivity_ != nullptr ? rawRlSensitivity_->load(std::memory_order_relaxed) : 50.0f;
    rd::RiffLockParams params;
    params.amount = std::isfinite(amountRaw) ? juce::jlimit(0.0f, 1.0f, amountRaw / 100.0f) : 0.0f;
    params.sensitivity = std::isfinite(sensitivityRaw) ? juce::jlimit(0.0f, 1.0f, sensitivityRaw / 100.0f) : 0.5f;
    params.accentCrash = rawRlAccent_ != nullptr && rawRlAccent_->load(std::memory_order_relaxed) >= 0.5f;

    riffLock_.configure(effectiveMeterNumerator_.load(std::memory_order_relaxed), effectiveMeterDenominator_.load(std::memory_order_relaxed),
                        rd::styleFor(getJamStyle()).swing);
    riffLock_.setTrackerLocked(trackerLocked);
    bool committed = false;
    const auto advanceTo = [&](long long bar) {
        if (riffBar_ < 0 || bar < riffBar_) { riffBar_ = bar; return; } // first block, or a seek / loop wrap: no bar to commit
        int guard = 0;
        while (riffBar_ < bar && guard < 8) {
            ++riffBar_;
            ++guard;
            riffLock_.endBar(); committed = true;
        }
        riffBar_ = bar; // eight decay updates suffice, irrespective of seek distance
    };
    for (std::size_t i = 0; i < blockOnsetCount_; ++i) {
        long long bar = 0; double phase = 0.0;
        jam_.barPhaseAt(blockOnsets_[i].offset, bar, phase);
        advanceTo(bar);
        riffLock_.addOnset(phase, blockOnsets_[i].strength, blockOnsets_[i].low);
    }
    long long lastBar = 0; double ignored = 0.0;
    jam_.barPhaseAt(std::max(0, numSamples - 1), lastBar, ignored);
    advanceTo(lastBar);
    const auto snapshot = riffLock_.snapshot();
    if (committed) publishRiffSnapshot(snapshot);
    else riffLocked_.store(snapshot.locked, std::memory_order_release);
    jam_.setRiffLock(snapshot, params);
}

// ---- Auto Setup ------------------------------------------------------------------------------------------------------
void RoboDrummerAudioProcessor::startAutoSetup(float seconds) {
    const auto transaction = lockNonRealtimeStateChange();
    if (autoSetup_.state() == robodrummer::AutoSetupState::Listening) return;
    clearAutoSetupUndo();
    autoSetup_.start(seconds);
}

void RoboDrummerAudioProcessor::cancelAutoSetup() { const auto transaction = lockNonRealtimeStateChange(); autoSetup_.cancel(); }

void RoboDrummerAudioProcessor::finalizeAutoSetupIfReady() {
    const auto transaction = lockNonRealtimeStateChange();
    if (autoSetup_.state() == robodrummer::AutoSetupState::Analyzing) autoSetup_.finalize();
}

robodrummer::AutoSetupSnapshot RoboDrummerAudioProcessor::getAutoSetupCurrentValues() const {
    namespace ids = robodrummer::ParamIDs;
    robodrummer::AutoSetupSnapshot s;
    s.inputGainDb = rawValue(ids::inputGain, s.inputGainDb);
    s.autoGain = rawValue(ids::autoGain, 0.0f) >= 0.5f;
    s.gateThresholdDb = rawValue(ids::gateThreshold, s.gateThresholdDb);
    s.followStrength = rawValue(ids::followStrength, s.followStrength);
    s.tempoInertia = rawValue(ids::tempoInertia, s.tempoInertia);
    s.confidenceSensitivity = rawValue(ids::confidenceSensitivity, s.confidenceSensitivity);
    s.timingMode = static_cast<robodrummer::LeadershipMode>(juce::jlimit(0, 2, juce::roundToInt(rawValue(ids::leadershipMode, 0.0f))));
    s.leadership = rawValue(ids::leadership, s.leadership);
    s.bpm = rawValue(ids::bpm, s.bpm);
    s.jamStyle = static_cast<robodrummer::JamStyle>(juce::jlimit(0, robodrummer::kJamStyleCount - 1, juce::roundToInt(rawValue(ids::jamStyle, 0.0f))));
    s.intensity = rawValue(ids::intensity, s.intensity);
    return s;
}

// Writes the masked fields through the host-notifying API, one change gesture per parameter (message thread).
bool RoboDrummerAudioProcessor::writeAutoSetupSnapshot(const robodrummer::AutoSetupSnapshot& v, std::uint32_t mask,
                                                       std::uint64_t expectedGeneration) {
    namespace ids = robodrummer::ParamIDs;
    bool valid = true;
    const auto current = [&] { return autoSetup_.generation() == expectedGeneration; };
    const auto write = [&](const char* id, float plain) {
        if (!valid || !current()) { valid = false; return; }
        if (auto* parameter = parameters_.getParameter(id)) {
            parameter->beginChangeGesture();
            // Host notifications may reenter state restoration on this thread.
            if (current()) parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
            parameter->endChangeGesture();
            valid = current();
        }
    };
    if (mask & robodrummer::kAsInputGain) write(ids::inputGain, v.inputGainDb);
    if (mask & robodrummer::kAsAutoGain) write(ids::autoGain, v.autoGain ? 1.0f : 0.0f);
    if (mask & robodrummer::kAsGate) write(ids::gateThreshold, v.gateThresholdDb);
    if (mask & robodrummer::kAsFollowStrength) write(ids::followStrength, v.followStrength);
    if (mask & robodrummer::kAsInertia) write(ids::tempoInertia, v.tempoInertia);
    if (mask & robodrummer::kAsConfidence) write(ids::confidenceSensitivity, v.confidenceSensitivity);
    if (mask & robodrummer::kAsTimingMode) write(ids::leadershipMode, static_cast<float>(static_cast<int>(v.timingMode)));
    if (mask & robodrummer::kAsLeadership) write(ids::leadership, v.leadership);
    if (mask & robodrummer::kAsBpm) write(ids::bpm, v.bpm);
    if (mask & robodrummer::kAsStyle) write(ids::jamStyle, static_cast<float>(static_cast<int>(v.jamStyle)));
    if (mask & robodrummer::kAsIntensity) write(ids::intensity, v.intensity);
    return valid && current();
}

bool RoboDrummerAudioProcessor::applyAutoSetup() {
    const auto transaction = lockNonRealtimeStateChange();
    if (autoSetup_.state() != robodrummer::AutoSetupState::Done || autoSetup_.result().status != robodrummer::AutoSetupStatus::Ok) return false;
    return applyAutoSetup(autoSetup_.result().proposal);
}

bool RoboDrummerAudioProcessor::applyAutoSetup(const robodrummer::AutoSetupProposal& selected) {
    const auto transaction = lockNonRealtimeStateChange();
    if (!autoSetup_.accepts(selected)) return false;
    const auto mask = robodrummer::appliedMask(selected);
    if (mask == 0) return false;
    const auto current = getAutoSetupCurrentValues();
    autoSetupUndo_.capture(current, selected);
    if (!writeAutoSetupSnapshot(robodrummer::applyProposal(selected, current), mask, selected.generation)) return false;
    autoSetupUndoGeneration_.store(selected.generation, std::memory_order_release);
    return true;
}

bool RoboDrummerAudioProcessor::undoAutoSetup() {
    const auto transaction = lockNonRealtimeStateChange();
    if (!canUndoAutoSetup()) return false;
    const auto mask = autoSetupUndo_.mask();
    const auto restored = autoSetupUndo_.restore(getAutoSetupCurrentValues());
    if (!writeAutoSetupSnapshot(restored, mask, getAutoSetupGeneration())) return false;
    clearAutoSetupUndo();
    return true;
}

// ---- practice mode ---------------------------------------------------------------------------------------------------
float RoboDrummerAudioProcessor::pracValue(std::size_t index, float fallback) const noexcept {
    const auto* raw = index < rawPrac_.size() ? rawPrac_[index] : nullptr;
    const float value = raw != nullptr ? raw->load(std::memory_order_relaxed) : fallback;
    return std::isfinite(value) ? value : fallback;
}

void RoboDrummerAudioProcessor::resetPracticeRuntime() noexcept {
    practiceTimelineValid_ = false;
    practiceNeedsResume_ = false;
    practiceMuted_ = false;
    practiceWasResting_ = false;
    practiceAppliedDelta_ = 0;
    practiceOffsetSamples_ = 0;
    practiceLoopWanted_ = false;
    practiceLoopIsRange_ = false;
    practiceLoopSnap_ = {};
    practiceRestSnap_ = {};
    practiceLast_ = {};
}

void RoboDrummerAudioProcessor::updatePracticeSettings(const robodrummer::TimingAuthoritySettings& settings, bool externalTempo) noexcept {
    namespace rd = robodrummer;
    rd::CountInSettings countIn;
    countIn.length = static_cast<rd::CountInLength>(juce::jlimit(0, 4, juce::roundToInt(pracValue(PracIdx::CountLen, 0.0f))));
    countIn.sound = pracValue(PracIdx::CountSound, 0.0f) >= 0.5f ? rd::CountInSound::Sticks : rd::CountInSound::Click;
    countIn.onlyOnStart = pracValue(PracIdx::CountStartOnly, 1.0f) >= 0.5f;
    practice_.setCountIn(countIn);

    static constexpr int kLoopBars[4] = { 1, 2, 4, 8 };
    rd::LoopSettings loop;
    loop.bars = kLoopBars[juce::jlimit(0, 3, juce::roundToInt(pracValue(PracIdx::LoopBars, 2.0f)))];
    if (pracValue(PracIdx::LoopMode, 0.0f) >= 0.5f && arrangementEnabled_.load(std::memory_order_relaxed) && !arrangement_.empty()) {
        if (!practice_.loopActive()) { // the range is fixed once the loop has started
            const auto a = static_cast<std::size_t>(juce::jlimit(1, 16, juce::roundToInt(pracValue(PracIdx::SecFirst, 1.0f))) - 1);
            const auto b = static_cast<std::size_t>(juce::jlimit(1, 16, juce::roundToInt(pracValue(PracIdx::SecLast, 1.0f))) - 1);
            practiceRangeFirst_ = std::min(std::min(a, b), arrangement_.size() - 1);
            practiceRange_ = robodrummer::sectionLoopRange(arrangement_, std::min(a, b), std::max(a, b), lastArrangementBarIndex_);
        }
        if (practiceRange_.valid) {
            loop.mode = rd::LoopMode::Range;
            loop.rangeStartBar = practiceRange_.startBar;
            loop.rangeBars = practiceRange_.lengthBars;
        }
    }
    if (!practice_.loopActive()) practiceLoopIsRange_ = loop.mode == rd::LoopMode::Range;
    practice_.setLoop(loop);

    rd::TrainerSettings trainer;
    trainer.enabled = pracValue(PracIdx::TrainOn, 0.0f) >= 0.5f;
    trainer.startBpm = static_cast<double>(pracValue(PracIdx::TrainStart, 80.0f));
    trainer.targetBpm = static_cast<double>(pracValue(PracIdx::TrainTarget, 120.0f));
    trainer.stepBpm = static_cast<double>(pracValue(PracIdx::TrainStep, 5.0f));
    trainer.every = juce::jlimit(1, 64, juce::roundToInt(pracValue(PracIdx::TrainEvery, 4.0f)));
    trainer.unit = pracValue(PracIdx::TrainUnit, 0.0f) >= 0.5f ? rd::TrainerUnit::LoopPasses : rd::TrainerUnit::Bars;
    trainer.direction = static_cast<rd::TrainerDirection>(juce::jlimit(0, 2, juce::roundToInt(pracValue(PracIdx::TrainDir, 0.0f))));
    trainer.gate = pracValue(PracIdx::TrainGate, 0.0f) >= 0.5f;
    trainer.gateMinConfidence = pracValue(PracIdx::TrainGateMin, 0.6f);
    trainer.restBar = pracValue(PracIdx::TrainRest, 0.0f) >= 0.5f;
    trainer.stopAtTarget = pracValue(PracIdx::TrainStopEnd, 0.0f) >= 0.5f;
    practice_.setTrainer(trainer);

    practice_.setAuthority(settings.mode, settings.leadership);
    practice_.setExternalTempoSource(externalTempo);
}

void RoboDrummerAudioProcessor::restoreArrangementSnapshot(const ArrangementSnapshot& snapshot) noexcept {
    if (!snapshot.valid || !arrangementEnabled_.load(std::memory_order_relaxed) || arrangement_.empty()) return;
    const auto before = arrangement_.currentSectionIndex();
    arrangement_.restorePosition(snapshot.section, snapshot.bars);
    lastArrangementBarIndex_ = snapshot.bar; // the next block advances the sequencer from here to the rewound bar
    if (arrangement_.currentSectionIndex() != before) {
        publishArrangementSection();
        applyCurrentArrangementSection();
    }
}

std::size_t RoboDrummerAudioProcessor::processJamWithPractice(int numSamples, robodrummer::DrumEvent* out, std::size_t capacity,
                                                              float trackerConfidence, bool arrangementEnabled) noexcept {
    namespace rd = robodrummer;
    if (practiceNeedsResume_) {
        practiceNeedsResume_ = false;
        practiceTimelineValid_ = false;
        practice_.resume();
    }
    const bool loopWanted = pracValue(PracIdx::LoopOn, 0.0f) >= 0.5f;
    if (loopWanted != practiceLoopWanted_) {
        practiceLoopWanted_ = loopWanted;
        if (loopWanted) practice_.armLoop(); else practice_.disarmLoop();
    }

    std::size_t count = 0;
    int pos = 0;
    int from = 0;
    while (pos < numSamples) {
        int offset = 0;
        long long beat = 0;
        const bool hasBeat = jam_.nextBeat(from, numSamples - pos, offset, beat);
        const int segmentEnd = hasBeat ? pos + offset : numSamples;
        if (segmentEnd > pos) {
            std::array<rd::DrumEvent, 128> segment{};
            const auto produced = jam_.processBlock(segmentEnd - pos, segment.data(), segment.size());
            if (!practiceMuted_)
                for (std::size_t i = 0; i < produced && count < capacity; ++i) {
                    out[count] = segment[i];
                    out[count].sampleOffset += pos;
                    ++count;
                }
            pos = segmentEnd;
        }
        if (!hasBeat) break;
        from = 1;

        const int numerator = std::max(1, jam_.meterNumerator());
        const int beatInBar = static_cast<int>(beat % numerator);
        if (!practiceTimelineValid_ || numerator != practiceTimelineNumerator_) {
            practiceTimelineBar_ = beat / numerator;
            practiceTimelineNumerator_ = numerator;
            practiceTimelineValid_ = true;
        } else if (beatInBar == 0) {
            ++practiceTimelineBar_;
        }

        rd::PracticeBeat practiceBeat;
        practiceBeat.barIndex = practiceTimelineBar_;
        practiceBeat.beatInBar = beatInBar;
        practiceBeat.numerator = numerator;
        practiceBeat.trackerConfidence = trackerConfidence;
        const auto action = practice_.onBeat(practiceBeat);
        practiceLast_ = action;
        practiceMuted_ = action.muteGroove;

        if (action.gateHeld) pracGateHeld_.store(true, std::memory_order_relaxed);
        if (action.tempoStepped) pracGateHeld_.store(false, std::memory_order_relaxed);

        if (action.restBar && !practiceWasResting_ && arrangementEnabled && !arrangement_.empty())
            practiceRestSnap_ = { true, arrangement_.currentSectionIndex(), arrangement_.barsIntoSection(), lastArrangementBarIndex_ };
        practiceWasResting_ = action.restBar;
        if (action.loopStarted && arrangementEnabled && !arrangement_.empty()) {
            practiceLoopSnap_ = practiceLoopIsRange_
                ? ArrangementSnapshot{ true, practiceRangeFirst_, 0, practiceRange_.startBar }
                : ArrangementSnapshot{ true, arrangement_.currentSectionIndex(), arrangement_.barsIntoSection(), lastArrangementBarIndex_ };
        }
        if (action.contentJump) {
            const long long delta = action.contentBarDelta - practiceAppliedDelta_;
            practiceAppliedDelta_ = action.contentBarDelta;
            const long long samples = delta * jam_.currentBarSamples();
            jam_.jumpPhaseSamples(samples);
            practiceOffsetSamples_ += samples;
            restoreArrangementSnapshot(action.loopWrapped ? practiceLoopSnap_ : practiceRestSnap_);
        }
        if (action.click.active && count < capacity) {
            const auto instrument = action.click.sound == rd::CountInSound::Sticks ? rd::DrumInstrument::Sidestick : rd::DrumInstrument::Cowbell;
            out[count++] = { instrument, pos, action.click.accent ? 1.0f : 0.7f, kClickTag };
        }
        if (action.stopRequested) handleCommand(rd::MidiCommand::Stop, arrangementEnabled);
    }
    return count;
}

void RoboDrummerAudioProcessor::publishPracticeStatus() noexcept {
    const bool running = practiceSessionWanted_ && practice_.isActive();
    pracSession_.store(running, std::memory_order_relaxed);
    pracPhase_.store(running ? static_cast<int>(practice_.phase()) : 0, std::memory_order_relaxed);
    const bool counting = running && practice_.phase() == robodrummer::PracticePhase::CountIn;
    pracClickBeat_.store(counting ? practiceLast_.click.beatNumber : 0, std::memory_order_relaxed);
    pracClickTotal_.store(counting ? practiceLast_.click.totalBeats : 0, std::memory_order_relaxed);
    pracLoopArmed_.store(running && practice_.loopArmed(), std::memory_order_relaxed);
    pracLoopActive_.store(running && practice_.loopActive(), std::memory_order_relaxed);
    pracLoopPass_.store(running ? practice_.loopPass() : 0, std::memory_order_relaxed);
    const bool trainerOn = running && pracValue(PracIdx::TrainOn, 0.0f) >= 0.5f;
    pracTrainOn_.store(trainerOn, std::memory_order_relaxed);
    pracTrainFinished_.store(trainerOn && practice_.trainerFinished(), std::memory_order_relaxed);
    pracTrainDrives_.store(trainerOn && practice_.trainerDrivesBase(), std::memory_order_relaxed);
    pracBpm_.store(trainerOn ? practice_.trainerBpm() : 0.0, std::memory_order_relaxed);
    pracStep_.store(running ? practice_.trainerStepCount() : 0, std::memory_order_relaxed);
    pracRest_.store(running && practiceLast_.restBar, std::memory_order_relaxed);
    if (!trainerOn) pracGateHeld_.store(false, std::memory_order_relaxed);
}

robodrummer::PracticeStatus RoboDrummerAudioProcessor::getPracticeStatus() const noexcept {
    robodrummer::PracticeStatus s;
    s.enabled = pracSession_.load(std::memory_order_relaxed);
    s.phase = pracPhase_.load(std::memory_order_relaxed);
    s.countInBeat = pracClickBeat_.load(std::memory_order_relaxed);
    s.countInTotal = pracClickTotal_.load(std::memory_order_relaxed);
    s.loopArmed = pracLoopArmed_.load(std::memory_order_relaxed);
    s.loopActive = pracLoopActive_.load(std::memory_order_relaxed);
    s.loopPass = pracLoopPass_.load(std::memory_order_relaxed);
    s.trainerOn = pracTrainOn_.load(std::memory_order_relaxed);
    s.trainerFinished = pracTrainFinished_.load(std::memory_order_relaxed);
    s.trainerDrives = pracTrainDrives_.load(std::memory_order_relaxed);
    s.trainerBpm = pracBpm_.load(std::memory_order_relaxed);
    s.trainerStep = pracStep_.load(std::memory_order_relaxed);
    s.gateHeld = pracGateHeld_.load(std::memory_order_relaxed);
    s.restBar = pracRest_.load(std::memory_order_relaxed);
    return s;
}

juce::String RoboDrummerAudioProcessor::getPracticeStatusText() const {
    const auto s = getPracticeStatus();
    if (!s.enabled) return "Practice: off";
    juce::StringArray parts;
    if (s.phase == static_cast<int>(robodrummer::PracticePhase::Waiting)) parts.add("Count-in: waiting for the next bar");
    else if (s.phase == static_cast<int>(robodrummer::PracticePhase::CountIn)) parts.add("Count-in " + juce::String(s.countInBeat) + "/" + juce::String(s.countInTotal));
    if (s.loopActive) parts.add("Loop pass " + juce::String(s.loopPass + 1));
    else if (s.loopArmed) parts.add("Loop armed (starts at the next bar)");
    if (s.trainerOn) {
        if (s.trainerFinished) parts.add("Trainer done at " + juce::String(juce::roundToInt(s.trainerBpm)) + " BPM");
        else if (s.trainerDrives) parts.add("Trainer " + juce::String(juce::roundToInt(s.trainerBpm)) + " BPM");
        else parts.add("Trainer: hint only, guitarist leads (aim for " + juce::String(juce::roundToInt(s.trainerBpm)) + " BPM)");
        if (s.gateHeld) parts.add("waiting for steady timing");
        if (s.restBar) parts.add("rest bar");
    }
    if (parts.isEmpty()) parts.add("Practice: running");
    return parts.joinIntoString(" | ");
}

// Message thread: flips the practice parameters that MIDI commands asked for, so the host sees the change.
void RoboDrummerAudioProcessor::drainParameterToggles() {
    const auto bits = pendingParamToggles_.exchange(0, std::memory_order_acquire);
    if (bits == 0) return;
    for (std::size_t i = 0; i < kPracticeIds.size(); ++i) {
        if ((bits & (1u << i)) == 0) continue;
        auto* parameter = parameters_.getParameter(kPracticeIds[i]);
        if (parameter == nullptr) continue;
        float next = parameter->getValue() >= 0.5f ? 0.0f : 1.0f;
        if (i == PracIdx::CountLen) {
            const float current = parameter->convertFrom0to1(parameter->getValue());
            if (current >= 0.5f) { practiceLastCountIn_ = current; next = 0.0f; }
            else next = parameter->convertTo0to1(practiceLastCountIn_);
            if (current < 0.5f && !(practiceLastCountIn_ >= 1.0f)) next = parameter->convertTo0to1(4.0f);
        }
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(next);
        parameter->endChangeGesture();
    }
}

void RoboDrummerAudioProcessor::handleCommand(robodrummer::MidiCommand command, bool arrangementEnabled) noexcept {
    if (command == robodrummer::MidiCommand::Resume) midiSafetyStopped_ = false;
    using C = robodrummer::MidiCommand;
    switch (command) {
        case C::SoloSupport: coordinatorSoloCuePending_ = true; return;
        case C::EndJam: coordinatorEndCuePending_ = true; break; // also plays the outro and stops
        case C::ResetListening: resetListeningState(); return;
        case C::NextSection:
            coordinatorNextCuePending_ = true;
            if (!arrangementEnabled) break;
            if (arrangement_.next()) enterArrangementSection(false);
            return;
        case C::PreviousSection:
            if (!arrangementEnabled) break;
            if (arrangement_.previous()) enterArrangementSection(false);
            return;
        case C::PracticeToggle: pendingParamToggles_.fetch_or(1u << PracIdx::Enabled, std::memory_order_release); return;
        case C::LoopToggle: pendingParamToggles_.fetch_or(1u << PracIdx::LoopOn, std::memory_order_release); return;
        case C::TrainerToggle: pendingParamToggles_.fetch_or(1u << PracIdx::TrainOn, std::memory_order_release); return;
        case C::CountInToggle: pendingParamToggles_.fetch_or(1u << PracIdx::CountLen, std::memory_order_release); return;
        case C::TrainerRestart: practice_.restartTrainer(); return;
        case C::Fill:
        case C::ImmediateFill: manualFillSinceMemoryBar_ = true; break;
        case C::IntensityUp:
        case C::IntensityDown:
            setIntensity(robodrummer::applyIntensityCommand(intensity_.load(std::memory_order_relaxed), command));
            intensityNeedsHostSync_.store(true, std::memory_order_release);
            break;
        case C::Crash:
        case C::HalfTime:
        case C::DoubleTime:
        case C::Break:
        case C::Stop:
        case C::Resume:
            break;
    }
    jam_.apply(command);
}

void RoboDrummerAudioProcessor::enterArrangementSection(bool) noexcept {
    publishArrangementSection();
    applyCurrentArrangementSection();
    coordinatorProgrammedBoundaryPending_ = true;
    // A "None" transition enters the section without a cymbal; every other transition marks the entry.
    if (arrangement_.empty() || arrangement_.current().transition != robodrummer::SectionTransition::None)
        jam_.apply(robodrummer::MidiCommand::Crash);
}

void RoboDrummerAudioProcessor::resetListeningState() noexcept {
    jam_.apply(robodrummer::MidiCommand::ResetListening);
    rhythmAnalyzer_.reset();
    performanceAnalyzer_.reset();
    conditioner_.reset();
    timingAuthority_.reset(internalBpm_.load(std::memory_order_relaxed));
    resyncPlanner_.reset();
    jamBrain_.reset();
    jamCoordinator_.reset();
    silenceController_.reset();
    silenceIntensityMultiplierAudio_ = 1.0f;
    silenceHoldGrooveAudio_ = false;
    lastSilenceBarIndex_ = -1;
    lastCoordinatorBarIndex_ = -1;
    coordinatorNextCuePending_ = false;
    coordinatorSoloCuePending_ = false;
    coordinatorEndCuePending_ = false;
    coordinatorProgrammedBoundaryPending_ = false;
    coordinatorIntensityBiasAudio_ = 0.0f;
    coordinatorSuppressBusyFillsAudio_ = false;
    jamCoordinationState_.store(static_cast<int>(robodrummer::JamCoordinationState::EstablishingGroove), std::memory_order_relaxed);
    transitionProbability_.store(0.0f, std::memory_order_relaxed);
    endingProbability_.store(0.0f, std::memory_order_relaxed);
    coordinatorSuppressBusyFillsVisible_.store(false, std::memory_order_relaxed);
    silentBarsVisible_.store(0, std::memory_order_relaxed);
    waitingForResume_.store(false, std::memory_order_relaxed);
    sessionMemory_.reset();
    shaper_.reset();
    drumBus_.reset();
    manualFillSinceMemoryBar_ = false;
    jamMemoryBars_.store(0, std::memory_order_relaxed);
    jamMemoryConfidence_.store(0.0f, std::memory_order_relaxed);
    jamMemoryAverageTempo_.store(0.0, std::memory_order_relaxed);
    jamMemoryAveragePhraseBars_.store(0.0f, std::memory_order_relaxed);
    jamMemoryFillBias_.store(0.0f, std::memory_order_relaxed);
    jamMemoryDynamicSensitivity_.store(1.0f, std::memory_order_relaxed);
    jamBrainCooldownSeconds_ = 0.0;
    phraseState_.store(static_cast<int>(robodrummer::PhraseState::Stable), std::memory_order_relaxed);
    phraseFillStrength_.store(0.0f, std::memory_order_relaxed);
    phraseBoundary_.store(false, std::memory_order_relaxed);
    adaptiveJoined_ = false;
    adaptiveJoinedVisible_.store(false, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* RoboDrummerAudioProcessor::createEditor() { return new RoboDrummerAudioProcessorEditor(*this); }

// ---------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------

juce::ValueTree RoboDrummerAudioProcessor::createArrangementState() const {
    juce::ValueTree arrangement("Arrangement");
    for (int i = 0; i < ArrangementSlotCount; ++i) {
        const auto slot = getArrangementSlot(i);
        const auto prefix = juce::String("arr") + juce::String(i);
        const auto set = [&](const char* suffix, const juce::var& value) { arrangement.setProperty(juce::Identifier(prefix + suffix), value, nullptr); };
        set("Style", static_cast<int>(slot.section.style));
        set("Bars", slot.section.bars);
        set("Intensity", slot.section.intensityTarget);
        set("Enabled", slot.enabled);
        set("Auto", slot.section.autoAdvance);
        set("Feel", static_cast<int>(slot.section.timeFeel));
        set("Transition", static_cast<int>(slot.section.transition));
        set("Personality", static_cast<int>(slot.section.personality));
        set("FillFreq", slot.section.fillFrequency);
        set("Leadership", slot.section.leadership);
        set("Follow", slot.section.followStrength);
        set("Name", getArrangementName(i));
    }
    return arrangement;
}

void RoboDrummerAudioProcessor::applyArrangementState(const juce::ValueTree& source) {
    if (!source.isValid()) return;
    for (int i = 0; i < ArrangementSlotCount; ++i) {
        auto slot = getArrangementSlot(i);
        const auto prefix = juce::String("arr") + juce::String(i);
        const auto get = [&](const char* suffix, const juce::var& fallback) { return source.getProperty(juce::Identifier(prefix + suffix), fallback); };
        slot.section.style = static_cast<robodrummer::JamStyle>(juce::jlimit(0, robodrummer::kJamStyleCount - 1, static_cast<int>(get("Style", static_cast<int>(slot.section.style)))));
        slot.section.bars = juce::jlimit(1, 64, static_cast<int>(get("Bars", slot.section.bars)));
        slot.section.intensityTarget = juce::jlimit(0.0f, 1.0f, static_cast<float>(get("Intensity", slot.section.intensityTarget)));
        slot.enabled = static_cast<bool>(get("Enabled", slot.enabled));
        slot.section.autoAdvance = static_cast<bool>(get("Auto", slot.section.autoAdvance));
        slot.section.timeFeel = static_cast<robodrummer::SectionTimeFeel>(juce::jlimit(0, 2, static_cast<int>(get("Feel", 0))));
        slot.section.transition = static_cast<robodrummer::SectionTransition>(juce::jlimit(0, 3, static_cast<int>(get("Transition", 0))));
        slot.section.personality = static_cast<robodrummer::DrummerPersonality>(juce::jlimit(0, robodrummer::kDrummerPersonalityCount - 1, static_cast<int>(get("Personality", 0))));
        slot.section.fillFrequency = static_cast<float>(get("FillFreq", -1.0f));
        slot.section.leadership = static_cast<float>(get("Leadership", -1.0f));
        slot.section.followStrength = static_cast<float>(get("Follow", -1.0f));
        setArrangementSlot(i, slot);
        setArrangementName(i, get("Name", "").toString());
    }
}

juce::ValueTree RoboDrummerAudioProcessor::createMidiMapState() const {
    juce::ValueTree map("MidiMap");
    for (int cc = 0; cc < 128; ++cc) {
        const int target = midiLearn_.targetForCc(cc);
        if (target < 0 || target >= robodrummer::kLearnTargetCount) continue;
        juce::ValueTree node("Cc");
        node.setProperty("cc", cc, nullptr);
        node.setProperty("target", robodrummer::kLearnTargets[static_cast<std::size_t>(target)].id, nullptr);
        map.appendChild(node, nullptr);
    }
    for (int note = 0; note < 128; ++note) {
        const auto binding = midiLearn_.noteBindingForNote(note);
        if (binding.command < 0) continue;
        juce::ValueTree node("Note");
        node.setProperty("note", note, nullptr);
        node.setProperty("command", binding.command, nullptr);
        node.setProperty("channel", binding.channel, nullptr);
        map.appendChild(node, nullptr);
    }
    return map;
}

void RoboDrummerAudioProcessor::applyMidiMapState(const juce::ValueTree& map) {
    if (!map.isValid()) return;
    midiLearn_.cancelLearn();
    midiLearn_.clearAll();
    midiLearn_.resetNoteDefaults();
    // Rebuild note bindings from scratch so removed defaults stay removed.
    for (int command = 0; command < robodrummer::MidiLearnMap::CommandCount; ++command) midiLearn_.unbindCommand(command);
    for (const auto& node : map) {
        if (node.hasType("Cc")) {
            const auto id = node.getProperty("target").toString();
            for (int t = 0; t < robodrummer::kLearnTargetCount; ++t)
                if (id == robodrummer::kLearnTargets[static_cast<std::size_t>(t)].id) midiLearn_.bindCc(static_cast<int>(node.getProperty("cc")), t);
        } else if (node.hasType("Note")) {
            midiLearn_.bindNote(static_cast<int>(node.getProperty("note")), static_cast<int>(node.getProperty("command")),
                                static_cast<int>(node.getProperty("channel", 16)));
        }
    }
}

void RoboDrummerAudioProcessor::resetMidiMappingsToDefault() {
    midiLearn_.cancelLearn();
    midiLearn_.clearAll();
    midiLearn_.resetNoteDefaults();
    // Expression pedal defaults (SPEC 26): CC11 -> Intensity, CC12 -> Leadership.
    midiLearn_.bindCc(11, 0);
    midiLearn_.bindCc(12, 1);
}

void RoboDrummerAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto transaction = lockNonRealtimeStateChange();
    auto state = parameters_.copyState();
    state.setProperty("stateVersion", StateVersion, nullptr);
    state.setProperty("userKitPath", getUserKitFolder().getFullPathName(), nullptr);
    state.removeChild(state.getChildWithName("Arrangement"), nullptr);
    state.removeChild(state.getChildWithName("MidiMap"), nullptr);
    state.removeChild(state.getChildWithName("DrumMap"), nullptr);
    state.appendChild(createArrangementState(), nullptr);
    state.appendChild(createMidiMapState(), nullptr);
    state.appendChild(createDrumMapState(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

// Sessions saved before 0.4.0 (state versions 1-3) carry none of the parameters added since: they load at their defaults,
// which are all off / neutral, so the restored session sounds exactly as before.
void RoboDrummerAudioProcessor::resetAbsentParametersToDefault(const juce::ValueTree& state) {
    juce::StringArray present;
    for (const auto& child : state)
        if (child.hasType("PARAM")) present.add(child.getProperty("id").toString());
    bool inAppendedRange = false;
    for (auto* parameter : juce::AudioProcessor::getParameters()) {
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        if (withId == nullptr) continue;
        if (withId->paramID == robodrummer::ParamIDs::drumBusEnabled) inAppendedRange = true;
        if (inAppendedRange && !present.contains(withId->paramID))
            parameter->setValueNotifyingHost(parameter->getDefaultValue());
    }
}

void RoboDrummerAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;
    if (!state.hasType(parameters_.state.getType()) && !state.hasType("RoboDrummerState")) return;
    const auto transaction = lockNonRealtimeStateChange();
    invalidateAutoSetup(); // accepted state replaces transient proposals before any parameter changes

    namespace ids = robodrummer::ParamIDs;
    juce::ValueTree arrangementSource;
    if (state.hasType(parameters_.state.getType())) {
        arrangementSource = state.getChildWithName("Arrangement").createCopy();
        const auto midiMap = state.getChildWithName("MidiMap").createCopy();
        const auto drumMap = state.getChildWithName("DrumMap").createCopy();
        auto parameterState = state.createCopy();
        parameterState.removeChild(parameterState.getChildWithName("Arrangement"), nullptr);
        parameterState.removeChild(parameterState.getChildWithName("MidiMap"), nullptr);
        parameterState.removeChild(parameterState.getChildWithName("DrumMap"), nullptr);
        parameters_.replaceState(parameterState);
        resetAbsentParametersToDefault(parameterState);
        if (state.hasProperty("userKitPath"))
            setUserKitFolder(juce::File(state.getProperty("userKitPath").toString()));
        if (midiMap.isValid()) applyMidiMapState(midiMap);
        if (drumMap.isValid()) applyDrumMapState(drumMap);
    } else if (state.hasType("RoboDrummerState")) {
        // Version 1 stored plain properties on the root before host parameters existed.
        setParameterFromState(ids::bpm, static_cast<float>(static_cast<double>(state.getProperty("bpm", 120.0))));
        setParameterFromState(ids::intensity, static_cast<float>(state.getProperty("intensity", 0.5f)));
        setParameterFromState(ids::outputMode, static_cast<float>(juce::jlimit(0, 2, static_cast<int>(state.getProperty("outputMode", 2)))));
        setParameterFromState(ids::dynamicFollow, static_cast<float>(state.getProperty("dynamicFollow", 0.60f)));
        setParameterFromState(ids::jamStyle, static_cast<float>(juce::jlimit(0, 5, static_cast<int>(state.getProperty("jamStyle", 0)))));
        setParameterFromState(ids::arrangementEnabled, static_cast<bool>(state.getProperty("arrangementEnabled", false)) ? 1.0f : 0.0f);
        setParameterFromState(ids::jamMemoryEnabled, static_cast<bool>(state.getProperty("jamMemoryEnabled", true)) ? 1.0f : 0.0f);
        setParameterFromState(ids::manualMeterEnabled, static_cast<bool>(state.getProperty("manualMeterEnabled", false)) ? 1.0f : 0.0f);
        setParameterFromState(ids::meterNumerator, static_cast<float>(juce::jlimit(kMinMeterNumerator, kMaxMeterNumerator,
            static_cast<int>(state.getProperty("manualMeterNumerator", 4))) - kMinMeterNumerator));
        setParameterFromState(ids::meterDenominator, static_cast<float>(denominatorToIndex(static_cast<int>(state.getProperty("manualMeterDenominator", 4)))));
        setParameterFromState(ids::silenceMode, static_cast<float>(juce::jlimit(0, 5, static_cast<int>(state.getProperty("silenceMode", 0)))));
        setParameterFromState(ids::silenceStopBars, static_cast<float>(juce::jlimit(1, 16, static_cast<int>(state.getProperty("silenceStopBars", 2)))));
        setParameterFromState(ids::leadershipMode, static_cast<float>(juce::jlimit(0, 2, static_cast<int>(state.getProperty("leadershipMode", 0)))));
        setParameterFromState(ids::leadership, static_cast<float>(state.getProperty("leadership", 0.5f)));
        setParameterFromState(ids::followRange, static_cast<float>(static_cast<double>(state.getProperty("followRange", 15.0))));
        resetAbsentParametersToDefault(juce::ValueTree());
        arrangementSource = state;
    } else {
        return;
    }
    sanitiseParameters();
    // A practice session is runtime state: it never resumes from a saved session (the settings do).
    for (const char* id : { ids::practiceEnabled, ids::pracLoopOn })
        if (auto* parameter = parameters_.getParameter(id)) parameter->setValueNotifyingHost(0.0f);
    syncAllParametersToAtomics();
    applyArrangementState(arrangementSource);
    requestKitReload();
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withProgramChanged(true));
}

// ---------------------------------------------------------------------------------------------------------------
// Programs (factory presets)
// ---------------------------------------------------------------------------------------------------------------

int RoboDrummerAudioProcessor::getNumPrograms() { return juce::jmax(1, robodrummer::PresetManager::factoryProgramCount()); }
const juce::String RoboDrummerAudioProcessor::getProgramName(int index) { return robodrummer::PresetManager::factoryProgramName(index); }
void RoboDrummerAudioProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= robodrummer::PresetManager::factoryProgramCount()) return;
    currentProgram_ = index;
    robodrummer::PresetManager::loadFactoryProgram(*this, index);
}

// ---------------------------------------------------------------------------------------------------------------
// Setters and getters
// ---------------------------------------------------------------------------------------------------------------

void RoboDrummerAudioProcessor::setInternalBpm(double bpm) noexcept {
    internalBpm_.store(juce::jlimit(20.0, 400.0, std::isfinite(bpm) ? bpm : 120.0), std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::setIntensity(float value) noexcept {
    intensity_.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::setManualMeter(int numerator, int denominator) noexcept {
    robodrummer::MeterSelectionInput input;
    input.manualEnabled = true;
    input.manualNumerator = numerator;
    input.manualDenominator = denominator;
    const auto meter = robodrummer::MeterSelection::resolve(input);
    manualMeterNumerator_.store(meter.numerator, std::memory_order_relaxed);
    manualMeterDenominator_.store(meter.denominator, std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::setArrangementSlot(int index, const robodrummer::ArrangementSlot& slot) noexcept {
    if (index < 0 || index >= ArrangementSlotCount) return;
    auto& s = slots_[static_cast<std::size_t>(index)];
    s.style.store(juce::jlimit(0, robodrummer::kJamStyleCount - 1, static_cast<int>(slot.section.style)), std::memory_order_relaxed);
    s.bars.store(juce::jlimit(1, 64, slot.section.bars), std::memory_order_relaxed);
    s.intensity.store(juce::jlimit(0.0f, 1.0f, slot.section.intensityTarget), std::memory_order_relaxed);
    s.enabled.store(slot.enabled, std::memory_order_relaxed);
    s.autoAdvance.store(slot.section.autoAdvance, std::memory_order_relaxed);
    s.timeFeel.store(juce::jlimit(0, 2, static_cast<int>(slot.section.timeFeel)), std::memory_order_relaxed);
    s.transition.store(juce::jlimit(0, 3, static_cast<int>(slot.section.transition)), std::memory_order_relaxed);
    s.personality.store(juce::jlimit(0, robodrummer::kDrummerPersonalityCount - 1, static_cast<int>(slot.section.personality)), std::memory_order_relaxed);
    s.fillFrequency.store(slot.section.fillFrequency < 0.0f ? -1.0f : juce::jlimit(0.0f, 1.0f, slot.section.fillFrequency), std::memory_order_relaxed);
    s.leadership.store(slot.section.leadership < 0.0f ? -1.0f : juce::jlimit(0.0f, 1.0f, slot.section.leadership), std::memory_order_relaxed);
    s.followStrength.store(slot.section.followStrength < 0.0f ? -1.0f : juce::jlimit(0.0f, 1.0f, slot.section.followStrength), std::memory_order_relaxed);
    arrangementRevision_.fetch_add(1, std::memory_order_release);
}

robodrummer::ArrangementSlot RoboDrummerAudioProcessor::getArrangementSlot(int index) const noexcept {
    robodrummer::ArrangementSlot slot;
    if (index < 0 || index >= ArrangementSlotCount) return slot;
    const auto& s = slots_[static_cast<std::size_t>(index)];
    slot.section.style = static_cast<robodrummer::JamStyle>(s.style.load(std::memory_order_relaxed));
    slot.section.bars = s.bars.load(std::memory_order_relaxed);
    slot.section.intensityTarget = s.intensity.load(std::memory_order_relaxed);
    slot.section.autoAdvance = s.autoAdvance.load(std::memory_order_relaxed);
    slot.section.timeFeel = static_cast<robodrummer::SectionTimeFeel>(s.timeFeel.load(std::memory_order_relaxed));
    slot.section.transition = static_cast<robodrummer::SectionTransition>(s.transition.load(std::memory_order_relaxed));
    slot.section.personality = static_cast<robodrummer::DrummerPersonality>(s.personality.load(std::memory_order_relaxed));
    slot.section.fillFrequency = s.fillFrequency.load(std::memory_order_relaxed);
    slot.section.leadership = s.leadership.load(std::memory_order_relaxed);
    slot.section.followStrength = s.followStrength.load(std::memory_order_relaxed);
    slot.enabled = s.enabled.load(std::memory_order_relaxed);
    return slot;
}

juce::String RoboDrummerAudioProcessor::getArrangementName(int index) const {
    if (index < 0 || index >= ArrangementSlotCount) return {};
    const juce::ScopedLock lock(uiTextLock_);
    return arrangementNames_[static_cast<std::size_t>(index)];
}

void RoboDrummerAudioProcessor::setArrangementName(int index, const juce::String& name) {
    if (index < 0 || index >= ArrangementSlotCount) return;
    const juce::ScopedLock lock(uiTextLock_);
    arrangementNames_[static_cast<std::size_t>(index)] = name.substring(0, 24);
}

robodrummer::HostTransportSnapshot RoboDrummerAudioProcessor::getLastTransport() const noexcept {
    robodrummer::HostTransportSnapshot result;
    result.playing = lastHostPlaying_.load(std::memory_order_relaxed);
    result.bpm = lastHostBpm_.load(std::memory_order_relaxed);
    result.ppqPosition = lastHostPpq_.load(std::memory_order_relaxed);
    result.numerator = lastHostNumerator_.load(std::memory_order_relaxed);
    result.denominator = lastHostDenominator_.load(std::memory_order_relaxed);
    result.validTempo = lastHostTempoValid_.load(std::memory_order_acquire);
    result.validPpq = lastHostPpqValid_.load(std::memory_order_acquire);
    return result;
}

void RoboDrummerAudioProcessor::installStarterArrangement() noexcept {
    struct Starter { robodrummer::JamStyle style; int bars; float intensity; bool autoAdvance; };
    const std::array<Starter, ArrangementSlotCount> defaults {{
        {robodrummer::JamStyle::Rock, 8, 0.50f, true},
        {robodrummer::JamStyle::Funk, 4, 0.66f, true},
        {robodrummer::JamStyle::Rock, 8, 0.78f, true},
        {robodrummer::JamStyle::Blues, 8, 0.48f, true},
        {robodrummer::JamStyle::Punk, 4, 0.82f, true},
        {robodrummer::JamStyle::Rock, 8, 0.60f, false}
    }};
    static const char* names[ArrangementSlotCount] { "Verse", "Pre-chorus", "Chorus", "Bridge", "Final push", "Outro" };

    for (int i = 0; i < ArrangementSlotCount; ++i) {
        robodrummer::ArrangementSlot slot;
        slot.section.style = defaults[static_cast<std::size_t>(i)].style;
        slot.section.bars = defaults[static_cast<std::size_t>(i)].bars;
        slot.section.intensityTarget = defaults[static_cast<std::size_t>(i)].intensity;
        slot.section.autoAdvance = defaults[static_cast<std::size_t>(i)].autoAdvance;
        slot.enabled = true;
        setArrangementSlot(i, slot);
        arrangementNames_[static_cast<std::size_t>(i)] = names[i];
    }
    arrangementRevision_.store(1, std::memory_order_release);
    rebuildArrangementFromSlots();
    appliedArrangementRevision_ = 1;
}

void RoboDrummerAudioProcessor::rebuildArrangementFromSlots() noexcept {
    arrangement_.clear();
    for (int i = 0; i < ArrangementSlotCount; ++i) {
        const auto slot = getArrangementSlot(i);
        if (!slot.enabled) continue;
        if (arrangement_.add(slot.section))
            arrangementSlotForIndex_[arrangement_.size() - 1] = i;
    }
    arrangement_.reset();
    publishArrangementSection();
}

void RoboDrummerAudioProcessor::publishArrangementSection() noexcept {
    // The UI numbers sections by slot, so report the slot that the playing (enabled) section came from.
    const auto index = arrangement_.currentSectionIndex();
    const int slot = arrangement_.empty() || index >= arrangementSlotForIndex_.size() ? 0 : arrangementSlotForIndex_[index];
    currentArrangementSection_.store(slot, std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::applyCurrentArrangementSection() noexcept {
    if (arrangement_.empty()) return;
    const auto& section = arrangement_.current();
    setJamStyle(section.style);
    setIntensity(section.intensityTarget);
    jamStyleNeedsHostSync_.store(true, std::memory_order_release);
    intensityNeedsHostSync_.store(true, std::memory_order_release);
    sectionPersonality_ = section.personality;
    sectionFillFrequency_ = section.fillFrequency;
    sectionLeadership_ = section.leadership;
    sectionFollowStrength_ = section.followStrength;
    jam_.setTimeScale(section.timeFeel == robodrummer::SectionTimeFeel::HalfTime ? 0.5
                      : section.timeFeel == robodrummer::SectionTimeFeel::DoubleTime ? 2.0 : 1.0);
}

// ---------------------------------------------------------------------------------------------------------------
// Telemetry, calibration, tools
// ---------------------------------------------------------------------------------------------------------------

std::size_t RoboDrummerAudioProcessor::popOnsetMarks(robodrummer::AnalysisOnsetMark* out, std::size_t capacity) noexcept {
    const int wanted = static_cast<int>(std::min<std::size_t>(capacity, static_cast<std::size_t>(onsetFifo_.getNumReady())));
    if (wanted <= 0) return 0;
    const auto reader = onsetFifo_.read(wanted);
    std::size_t count = 0;
    for (int i = 0; i < reader.blockSize1; ++i) out[count++] = onsetStorage_[static_cast<std::size_t>(reader.startIndex1 + i)];
    for (int i = 0; i < reader.blockSize2; ++i) out[count++] = onsetStorage_[static_cast<std::size_t>(reader.startIndex2 + i)];
    return count;
}

std::array<robodrummer::TempoHypothesis, 3> RoboDrummerAudioProcessor::getTempoCandidates() const noexcept {
    std::array<robodrummer::TempoHypothesis, 3> result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result[i].bpm = candidateBpm_[i].load(std::memory_order_relaxed);
        result[i].confidence = candidateConfidence_[i].load(std::memory_order_relaxed);
    }
    return result;
}

robodrummer::CalibrationSummary RoboDrummerAudioProcessor::getCalibration() const noexcept {
    robodrummer::CalibrationSummary summary;
    summary.state = calibrationState_.load(std::memory_order_acquire);
    summary.progress = calibrationProgress_.load(std::memory_order_relaxed);
    auto& r = summary.result;
    r.valid = summary.state == 2;
    r.noiseFloorDb = calibrationValues_[0].load(std::memory_order_relaxed);
    r.typicalRmsDb = calibrationValues_[1].load(std::memory_order_relaxed);
    r.loudRmsDb = calibrationValues_[2].load(std::memory_order_relaxed);
    r.peakDb = calibrationValues_[3].load(std::memory_order_relaxed);
    r.dynamicRangeDb = calibrationValues_[4].load(std::memory_order_relaxed);
    r.onsetsPerSecond = calibrationValues_[5].load(std::memory_order_relaxed);
    r.meanOnsetStrength = calibrationValues_[6].load(std::memory_order_relaxed);
    r.suggestedGateDb = calibrationValues_[7].load(std::memory_order_relaxed);
    r.suggestedGainDb = calibrationValues_[8].load(std::memory_order_relaxed);
    return summary;
}

void RoboDrummerAudioProcessor::applyCalibrationSuggestions() {
    const auto summary = getCalibration();
    if (!summary.result.valid) return;
    setParameterFromState(robodrummer::ParamIDs::gateThreshold, summary.result.suggestedGateDb);
    setParameterFromState(robodrummer::ParamIDs::inputGain, summary.result.suggestedGainDb);
}

void RoboDrummerAudioProcessor::tapTempo() {
    const auto bpm = tapTempo_.tap(juce::Time::getMillisecondCounterHiRes() / 1000.0);
    if (bpm) setParameterFromState(robodrummer::ParamIDs::bpm, static_cast<float>(*bpm));
}

void RoboDrummerAudioProcessor::setUserKitFolder(const juce::File& folder) {
    const juce::ScopedLock lock(uiTextLock_);
    userKitFolder_ = folder;
}

juce::File RoboDrummerAudioProcessor::getUserKitFolder() const {
    const juce::ScopedLock lock(uiTextLock_);
    return userKitFolder_;
}

juce::String RoboDrummerAudioProcessor::getKitStatus() const {
    const juce::ScopedLock lock(uiTextLock_);
    return kitStatus_;
}

juce::String RoboDrummerAudioProcessor::getLastMidiText() const {
    switch (lastMidiKind_.load(std::memory_order_relaxed)) {
        case 1: return "CC " + juce::String(lastMidiData1_.load()) + " = " + juce::String(lastMidiData2_.load()) + "  (ch " + juce::String(lastMidiChannel_.load()) + ")";
        case 2: return "Note " + juce::String(lastMidiData1_.load()) + " vel " + juce::String(lastMidiData2_.load()) + "  (ch " + juce::String(lastMidiChannel_.load()) + ")";
        default: return "No MIDI received yet";
    }
}

int RoboDrummerAudioProcessor::midiNoteFor(robodrummer::DrumInstrument i) const noexcept {
    return drumNotes_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::resetMidiDrumMap() noexcept {
    using I = robodrummer::DrumInstrument;
    // General MIDI percussion map.
    const std::array<std::pair<I, int>, 16> gm {{
        {I::Kick, 36}, {I::Snare, 38}, {I::ClosedHat, 42}, {I::OpenHat, 46}, {I::Ride, 51}, {I::Crash, 49},
        {I::HighTom, 50}, {I::MidTom, 47}, {I::FloorTom, 43}, {I::Rimshot, 37}, {I::Sidestick, 37}, {I::China, 52},
        {I::Splash, 55}, {I::Cowbell, 56}, {I::Tambourine, 54}, {I::Clap, 39}
    }};
    for (const auto& [instrument, note] : gm) drumNotes_[static_cast<std::size_t>(instrument)].store(note, std::memory_order_relaxed);
}

juce::ValueTree RoboDrummerAudioProcessor::createDrumMapState() const {
    juce::ValueTree map("DrumMap");
    for (int i = 0; i <= static_cast<int>(robodrummer::DrumInstrument::Clap); ++i)
        map.setProperty(juce::Identifier("n" + juce::String(i)), drumNotes_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed), nullptr);
    return map;
}

void RoboDrummerAudioProcessor::applyDrumMapState(const juce::ValueTree& tree) {
    if (!tree.isValid()) return;
    for (int i = 0; i <= static_cast<int>(robodrummer::DrumInstrument::Clap); ++i) {
        const auto key = juce::Identifier("n" + juce::String(i));
        if (tree.hasProperty(key)) drumNotes_[static_cast<std::size_t>(i)].store(juce::jlimit(0, 127, static_cast<int>(tree.getProperty(key))), std::memory_order_relaxed);
    }
}

robodrummer::PerformanceState RoboDrummerAudioProcessor::getPerformanceState() const noexcept {
    robodrummer::PerformanceState s;
    float* fields[13] { &s.rms, &s.peak, &s.activity, &s.intensity, &s.bandLow, &s.bandMid, &s.bandHigh, &s.spectralFlux,
                        &s.transientDensity, &s.transientStrength, &s.regularity, &s.accentProbability, &s.silenceProbability };
    for (std::size_t i = 0; i < perfValues_.size(); ++i) *fields[i] = perfValues_[i].load(std::memory_order_relaxed);
    return s;
}

std::array<robodrummer::RichTempoHypothesis, 3> RoboDrummerAudioProcessor::getRichHypotheses() const noexcept {
    std::array<robodrummer::RichTempoHypothesis, 3> result{};
    for (std::size_t i = 0; i < 3; ++i) {
        auto& h = result[i];
        h.bpm = richValues_[i * 6 + 0].load(std::memory_order_relaxed);
        h.confidence = richValues_[i * 6 + 1].load(std::memory_order_relaxed);
        h.supportingEvents = static_cast<int>(richValues_[i * 6 + 2].load(std::memory_order_relaxed));
        h.consistency = richValues_[i * 6 + 3].load(std::memory_order_relaxed);
        h.ageSeconds = richValues_[i * 6 + 4].load(std::memory_order_relaxed);
        h.relationToBest = static_cast<robodrummer::HypothesisRelation>(static_cast<int>(richValues_[i * 6 + 5].load(std::memory_order_relaxed)));
    }
    return result;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RoboDrummerAudioProcessor(); }
