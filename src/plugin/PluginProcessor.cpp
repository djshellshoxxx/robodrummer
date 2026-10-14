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
    "Break", "Stop", "Resume", "Reset Listening", "Solo Support", "End Jam", "Immediate Fill"
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
    for (auto& r : retiredPlayers_) r.store(nullptr);
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
    resetMidiDrumMap();
    for (std::size_t i = 0; i < 8; ++i) { rawMixGain_[i] = cache(ids::mixGain[i]); rawMixMute_[i] = cache(ids::mixMute[i]); }
    for (std::size_t i = 0; i < 7; ++i) rawMixPan_[i] = cache(ids::mixPan[i]);
    for (std::size_t i = 0; i < 6; ++i) rawMixSolo_[i] = cache(ids::mixSolo[i]);
    resetMidiMappingsToDefault();
    syncAllParametersToAtomics();
    startTimerHz(30);
}

RoboDrummerAudioProcessor::~RoboDrummerAudioProcessor() {
    stopTimer();
    kitLoader_.removeAllJobs(true, 15000);
    for (const auto* id : kParameterIds)
        parameters_.removeParameterListener(id, this);
    delete player_;
    delete pendingPlayer_.exchange(nullptr);
    for (auto& r : retiredPlayers_) delete r.exchange(nullptr);
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
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ParameterID{ids::inputMode, 1}, "Guitar Input",
        juce::StringArray{"Auto (mono / stereo sum)", "Left only", "Right only"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::inputGain, 1}, "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 36.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(ParameterID{ids::autoGain, 1}, "Auto Gain", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ParameterID{ids::gateThreshold, 1}, "Noise Gate",
        juce::NormalisableRange<float>(-90.0f, -20.0f, 0.5f), -90.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return v <= -80.0f ? juce::String("Off") : juce::String(v, 1) + " dB"; })));
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

    // Kit selection and loading happen here (message thread) and on the loader thread, never on the audio thread.
    const int kitChoice = choiceIndex(rawKit_, robodrummer::kKitChoiceCount);
    const bool reload = kitReloadRequested_.exchange(false, std::memory_order_acq_rel);
    if (sampleRate_ > 0.0 && (kitChoice != lastKitChoice_ || (reload && kitChoice == robodrummer::kUserKitIndex))) {
        lastKitChoice_ = kitChoice;
        loadKitAsync(kitChoice);
    }
    for (auto& slot : retiredPlayers_) delete slot.exchange(nullptr);
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

void RoboDrummerAudioProcessor::loadKitAsync(int kitIndex) {
    const double sr = sampleRate_;
    const auto folder = getUserKitFolder();
    kitLoader_.addJob([this, kitIndex, sr, folder] {
        juce::String report;
        std::unique_ptr<robodrummer::DrumSamplePlayer> built;
        if (kitIndex == robodrummer::kUserKitIndex) {
            built = robodrummer::KitBuilder::loadUserFolder(folder, sr, report);
        } else {
            built = robodrummer::KitBuilder::buildBuiltIn(kitIndex, sr);
            report = robodrummer::kKitNames[static_cast<std::size_t>(juce::jlimit(0, robodrummer::kKitChoiceCount - 1, kitIndex))];
        }
        {
            const juce::ScopedLock lock(uiTextLock_);
            kitStatus_ = report;
        }
        delete pendingPlayer_.exchange(built.release(), std::memory_order_acq_rel);
    });
}

void RoboDrummerAudioProcessor::swapInPendingKit() noexcept {
    if (pendingPlayer_.load(std::memory_order_relaxed) == nullptr) return;
    auto* incoming = pendingPlayer_.exchange(nullptr, std::memory_order_acq_rel);
    if (incoming == nullptr) return;
    // Hand the old sampler back for deletion on the message thread; if every slot is somehow busy keep it alive.
    robodrummer::DrumSamplePlayer* previous = player_;
    player_ = incoming;
    if (previous != nullptr) {
        for (auto& slot : retiredPlayers_) {
            robodrummer::DrumSamplePlayer* empty = nullptr;
            if (slot.compare_exchange_strong(empty, previous, std::memory_order_acq_rel)) { previous = nullptr; break; }
        }
    }
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

void RoboDrummerAudioProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
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
    lastRoomSize_ = -1.0f;
    masterGain_.reset(sampleRate_, 0.02);
    masterGain_.setCurrentAndTargetValue(1.0f);

    // Rebuild the sampler for the new sample rate. Built-in kits are generated synchronously; a user folder
    // starts on the built-in kit and swaps in as soon as the loader thread has read the files.
    kitLoader_.removeAllJobs(true, 15000);
    delete pendingPlayer_.exchange(nullptr);
    for (auto& r : retiredPlayers_) delete r.exchange(nullptr);
    delete player_;
    lastKitChoice_ = choiceIndex(rawKit_, robodrummer::kKitChoiceCount);
    player_ = robodrummer::KitBuilder::buildBuiltIn(lastKitChoice_ == robodrummer::kUserKitIndex ? 0 : lastKitChoice_, sampleRate_).release();
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
    // Output channels that have no matching input (mono or disabled guitar input) contain garbage;
    // this also clears any enabled aux output buses.
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);

    swapInPendingKit();

    // ---- MIDI: clock, MIDI learn, controller mapping and intervention notes -------------------------------------
    // Channel 16 is RoboDrummer's intervention channel; those messages are consumed here and not forwarded, so
    // they cannot trigger a downstream drum instrument. Controllers bound to a parameter are consumed too.
    // Everything else passes through.
    midiOut_.clear();
    std::array<rd::MidiCommand, 24> midiCommands{};
    std::size_t midiCommandCount = 0;
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        const long long absoluteSample = midiSampleCursor_ + metadata.samplePosition;
        if (msg.isMidiClock()) {
            midiClock_.tick(absoluteSample);
            midiOut_.addEvent(msg, metadata.samplePosition);
            continue;
        }
        if (msg.isMidiStart() || msg.isMidiContinue() || msg.isMidiStop()) {
            if (msg.isMidiStart()) midiClock_.start();
            else if (msg.isMidiContinue()) midiClock_.continueClock();
            else midiClock_.stop();
            midiOut_.addEvent(msg, metadata.samplePosition);
            continue;
        }
        const int channel = msg.getChannel();
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
            if (command >= 0 && midiCommandCount < midiCommands.size())
                midiCommands[midiCommandCount++] = static_cast<rd::MidiCommand>(command);
            if (learning || command >= 0) continue;
        } else if (msg.isNoteOff()) {
            if (midiLearn_.commandForNote(msg.getNoteNumber(), channel) >= 0) continue;
        }
        if (channel == rd::MidiLearnMap::DefaultChannel) continue;
        midiOut_.addEvent(msg, metadata.samplePosition);
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
        for (int position = 0; position < numSamples; position += AnalysisChunk) {
            const int n = std::min(AnalysisChunk, numSamples - position);
            conditioner_.process(left + position, right != nullptr ? right + position : nullptr, analysisScratch_.data(), n);
            rhythmAnalyzer_.processBlock(analysisScratch_.data(), n);
            for (std::size_t i = 0; i < rhythmAnalyzer_.lastBlockOnsetCount(); ++i) {
                const auto& onset = rhythmAnalyzer_.lastBlockOnset(i);
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
        while (lastArrangementBarIndex_ < currentBar) {
            const auto step = arrangement_.advanceBar();
            ++lastArrangementBarIndex_;
            if (step.sectionChanged) {
                enterArrangementSection(true);
            } else if (!arrangement_.empty() && arrangement_.current().autoAdvance &&
                       arrangement_.barsIntoSection() == arrangement_.current().bars - 1 &&
                       arrangement_.currentSectionIndex() + 1 < arrangement_.size()) {
                // Lead into the next section: its transition may ask for a fill or a break on this final bar.
                switch (arrangement_.section(arrangement_.currentSectionIndex() + 1).transition) {
                    case robodrummer::SectionTransition::Fill:
                        if (!jam_.state().fillRequested) jam_.requestFill(0.8f);
                        break;
                    case robodrummer::SectionTransition::Break: jam_.apply(robodrummer::MidiCommand::Break); break;
                    default: break;
                }
            }
        }
        if (currentBar < lastArrangementBarIndex_)
            lastArrangementBarIndex_ = currentBar;
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

    jam_.setTempo(authority.outputBpm);
    jam_.setMeter(numerator, denominator);
    jam_.setIntensity(effectiveIntensity);
    const auto phraseState = getPhraseState();
    const float phraseAmount = phraseState == robodrummer::PhraseState::Break ? 1.0f : 0.75f;
    jam_.setStyle(robodrummer::PhraseGrooveModifier::apply(
        robodrummer::styleFor(getJamStyle()), phraseState, phraseAmount));

    if (hasPpq && settings.mode == robodrummer::LeadershipMode::DrummerLeads)
        jam_.syncToPpq(hostPpq);

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
    for (int bit = 0; bit < kCommandCount; ++bit)
        if ((pending & (1u << static_cast<unsigned>(bit))) != 0)
            handleCommand(static_cast<robodrummer::MidiCommand>(bit), arrangementEnabled);
    for (std::size_t i = 0; i < midiCommandCount; ++i)
        handleCommand(midiCommands[i], arrangementEnabled);

    // No new hits while the transport is stopped (Drummer Leads) or before the adaptive modes have
    // joined the guitarist; already-ringing voices still render so tails decay naturally instead of freezing.
    const bool hostStopped = transportStopped && settings.mode == robodrummer::LeadershipMode::DrummerLeads;
    const bool waitingToJoin = settings.mode != robodrummer::LeadershipMode::DrummerLeads && !adaptiveJoined_;
    std::array<robodrummer::DrumEvent, 128> events{};
    const auto eventCount = (hostStopped || waitingToJoin)
        ? std::size_t{0}
        : jam_.processBlock(numSamples, events.data(), events.size());
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
                midiOut_.addEvent(juce::MidiMessage::noteOn(outChannel, note, events[i].velocity), offset);
                midiOut_.addEvent(juce::MidiMessage::noteOff(outChannel, note), juce::jmin(numSamples - 1, offset + 1));
            }
        }
        renderedUntil = std::max(renderedUntil, offset);
    }
    renderSpan(renderedUntil, numSamples - renderedUntil);

    float outPeak = 0.0f;
    if (renderInternal) {
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
    outputPeak_.store(outPeak, std::memory_order_relaxed);
    midi.swapWith(midiOut_);
}

void RoboDrummerAudioProcessor::handleCommand(robodrummer::MidiCommand command, bool arrangementEnabled) noexcept {
    using C = robodrummer::MidiCommand;
    switch (command) {
        case C::SoloSupport: coordinatorSoloCuePending_ = true; return;
        case C::EndJam: coordinatorEndCuePending_ = true; return;
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
        const int command = midiLearn_.commandForNote(note, midiLearn_.channelForNote(note));
        if (command < 0) continue;
        juce::ValueTree node("Note");
        node.setProperty("note", note, nullptr);
        node.setProperty("command", command, nullptr);
        node.setProperty("channel", midiLearn_.channelForNote(note), nullptr);
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

void RoboDrummerAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;

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
        arrangementSource = state;
    } else {
        return;
    }
    syncAllParametersToAtomics();
    applyArrangementState(arrangementSource);
    requestKitReload();
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
