// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#pragma once
#include <JuceHeader.h>
#include "analysis/CalibrationAnalyzer.h"
#include "analysis/InputConditioner.h"
#include "analysis/JamBrain.h"
#include "analysis/JamCoordinator.h"
#include "analysis/PerformanceAnalyzer.h"
#include "analysis/PhaseFollower.h"
#include "analysis/ResyncPlanner.h"
#include "analysis/RhythmAnalyzer.h"
#include "analysis/TimingAuthorityController.h"
#include "analysis/SessionMemory.h"
#include "analysis/SessionMemoryPolicy.h"
#include "analysis/SilenceController.h"
#include "audio/DrumSamplePlayer.h"
#include "audio/KitBuilder.h"
#include "core/DrummerPersonality.h"
#include "core/JamStyleProfile.h"
#include "core/MeterSelection.h"
#include "core/MidiClockTracker.h"
#include "core/MidiLearn.h"
#include "core/Mixer.h"
#include "core/OutputMode.h"
#include "core/SectionSequencer.h"
#include "core/TapTempo.h"
#include "plugin/HostTransportAdapter.h"
#include "plugin/JamEngine.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace robodrummer::ParamIDs {
// Stable host parameter IDs. Never reuse an ID for a different meaning once released.
inline constexpr const char* bpm = "bpm";
inline constexpr const char* intensity = "intensity";
inline constexpr const char* outputMode = "outputMode";
inline constexpr const char* leadershipMode = "leadershipMode";
inline constexpr const char* leadership = "leadership";
inline constexpr const char* followRange = "followRange";
inline constexpr const char* dynamicFollow = "dynamicFollow";
inline constexpr const char* jamStyle = "jamStyle";
inline constexpr const char* arrangementEnabled = "arrangementEnabled";
inline constexpr const char* jamMemoryEnabled = "jamMemoryEnabled";
inline constexpr const char* manualMeterEnabled = "manualMeterEnabled";
inline constexpr const char* meterNumerator = "meterNumerator";
inline constexpr const char* meterDenominator = "meterDenominator";
inline constexpr const char* silenceMode = "silenceMode";
inline constexpr const char* silenceStopBars = "silenceStopBars";
// Added after the first host-parameter release (all appended; existing IDs are unchanged).
inline constexpr const char* tempoSource = "tempoSource";
inline constexpr const char* followResponse = "followResponse";
inline constexpr const char* humanize = "humanize";
inline constexpr const char* feel = "feel";
inline constexpr const char* complexity = "complexity";
inline constexpr const char* fillFrequency = "fillFrequency";
inline constexpr const char* fillLength = "fillLength";
inline constexpr const char* fillComplexity = "fillComplexity";
inline constexpr const char* fillSurprise = "fillSurprise";
inline constexpr const char* drummer = "drummer";
inline constexpr const char* crashTendency = "crashTendency";
inline constexpr const char* inputMode = "inputMode";
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* autoGain = "autoGain";
inline constexpr const char* gateThreshold = "gateThreshold";
inline constexpr const char* kit = "kit";
inline constexpr const char* roomSize = "roomSize";
inline constexpr std::array<const char*, 8> mixGain { "gain_kick", "gain_snare", "gain_toms", "gain_hihat", "gain_ride", "gain_cymbals", "gain_room", "gain_master" };
inline constexpr std::array<const char*, 7> mixPan { "pan_kick", "pan_snare", "pan_toms", "pan_hihat", "pan_ride", "pan_cymbals", "pan_room" };
inline constexpr std::array<const char*, 8> mixMute { "mute_kick", "mute_snare", "mute_toms", "mute_hihat", "mute_ride", "mute_cymbals", "mute_room", "mute_master" };
inline constexpr std::array<const char*, 6> mixSolo { "solo_kick", "solo_snare", "solo_toms", "solo_hihat", "solo_ride", "solo_cymbals" };
}

namespace robodrummer {

// One programmed-arrangement slot as the UI edits it.
struct ArrangementSlot {
    SectionDefinition section{};
    bool enabled{true};
};

struct AnalysisOnsetMark {
    long long sample{0};
    float strength{0.0f};
};

struct CalibrationSummary {
    int state{0}; // 0 idle, 1 running, 2 complete
    float progress{0.0f};
    CalibrationResult result{};
};

struct LearnTarget {
    const char* id;
    const char* label;
};

inline constexpr int kLearnTargetCount = 34;
extern const std::array<LearnTarget, kLearnTargetCount> kLearnTargets;
extern const std::array<const char*, MidiLearnMap::CommandCount> kCommandNames;

} // namespace robodrummer

class RoboDrummerAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AudioProcessorValueTreeState::Listener,
                                        private juce::Timer {
public:
    RoboDrummerAudioProcessor();
    ~RoboDrummerAudioProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters_; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    // Factory presets are exposed as host programs; the full preset browser lives in the editor.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void setInternalBpm(double bpm) noexcept;
    double getInternalBpm() const noexcept { return internalBpm_.load(std::memory_order_relaxed); }
    void setIntensity(float value) noexcept;
    float getIntensity() const noexcept { return intensity_.load(std::memory_order_relaxed); }
    void setOutputMode(robodrummer::OutputMode mode) noexcept { outputMode_.store(static_cast<int>(mode), std::memory_order_relaxed); }
    robodrummer::OutputMode getOutputMode() const noexcept { return static_cast<robodrummer::OutputMode>(outputMode_.load(std::memory_order_relaxed)); }

    void setLeadershipMode(robodrummer::LeadershipMode mode) noexcept { leadershipMode_.store(static_cast<int>(mode), std::memory_order_relaxed); }
    robodrummer::LeadershipMode getLeadershipMode() const noexcept { return static_cast<robodrummer::LeadershipMode>(leadershipMode_.load(std::memory_order_relaxed)); }
    void setLeadership(float value) noexcept { leadership_.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed); }
    float getLeadership() const noexcept { return leadership_.load(std::memory_order_relaxed); }
    void setFollowRange(double bpm) noexcept { followRangeBpm_.store(juce::jlimit(0.0, 80.0, bpm), std::memory_order_relaxed); }
    double getFollowRange() const noexcept { return followRangeBpm_.load(std::memory_order_relaxed); }
    void setDynamicFollow(float value) noexcept { dynamicFollow_.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed); }
    float getDynamicFollow() const noexcept { return dynamicFollow_.load(std::memory_order_relaxed); }
    float getDetectedGuitarIntensity() const noexcept { return guitarIntensity_.load(std::memory_order_relaxed); }
    float getEffectiveDrummerIntensity() const noexcept { return effectiveDrummerIntensity_.load(std::memory_order_relaxed); }
    double getEffectiveDrummerBpm() const noexcept { return effectiveDrummerBpm_.load(std::memory_order_relaxed); }
    float getEffectiveGuitarAuthority() const noexcept { return effectiveGuitarAuthority_.load(std::memory_order_relaxed); }
    double getPhaseErrorCycles() const noexcept { return phaseErrorCycles_.load(std::memory_order_relaxed); }
    bool isHardResyncRecommended() const noexcept { return hardResyncRecommended_.load(std::memory_order_relaxed); }
    bool hasAdaptiveJoined() const noexcept { return adaptiveJoinedVisible_.load(std::memory_order_relaxed); }
    int getDrummerBeatInBar() const noexcept { return drummerBeatInBar_.load(std::memory_order_relaxed); }
    long long getDrummerBarNumber() const noexcept { return drummerBarNumber_.load(std::memory_order_relaxed); }
    int getGuitarBeatInBar() const noexcept { return guitarBeatInBar_.load(std::memory_order_relaxed); }
    float getGuitarDownbeatConfidence() const noexcept { return guitarDownbeatConfidence_.load(std::memory_order_relaxed); }
    robodrummer::PhraseState getPhraseState() const noexcept { return static_cast<robodrummer::PhraseState>(phraseState_.load(std::memory_order_relaxed)); }
    float getPhraseFillStrength() const noexcept { return phraseFillStrength_.load(std::memory_order_relaxed); }
    bool isPhraseBoundary() const noexcept { return phraseBoundary_.load(std::memory_order_relaxed); }
    void setJamStyle(robodrummer::JamStyle style) noexcept { jamStyle_.store(static_cast<int>(style), std::memory_order_relaxed); }
    robodrummer::JamStyle getJamStyle() const noexcept { return static_cast<robodrummer::JamStyle>(jamStyle_.load(std::memory_order_relaxed)); }
    void setArrangementEnabled(bool enabled) noexcept { arrangementEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isArrangementEnabled() const noexcept { return arrangementEnabled_.load(std::memory_order_relaxed); }
    int getCurrentArrangementSection() const noexcept { return currentArrangementSection_.load(std::memory_order_relaxed); }
    static constexpr int ArrangementSlotCount = 6;
    void setArrangementSlot(int index, const robodrummer::ArrangementSlot& slot) noexcept;
    robodrummer::ArrangementSlot getArrangementSlot(int index) const noexcept;
    juce::String getArrangementName(int index) const;
    void setArrangementName(int index, const juce::String& name);

    void setManualMeterEnabled(bool enabled) noexcept { manualMeterEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isManualMeterEnabled() const noexcept { return manualMeterEnabled_.load(std::memory_order_relaxed); }
    void setManualMeter(int numerator, int denominator) noexcept;
    int getManualMeterNumerator() const noexcept { return manualMeterNumerator_.load(std::memory_order_relaxed); }
    int getManualMeterDenominator() const noexcept { return manualMeterDenominator_.load(std::memory_order_relaxed); }
    int getEffectiveMeterNumerator() const noexcept { return effectiveMeterNumerator_.load(std::memory_order_relaxed); }
    int getEffectiveMeterDenominator() const noexcept { return effectiveMeterDenominator_.load(std::memory_order_relaxed); }

    void setJamMemoryEnabled(bool enabled) noexcept { jamMemoryEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isJamMemoryEnabled() const noexcept { return jamMemoryEnabled_.load(std::memory_order_relaxed); }
    int getJamMemoryBars() const noexcept { return jamMemoryBars_.load(std::memory_order_relaxed); }
    float getJamMemoryConfidence() const noexcept { return jamMemoryConfidence_.load(std::memory_order_relaxed); }
    double getJamMemoryAverageTempo() const noexcept { return jamMemoryAverageTempo_.load(std::memory_order_relaxed); }
    float getJamMemoryAveragePhraseBars() const noexcept { return jamMemoryAveragePhraseBars_.load(std::memory_order_relaxed); }
    float getJamMemoryFillBias() const noexcept { return jamMemoryFillBias_.load(std::memory_order_relaxed); }
    float getJamMemoryDynamicSensitivity() const noexcept { return jamMemoryDynamicSensitivity_.load(std::memory_order_relaxed); }

    void setSilenceMode(robodrummer::SilenceMode mode) noexcept { silenceMode_.store(static_cast<int>(mode), std::memory_order_relaxed); }
    robodrummer::SilenceMode getSilenceMode() const noexcept { return static_cast<robodrummer::SilenceMode>(silenceMode_.load(std::memory_order_relaxed)); }
    void setSilenceStopBars(int bars) noexcept { silenceStopBars_.store(juce::jlimit(1, 16, bars), std::memory_order_relaxed); }
    int getSilenceStopBars() const noexcept { return silenceStopBars_.load(std::memory_order_relaxed); }
    int getSilentBars() const noexcept { return silentBarsVisible_.load(std::memory_order_relaxed); }
    bool isWaitingForGuitarResume() const noexcept { return waitingForResume_.load(std::memory_order_relaxed); }

    robodrummer::JamCoordinationState getJamCoordinationState() const noexcept {
        return static_cast<robodrummer::JamCoordinationState>(jamCoordinationState_.load(std::memory_order_relaxed));
    }
    float getTransitionProbability() const noexcept { return transitionProbability_.load(std::memory_order_relaxed); }
    float getEndingProbability() const noexcept { return endingProbability_.load(std::memory_order_relaxed); }
    bool isCoordinatorSuppressingBusyFills() const noexcept { return coordinatorSuppressBusyFillsVisible_.load(std::memory_order_relaxed); }

    robodrummer::HostTransportSnapshot getLastTransport() const noexcept;
    double getDetectedGuitarBpm() const noexcept { return detectedGuitarBpm_.load(std::memory_order_relaxed); }
    float getGuitarTempoConfidence() const noexcept { return guitarTempoConfidence_.load(std::memory_order_relaxed); }
    float getGuitarBeatConfidence() const noexcept { return guitarBeatConfidence_.load(std::memory_order_relaxed); }
    double getGuitarBeatPhase() const noexcept { return guitarBeatPhase_.load(std::memory_order_relaxed); }
    double getPredictedNextGuitarBeatSeconds() const noexcept { return predictedNextGuitarBeatSeconds_.load(std::memory_order_relaxed); }
    bool isGuitarTrackerLocked() const noexcept { return guitarTrackerLocked_.load(std::memory_order_acquire); }
    // Queues a performance command from the UI; it is executed on the audio thread exactly like
    // the matching channel-16 MIDI intervention note.
    void requestCommand(robodrummer::MidiCommand command) noexcept {
        pendingUiCommands_.fetch_or(1u << static_cast<unsigned>(command), std::memory_order_release);
    }
    void requestFill() noexcept { requestCommand(robodrummer::MidiCommand::Fill); }
    void resetJamPhase() noexcept { requestCommand(robodrummer::MidiCommand::ResetListening); }
    bool isDrummerStopped() const noexcept { return drummerStopped_.load(std::memory_order_relaxed); }
    double getTimeScale() const noexcept { return timeScale_.load(std::memory_order_relaxed); }
    std::uint32_t getArrangementRevision() const noexcept { return arrangementRevision_.load(std::memory_order_acquire); }

    // ---- input analysis, telemetry and calibration ----
    float getInputPeak() const noexcept { return inputPeak_.load(std::memory_order_relaxed); }
    float getInputRms() const noexcept { return inputRms_.load(std::memory_order_relaxed); }
    bool isGateOpen() const noexcept { return gateOpen_.load(std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak_.load(std::memory_order_relaxed); }
    long long getAnalysisSampleCursor() const noexcept { return analysisCursor_.load(std::memory_order_relaxed); }
    double getSampleRateForDisplay() const noexcept { return sampleRate_; }
    // Pops onset marks produced since the last call (UI thread).
    std::size_t popOnsetMarks(robodrummer::AnalysisOnsetMark* out, std::size_t capacity) noexcept;
    std::array<robodrummer::TempoHypothesis, 3> getTempoCandidates() const noexcept;
    void startCalibration() noexcept { calibrationCommand_.store(1, std::memory_order_release); }
    void cancelCalibration() noexcept { calibrationCommand_.store(2, std::memory_order_release); }
    robodrummer::CalibrationSummary getCalibration() const noexcept;
    void applyCalibrationSuggestions();

    // ---- tempo tools ----
    void tapTempo();
    bool isMidiClockActive() const noexcept { return midiClockActive_.load(std::memory_order_relaxed); }
    double getMidiClockBpm() const noexcept { return midiClockBpm_.load(std::memory_order_relaxed); }

    // ---- kit management ----
    void requestKitReload() noexcept { kitReloadRequested_.store(true, std::memory_order_release); }
    void setUserKitFolder(const juce::File& folder);
    juce::File getUserKitFolder() const;
    juce::String getKitStatus() const;

    // ---- MIDI learn ----
    robodrummer::MidiLearnMap& getMidiLearn() noexcept { return midiLearn_; }
    juce::String getLastMidiText() const;
    void resetMidiMappingsToDefault();

    // ---- state helpers shared with the preset manager ----
    juce::ValueTree createArrangementState() const;
    void applyArrangementState(const juce::ValueTree& tree);
    juce::ValueTree createMidiMapState() const;
    void applyMidiMapState(const juce::ValueTree& tree);
    void setCurrentProgramName(const juce::String& name) { currentProgram_ = -1; (void) name; }

private:
    static constexpr int StateVersion = 3;

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void syncAllParametersToAtomics();
    void setParameterFromState(const char* id, float plainValue);
    void handleCommand(robodrummer::MidiCommand command, bool arrangementEnabled) noexcept;
    void resetListeningState() noexcept;
    void installStarterArrangement() noexcept;
    void applyCurrentArrangementSection() noexcept;
    void enterArrangementSection(bool fromAutoAdvance) noexcept;
    void rebuildArrangementFromSlots() noexcept;
    void publishArrangementSection() noexcept;
    void loadKitAsync(int kitIndex);
    void swapInPendingKit() noexcept;
    void applyMixerToSampler() noexcept;
    void drainMidiLearnToParameters();
    static int midiNoteFor(robodrummer::DrumInstrument) noexcept;
    float rawValue(const char* id, float fallback) const noexcept;

    struct SlotAtomics {
        std::atomic<int> style{0}, bars{4}, timeFeel{0}, transition{0}, personality{0};
        std::atomic<float> intensity{0.5f}, fillFrequency{-1.0f}, leadership{-1.0f}, followStrength{-1.0f};
        std::atomic<bool> enabled{true}, autoAdvance{true};
    };

    juce::AudioProcessorValueTreeState parameters_;
    juce::MidiBuffer midiOut_;
    robodrummer::JamEngine jam_{};
    robodrummer::InputConditioner conditioner_{};
    robodrummer::CalibrationAnalyzer calibration_{};
    robodrummer::LiveRhythmAnalyzer rhythmAnalyzer_{};
    robodrummer::PerformanceAnalyzer performanceAnalyzer_{};
    robodrummer::TimingAuthorityController timingAuthority_{};
    robodrummer::PhaseFollower phaseFollower_{};
    robodrummer::ResyncPlanner resyncPlanner_{};
    robodrummer::JamBrain jamBrain_{};
    robodrummer::JamCoordinator jamCoordinator_{};
    robodrummer::SessionMemory sessionMemory_{};
    robodrummer::SilenceController silenceController_{};
    robodrummer::SectionSequencer<16> arrangement_{};
    robodrummer::MidiClockTracker midiClock_{};
    robodrummer::MidiLearnMap midiLearn_{};
    robodrummer::TapTempo tapTempo_{};
    std::array<int, ArrangementSlotCount> arrangementSlotForIndex_{};
    std::array<juce::String, ArrangementSlotCount> arrangementNames_{};
    // Active sampler is only touched by the audio thread; replacements arrive through pendingPlayer_ and the
    // old one is handed back through retiredPlayer_ for the message thread to delete.
    robodrummer::DrumSamplePlayer* player_{nullptr};
    std::atomic<robodrummer::DrumSamplePlayer*> pendingPlayer_{nullptr};
    std::array<std::atomic<robodrummer::DrumSamplePlayer*>, 4> retiredPlayers_{};
    double sampleRate_{48000.0};
    bool adaptiveJoined_{false};
    int lastAudioMode_{static_cast<int>(robodrummer::LeadershipMode::DrummerLeads)};
    double jamBrainCooldownSeconds_{0.0};
    bool lastArrangementEnabled_{false};
    long long lastArrangementBarIndex_{0};
    std::uint32_t appliedArrangementRevision_{0};
    bool manualFillSinceMemoryBar_{false};
    float silenceIntensityMultiplierAudio_{1.0f};
    bool silenceHoldGrooveAudio_{false};
    long long lastSilenceBarIndex_{-1};
    long long lastCoordinatorBarIndex_{-1};
    bool coordinatorNextCuePending_{false};
    bool coordinatorSoloCuePending_{false};
    bool coordinatorEndCuePending_{false};
    bool coordinatorProgrammedBoundaryPending_{false};
    float coordinatorIntensityBiasAudio_{0.0f};
    bool coordinatorSuppressBusyFillsAudio_{false};
    // Active programmed-section overrides (audio thread only).
    robodrummer::DrummerPersonality sectionPersonality_{robodrummer::DrummerPersonality::Balanced};
    float sectionFillFrequency_{-1.0f};
    float sectionLeadership_{-1.0f};
    float sectionFollowStrength_{-1.0f};
    long long midiSampleCursor_{0};
    int lastKitChoice_{-1};
    int lastPlayedSectionBars_{-1};
    bool calibrating_{false};
    float lastRoomSize_{-1.0f};

    // Output stage (preallocated in prepareToPlay).
    static constexpr int MaxScratchSamples = 32768;
    static constexpr int AnalysisChunk = 4096;
    std::vector<float> drumL_, drumR_, roomL_, roomR_;
    std::array<float, AnalysisChunk> analysisScratch_{};
    juce::Reverb reverb_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain_;

    std::atomic<double> internalBpm_{120.0};
    std::atomic<float> intensity_{0.5f};
    std::atomic<int> outputMode_{static_cast<int>(robodrummer::OutputMode::InternalAndMidi)};
    std::atomic<float> dynamicFollow_{0.60f};
    std::atomic<int> jamStyle_{static_cast<int>(robodrummer::JamStyle::Rock)};
    std::atomic<bool> arrangementEnabled_{false};
    std::atomic<bool> manualMeterEnabled_{false};
    std::atomic<int> manualMeterNumerator_{4};
    std::atomic<int> manualMeterDenominator_{4};
    std::atomic<int> effectiveMeterNumerator_{4};
    std::atomic<int> effectiveMeterDenominator_{4};
    std::atomic<bool> jamMemoryEnabled_{true};
    std::atomic<int> silenceMode_{static_cast<int>(robodrummer::SilenceMode::KeepPlaying)};
    std::atomic<int> silenceStopBars_{2};
    std::atomic<int> silentBarsVisible_{0};
    std::atomic<bool> waitingForResume_{false};
    std::atomic<int> jamCoordinationState_{static_cast<int>(robodrummer::JamCoordinationState::EstablishingGroove)};
    std::atomic<float> transitionProbability_{0.0f};
    std::atomic<float> endingProbability_{0.0f};
    std::atomic<bool> coordinatorSuppressBusyFillsVisible_{false};
    std::atomic<int> currentArrangementSection_{0};
    std::array<SlotAtomics, ArrangementSlotCount> slots_{};
    std::atomic<std::uint32_t> arrangementRevision_{0};
    std::atomic<int> leadershipMode_{static_cast<int>(robodrummer::LeadershipMode::DrummerLeads)};
    std::atomic<float> leadership_{0.5f};
    std::atomic<double> followRangeBpm_{15.0};
    std::atomic<double> effectiveDrummerBpm_{120.0};
    std::atomic<float> effectiveDrummerIntensity_{0.5f};
    std::atomic<float> effectiveGuitarAuthority_{0.0f};
    std::atomic<float> guitarIntensity_{0.0f};
    std::atomic<double> phaseErrorCycles_{0.0};
    std::atomic<bool> hardResyncRecommended_{false};
    std::atomic<bool> adaptiveJoinedVisible_{false};
    std::atomic<std::uint32_t> pendingUiCommands_{0};
    // Set by the audio thread when MIDI/arrangement changes a host-visible value; the message-thread
    // timer then publishes the new value to the host parameter.
    std::atomic<bool> intensityNeedsHostSync_{false};
    std::atomic<bool> jamStyleNeedsHostSync_{false};
    std::atomic<bool> drummerStopped_{false};
    std::atomic<double> timeScale_{1.0};
    std::atomic<double> lastHostBpm_{120.0};
    std::atomic<double> lastHostPpq_{0.0};
    std::atomic<int> lastHostNumerator_{4};
    std::atomic<int> lastHostDenominator_{4};
    std::atomic<bool> lastHostPlaying_{false};
    std::atomic<bool> lastHostTempoValid_{false};
    std::atomic<bool> lastHostPpqValid_{false};
    std::atomic<double> detectedGuitarBpm_{120.0};
    std::atomic<float> guitarTempoConfidence_{0.0f};
    std::atomic<float> guitarBeatConfidence_{0.0f};
    std::atomic<double> guitarBeatPhase_{0.0};
    std::atomic<double> predictedNextGuitarBeatSeconds_{0.0};
    std::atomic<int> guitarBeatInBar_{1};
    std::atomic<int> drummerBeatInBar_{1};
    std::atomic<long long> drummerBarNumber_{1};
    std::atomic<float> guitarDownbeatConfidence_{0.0f};
    std::atomic<int> phraseState_{static_cast<int>(robodrummer::PhraseState::Stable)};
    std::atomic<float> phraseFillStrength_{0.0f};
    std::atomic<bool> phraseBoundary_{false};
    std::atomic<bool> guitarTrackerLocked_{false};
    std::atomic<int> jamMemoryBars_{0};
    std::atomic<float> jamMemoryConfidence_{0.0f};
    std::atomic<double> jamMemoryAverageTempo_{0.0};
    std::atomic<float> jamMemoryAveragePhraseBars_{0.0f};
    std::atomic<float> jamMemoryFillBias_{0.0f};
    std::atomic<float> jamMemoryDynamicSensitivity_{1.0f};
    // telemetry
    std::atomic<float> inputPeak_{0.0f}, inputRms_{0.0f}, outputPeak_{0.0f};
    std::atomic<bool> gateOpen_{true};
    std::atomic<long long> analysisCursor_{0};
    std::array<std::atomic<double>, 3> candidateBpm_{};
    std::array<std::atomic<float>, 3> candidateConfidence_{};
    static constexpr int OnsetFifoSize = 512;
    juce::AbstractFifo onsetFifo_{OnsetFifoSize};
    std::array<robodrummer::AnalysisOnsetMark, OnsetFifoSize> onsetStorage_{};
    int candidateCountdown_{0};
    std::atomic<int> calibrationCommand_{0};
    std::atomic<int> calibrationState_{0};
    std::atomic<float> calibrationProgress_{0.0f};
    std::array<std::atomic<float>, 9> calibrationValues_{};
    // tempo source / MIDI clock
    std::atomic<bool> midiClockActive_{false};
    std::atomic<double> midiClockBpm_{0.0};
    // MIDI learn plumbing
    std::array<std::atomic<float>, robodrummer::kLearnTargetCount> pendingCc_{};
    std::atomic<int> lastMidiKind_{0}; // 0 none, 1 cc, 2 note, 3 clock
    std::atomic<int> lastMidiData1_{0}, lastMidiData2_{0}, lastMidiChannel_{0};
    // parameter cache for values read on the audio thread
    std::atomic<float>* rawTempoSource_{nullptr};
    std::atomic<float>* rawFollowResponse_{nullptr};
    std::atomic<float>* rawHumanize_{nullptr};
    std::atomic<float>* rawFeel_{nullptr};
    std::atomic<float>* rawComplexity_{nullptr};
    std::atomic<float>* rawFillFrequency_{nullptr};
    std::atomic<float>* rawFillLength_{nullptr};
    std::atomic<float>* rawFillComplexity_{nullptr};
    std::atomic<float>* rawFillSurprise_{nullptr};
    std::atomic<float>* rawDrummer_{nullptr};
    std::atomic<float>* rawCrash_{nullptr};
    std::atomic<float>* rawInputMode_{nullptr};
    std::atomic<float>* rawInputGain_{nullptr};
    std::atomic<float>* rawAutoGain_{nullptr};
    std::atomic<float>* rawGate_{nullptr};
    std::atomic<float>* rawKit_{nullptr};
    std::atomic<float>* rawRoomSize_{nullptr};
    std::array<std::atomic<float>*, 8> rawMixGain_{};
    std::array<std::atomic<float>*, 7> rawMixPan_{};
    std::array<std::atomic<float>*, 8> rawMixMute_{};
    std::array<std::atomic<float>*, 6> rawMixSolo_{};
    // kit hand-off between the loader thread and the audio thread
    std::atomic<bool> kitReloadRequested_{false};
    juce::ThreadPool kitLoader_{1};
    mutable juce::CriticalSection uiTextLock_;
    juce::String kitStatus_{"Built-in Studio kit"};
    juce::File userKitFolder_;
    int currentProgram_{0};
    bool restoringState_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessor)
};
