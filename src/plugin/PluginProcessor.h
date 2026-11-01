// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#pragma once
#include <JuceHeader.h>
#include "analysis/AutoSetup.h"
#include "audio/RealtimeOwnerHandoff.h"
#include "audio/KitLoaderLifecycle.h"
#include "core/MidiBlockPolicy.h"
#include "analysis/CalibrationAnalyzer.h"
#include "analysis/InputConditioner.h"
#include "analysis/JamBrain.h"
#include "analysis/JamCoordinator.h"
#include "analysis/MeterDetector.h"
#include "core/PracticeMode.h"
#include "analysis/PerformanceAnalyzer.h"
#include "analysis/PhaseFollower.h"
#include "analysis/RiffLock.h"
#include "analysis/ResyncPlanner.h"
#include "analysis/RhythmAnalyzer.h"
#include "analysis/TimingAuthorityController.h"
#include "analysis/SessionMemory.h"
#include "analysis/SessionMemoryPolicy.h"
#include "analysis/SilenceController.h"
#include "audio/DrumSamplePlayer.h"
#include "audio/KitBuilder.h"
#include "core/DrummerPersonality.h"
#include "dsp/DrumBus.h"
#include "dsp/TransientShaper.h"
#include "analysis/PerformanceAnalyzer.h"
#include "analysis/TempoMovement.h"
#include "core/JamStyleProfile.h"
#include "core/Lookahead.h"
#include "core/MeterSelection.h"
#include "core/MidiCapture.h"
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
#include <filesystem>
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
inline constexpr const char* followStrength = "followStrength";
inline constexpr const char* tempoInertia = "tempoInertia";
inline constexpr const char* beatCorrection = "beatCorrection";
inline constexpr const char* confidenceSensitivity = "confidenceSensitivity";
inline constexpr const char* kitTune = "kitTune";
inline constexpr const char* midiOutChannel = "midiOutChannel";
inline constexpr const char* guitarMonitor = "guitarMonitor";
// Added in 0.4.0 (state version 4); every new feature is off / neutral by default so old sessions sound identical.
inline constexpr const char* drumBusEnabled = "drumBusEnabled";
inline constexpr const char* drumBusThreshold = "drumBusThreshold";
inline constexpr const char* drumBusRatio = "drumBusRatio";
inline constexpr const char* drumBusKnee = "drumBusKnee";
inline constexpr const char* drumBusAttack = "drumBusAttack";
inline constexpr const char* drumBusRelease = "drumBusRelease";
inline constexpr const char* drumBusMakeup = "drumBusMakeup";
inline constexpr const char* drumBusDetect = "drumBusDetect";
inline constexpr const char* drumBusScHpf = "drumBusScHpf";
inline constexpr const char* drumBusMix = "drumBusMix";
inline constexpr const char* drumBusDrive = "drumBusDrive";
inline constexpr const char* transientShaperEnabled = "transientShaperEnabled";
inline constexpr const char* transientShaperAttack = "transientShaperAttack";
inline constexpr const char* transientShaperSustain = "transientShaperSustain";
inline constexpr const char* transientShaperMix = "transientShaperMix";
inline constexpr const char* transientShaperOutput = "transientShaperOutput";
inline constexpr const char* rlAmount = "rlAmount";
inline constexpr const char* rlSensitivity = "rlSensitivity";
inline constexpr const char* rlAccentCrash = "rlAccentCrash";
inline constexpr const char* limbModelAmount = "limbModelAmount";
inline constexpr const char* autoMeterEnabled = "autoMeterEnabled";
inline constexpr const char* autoMeterThreshold = "autoMeterThreshold";
inline constexpr const char* practiceEnabled = "practiceEnabled";
inline constexpr const char* pracCountInLen = "pracCountInLen";
inline constexpr const char* pracCountInSound = "pracCountInSound";
inline constexpr const char* pracCountInStartOnly = "pracCountInStartOnly";
inline constexpr const char* pracLoopOn = "pracLoopOn";
inline constexpr const char* pracLoopMode = "pracLoopMode";
inline constexpr const char* pracLoopBars = "pracLoopBars";
inline constexpr const char* pracLoopSecFirst = "pracLoopSecFirst";
inline constexpr const char* pracLoopSecLast = "pracLoopSecLast";
inline constexpr const char* pracTrainOn = "pracTrainOn";
inline constexpr const char* pracTrainStart = "pracTrainStart";
inline constexpr const char* pracTrainTarget = "pracTrainTarget";
inline constexpr const char* pracTrainStep = "pracTrainStep";
inline constexpr const char* pracTrainEvery = "pracTrainEvery";
inline constexpr const char* pracTrainUnit = "pracTrainUnit";
inline constexpr const char* pracTrainDir = "pracTrainDir";
inline constexpr const char* pracTrainGate = "pracTrainGate";
inline constexpr const char* pracTrainGateMin = "pracTrainGateMin";
inline constexpr const char* pracTrainRest = "pracTrainRest";
inline constexpr const char* pracTrainStopEnd = "pracTrainStopEnd";
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

// Counts from the virtual-limb model's most recent bar ("Limbs: 3 hits removed (2 rate, 1 travel)").
struct LimbSummary {
    int input{0}, kept{0}, removed{0};
    int byReason[4]{ 0, 0, 0, 0 }; // LimbRemoval: Rate, Travel, NoLimb, Evicted
    int nonDefaultLimb{0}, protectedKept{0}, protectedDropped{0};
    bool overflow{false};
    std::uint32_t revision{0};     // increments each time a bar was processed
};

// What the automatic meter detector currently thinks (ANALYSIS tab and the readout under the meter selector).
struct AutoMeterStatus {
    bool enabled{false};
    bool applied{false};      // true while the suggestion is actually driving the drummer's meter
    int numerator{4}, denominator{4}, pulsesPerBar{4};
    float confidence{0.0f};
    bool ternary{false};
};

// Practice mode snapshot for the UI (written by the audio thread, read on the message thread).
struct PracticeStatus {
    bool enabled{false};         // a practice session is running
    int phase{0};                // PracticePhase: 0 idle, 1 waiting for the count-in, 2 count-in clicks, 3 playing
    int countInBeat{0}, countInTotal{0};
    bool loopArmed{false}, loopActive{false};
    int loopPass{0};
    bool trainerOn{false}, trainerFinished{false}, trainerDrives{false};
    double trainerBpm{0.0};      // current rung (drives the tempo only when trainerDrives is true)
    int trainerStep{0};
    bool gateHeld{false};        // last due step was held back by "only speed up when I'm in time"
    bool restBar{false};
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
    friend struct RoboDrummerProcessorTestAccess;
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

    robodrummer::PerformanceState getPerformanceState() const noexcept;
    robodrummer::TempoMovement getTempoMovement() const noexcept { return static_cast<robodrummer::TempoMovement>(tempoMovement_.load(std::memory_order_relaxed)); }
    double getTempoVelocity() const noexcept { return tempoVelocity_.load(std::memory_order_relaxed); }
    std::array<robodrummer::RichTempoHypothesis, 3> getRichHypotheses() const noexcept;
    double getLookaheadMs() const noexcept { return lookaheadMs_.load(std::memory_order_relaxed); }

    // ---- MIDI drum output map (General MIDI by default) ----
    int getMidiDrumNote(robodrummer::DrumInstrument instrument) const noexcept { return drumNotes_[static_cast<std::size_t>(instrument)].load(std::memory_order_relaxed); }
    void setMidiDrumNote(robodrummer::DrumInstrument instrument, int note) noexcept { drumNotes_[static_cast<std::size_t>(instrument)].store(juce::jlimit(0, 127, note), std::memory_order_relaxed); }
    void resetMidiDrumMap() noexcept;
    juce::ValueTree createDrumMapState() const;
    void applyDrumMapState(const juce::ValueTree& tree);

    // ---- Drum Bus / Transient Shaper meters (read from the UI timer) ----
    float getDrumBusGainReductionDb() const noexcept { return drumBus_.gainReductionDb(); }
    float getTransientShaperGainDb() const noexcept { return shaper_.gainDb(); }

    // ---- Riff Lock (ANALYSIS tab): copy of the learned riff, safe from any thread ----
    robodrummer::RiffSnapshot getRiffSnapshot() const noexcept;
    // Last transform on the audio thread: kicks added / removed / accented in the most recent bar it touched.
    int getRiffLastAdded() const noexcept { return riffAdded_.load(std::memory_order_relaxed); }
    int getRiffLastRemoved() const noexcept { return riffRemoved_.load(std::memory_order_relaxed); }

    // ---- MIDI capture and export (message thread only, except where noted) ----
    // The recorder runs on the audio thread all the time. drainCapture() moves recorded hits into history (the 30 Hz
    // timer already does this; call it before reading a summary or exporting). bars: 4 / 8 / 16 / 32, or 0 for everything
    // since the start (capped at 10 minutes). The drum map in the file is the user's MIDI drum map (SETTINGS -> EDIT DRUM MAP).
    void drainCapture();
    void requestCaptureClear();
    robodrummer::CaptureResult buildCaptureFile(int bars, juce::MemoryBlock& midiFile);
    // Same data written to <temp>/RoboDrummer/Capture/<instance>/RoboDrummer_<style>_<bpm>bpm_<N>bars.mid for drag-and-drop;
    // returns an invalid File (and the reason in `result`) when there is nothing to export or the write failed.
    juce::File writeCaptureTempFile(int bars, robodrummer::CaptureResult* result = nullptr);
    // Status line for the Capture panel: "Ready: 8 bars (16 s)", "Nothing captured yet", ...
    juce::String getCaptureStatusText(int bars);
    std::uint32_t getCaptureDroppedCount() const noexcept { return capture_.droppedCount(); }
    // Removes stale drag files (older than 24 h) left by earlier sessions; also runs once at start-up.
    static void sweepStaleCaptureFiles();

    // ---- Auto Setup / "Learn My Playing" (message thread unless noted) ----
    // start -> poll state/progress/prompt on the UI timer -> when Done read getAutoSetupResult() -> applyAutoSetup() -> undoAutoSetup().
    // The processor's own 30 Hz timer finalizes the analysis (state Analyzing -> Done); nothing here touches the audio thread except atomics.
    void startAutoSetup(float seconds = 10.0f);           // ignored while a run is listening; also discards the previous undo
    void cancelAutoSetup();
    robodrummer::AutoSetupState getAutoSetupState() const noexcept { return autoSetup_.state(); }
    float getAutoSetupProgress() const noexcept { return autoSetup_.progress(); }
    robodrummer::AutoSetupPrompt getAutoSetupPrompt() const noexcept { return autoSetup_.prompt(); }
    juce::String getAutoSetupPromptText() const { return robodrummer::autoSetupPromptText(autoSetup_.prompt()); }
    float getAutoSetupInputPeakDb() const noexcept { return autoSetup_.inputPeakDb(); }
    robodrummer::AutoSetupResult getAutoSetupResult() const { return autoSetup_.result(); } // copy; valid once state is Done
    robodrummer::AutoSetupSnapshot getAutoSetupCurrentValues() const;                       // the 11 parameters Auto Setup can change
    bool applyAutoSetup();                                                                  // applies the stored proposal (needs state Done and status Ok)
    bool applyAutoSetup(const robodrummer::AutoSetupProposal& selected);                    // review card: only fields with apply == true
    bool undoAutoSetup();
    std::uint64_t getAutoSetupGeneration() const noexcept { return autoSetup_.generation(); }
    // Recursive gate permits same-thread host callbacks; processBlock never takes it.
    [[nodiscard]] juce::ScopedLock lockNonRealtimeStateChange() const { return juce::ScopedLock(stateChangeLock_); }
    void invalidateAutoSetup() noexcept {
        const auto transaction = lockNonRealtimeStateChange();
        autoSetup_.invalidate();
    }
    bool canUndoAutoSetup() const noexcept {
        const auto generation = autoSetupUndoGeneration_.load(std::memory_order_acquire);
        return generation != 0 && generation == autoSetup_.generation();
    }
    void clearAutoSetupUndo() noexcept {
        const auto transaction = lockNonRealtimeStateChange();
        autoSetupUndoGeneration_.store(0, std::memory_order_release);
        autoSetupUndo_.clear();
    }
    void finalizeAutoSetupIfReady();                                                        // idempotent; the timer calls it

    // ---- Practice mode (count-in, bar loop, tempo trainer) ----
    robodrummer::PracticeStatus getPracticeStatus() const noexcept;
    // "Count-in 3/4", "Loop pass 3", "Trainer 95 BPM", "Trainer: hint only, guitarist leads", or "Practice: off".
    juce::String getPracticeStatusText() const;
    void restartPracticeTrainer() noexcept { requestCommand(robodrummer::MidiCommand::TrainerRestart); }

    // ---- Automatic meter (ANALYSIS tab, under the meter selector) ----
    robodrummer::AutoMeterStatus getAutoMeterStatus() const noexcept;
    juce::String getAutoMeterReadout() const; // "Auto meter: 3/4 (82%)", "Auto meter: --" or "Auto meter: off"

    // ---- Virtual limbs (ANALYSIS tab) ----
    robodrummer::LimbSummary getLimbSummary() const noexcept;

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
    static constexpr int StateVersion = 4;

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void syncAllParametersToAtomics();
    void sanitiseParameters();
    void resetAbsentParametersToDefault(const juce::ValueTree& state);
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
    int midiNoteFor(robodrummer::DrumInstrument) const noexcept;
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
    // Audio owns the borrowed facade; deletion and publication happen off the callback.
    robodrummer::RealtimeOwnerHandoff<robodrummer::DrumSamplePlayer, 4> playerHandoff_{};
    robodrummer::DrumSamplePlayer* player_{nullptr};
    juce::CriticalSection kitSubmissionLock_;
    bool kitSubmissionsEnabled_{true}; // guarded by kitSubmissionLock_, never used in processBlock
    std::atomic<bool> kitLoadCancelled_{false};
    bool midiSafetyStopped_{false}, midiPanicOutputPending_{false}, midiSafetyHostStopSeen_{false};
    void stopKitLoader();
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
    robodrummer::dsp::TransientShaper shaper_;
    robodrummer::dsp::DrumBus drumBus_;
    struct DspParamCache {
        std::atomic<float>* busEnabled{nullptr}; std::atomic<float>* busThreshold{nullptr}; std::atomic<float>* busRatio{nullptr};
        std::atomic<float>* busKnee{nullptr}; std::atomic<float>* busAttack{nullptr}; std::atomic<float>* busRelease{nullptr};
        std::atomic<float>* busMakeup{nullptr}; std::atomic<float>* busDetect{nullptr}; std::atomic<float>* busScHpf{nullptr};
        std::atomic<float>* busMix{nullptr}; std::atomic<float>* busDrive{nullptr};
        std::atomic<float>* shEnabled{nullptr}; std::atomic<float>* shAttack{nullptr}; std::atomic<float>* shSustain{nullptr};
        std::atomic<float>* shMix{nullptr}; std::atomic<float>* shOutput{nullptr};
    } dspParams_;
    // Riff Lock learning (audio thread) and its published snapshot (any thread).
    struct BlockOnset { int offset{0}; float strength{0.0f}; float low{1.0f}; };
    static constexpr std::size_t MaxBlockOnsets = 64;
    std::array<BlockOnset, MaxBlockOnsets> blockOnsets_{};
    std::size_t blockOnsetCount_{0};
    robodrummer::RiffLock riffLock_;
    robodrummer::LowBandMeter lowBand_;
    long long riffBar_{-1};
    std::atomic<float>* rawRlAmount_{nullptr};
    std::atomic<float>* rawRlSensitivity_{nullptr};
    std::atomic<float>* rawRlAccent_{nullptr};
    std::array<std::atomic<float>, robodrummer::RiffGrid::MaxSlots> riffConsistency_{}, riffStrength_{};
    std::atomic<float> riffConfidence_{0.0f}, riffRepetition_{0.0f};
    std::atomic<bool> riffLocked_{false};
    std::atomic<int> riffBarsSeen_{0}, riffSlots_{16}, riffSlotsPerBeat_{4}, riffNumerator_{4}, riffDenominator_{4};
    std::atomic<bool> riffTriplet_{false};
    std::atomic<int> riffAdded_{0}, riffRemoved_{0};
    void feedRiffLock(int numSamples, bool trackerLocked) noexcept;
    robodrummer::MidiCapture<> capture_;
    std::filesystem::path captureDirectory_;
    robodrummer::DrumMap captureDrumMap() const noexcept;
    // Automatic meter (audio thread).
    robodrummer::MeterDetector meterDetector_;
    robodrummer::AutoMeterGate meterGate_;
    long long meterPulseCounter_{0};
    double meterLastPhase_{0.0};
    bool meterPhaseValid_{false};
    bool autoMeterWasEnabled_{false};
    bool autoMeterLastManual_{false};
    bool autoMeterLastStopped_{false};
    std::atomic<float>* rawAutoMeterEnabled_{nullptr};
    std::atomic<float>* rawAutoMeterThreshold_{nullptr};
    std::atomic<bool> autoMeterEnabledVisible_{false}, autoMeterApplied_{false}, autoMeterTernary_{false};
    std::atomic<int> autoMeterNum_{4}, autoMeterDen_{4}, autoMeterPulses_{4};
    std::atomic<float> autoMeterConfidence_{0.0f};
    void resetAutoMeter() noexcept;
    // Practice mode (audio thread unless noted).
    static constexpr std::size_t kPracParamCount = 20;
    std::array<std::atomic<float>*, kPracParamCount> rawPrac_{};
    robodrummer::AutoSetup autoSetup_;
    std::array<float, AnalysisChunk> autoSetupScratch_{};
    mutable juce::CriticalSection stateChangeLock_;
    robodrummer::AutoSetupUndo autoSetupUndo_;
    std::atomic<std::uint64_t> autoSetupUndoGeneration_{0};
    bool writeAutoSetupSnapshot(const robodrummer::AutoSetupSnapshot& values, std::uint32_t mask, std::uint64_t expectedGeneration);
    robodrummer::PracticeMode practice_;
    bool practiceSessionWanted_{false};   // the practiceEnabled parameter as last seen
    bool practiceSuspendedByTransport_{false};
    bool practiceLoopWanted_{false};
    long long practiceTimelineBar_{0};
    int practiceTimelineNumerator_{0};
    bool practiceTimelineValid_{false};
    bool practiceNeedsResume_{false};
    bool practiceMuted_{false};
    bool practiceWasResting_{false};
    bool practiceLoopIsRange_{false};
    long long practiceOffsetSamples_{0};
    robodrummer::LoopRange practiceRange_{};
    std::size_t practiceRangeFirst_{0};
    struct ArrangementSnapshot { bool valid{false}; std::size_t section{0}; int bars{0}; long long bar{0}; };
    ArrangementSnapshot practiceLoopSnap_{}, practiceRestSnap_{};
    robodrummer::PracticeAction practiceLast_{};
    long long practiceAppliedDelta_{0};
    float practiceLastCountIn_{4.0f};     // last non-off count-in length (message thread only)
    std::atomic<std::uint32_t> pendingParamToggles_{0}; // bit n = flip practice parameter n on the message thread
    std::atomic<bool> pracSession_{false}, pracLoopArmed_{false}, pracLoopActive_{false}, pracTrainOn_{false}, pracTrainFinished_{false},
                      pracTrainDrives_{false}, pracGateHeld_{false}, pracRest_{false};
    std::atomic<int> pracPhase_{0}, pracClickBeat_{0}, pracClickTotal_{0}, pracLoopPass_{0}, pracStep_{0};
    std::atomic<double> pracBpm_{0.0};
    void updatePracticeSettings(const robodrummer::TimingAuthoritySettings& settings, bool externalTempo) noexcept;
    std::size_t processJamWithPractice(int numSamples, robodrummer::DrumEvent* out, std::size_t capacity, float trackerConfidence, bool arrangementEnabled) noexcept;
    void restoreArrangementSnapshot(const ArrangementSnapshot& snapshot) noexcept;
    void publishPracticeStatus() noexcept;
    void resetPracticeRuntime() noexcept;
    void drainParameterToggles();
    float pracValue(std::size_t index, float fallback) const noexcept;
    std::atomic<float>* rawLimbAmount_{nullptr};
    std::array<std::atomic<int>, 12> limbValues_{}; // input, kept, removed, 4 reasons, nonDefault, protKept, protDropped, overflow, spare
    std::atomic<std::uint32_t> limbRevisionPublished_{0};
    std::uint32_t limbRevisionSeen_{0};
    void publishRiffSnapshot(const robodrummer::RiffSnapshot& snapshot) noexcept;
    robodrummer::dsp::DrumBusParams readDrumBusParams() const noexcept;
    robodrummer::dsp::TransientShaperParams readShaperParams() const noexcept;
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
    std::array<std::atomic<float>, 13> perfValues_{};
    std::atomic<int> tempoMovement_{0};
    std::atomic<double> tempoVelocity_{0.0};
    std::atomic<double> lookaheadMs_{0.0};
    std::array<std::atomic<float>, 18> richValues_{}; // 3 hypotheses x (bpm, confidence, support, consistency, age, relation)
    std::array<std::atomic<int>, 16> drumNotes_{};
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
    std::atomic<float>* rawFollowStrength_{nullptr};
    std::atomic<float>* rawTempoInertia_{nullptr};
    std::atomic<float>* rawBeatCorrection_{nullptr};
    std::atomic<float>* rawConfidenceSensitivity_{nullptr};
    std::atomic<float>* rawKitTune_{nullptr};
    std::atomic<float>* rawMidiOutChannel_{nullptr};
    std::atomic<float>* rawGuitarMonitor_{nullptr};
    float lastMonitorGain_{0.0f};
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
