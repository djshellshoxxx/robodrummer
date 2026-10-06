#pragma once
#include <JuceHeader.h>
#include "analysis/JamBrain.h"
#include "analysis/PerformanceAnalyzer.h"
#include "analysis/PhaseFollower.h"
#include "analysis/ResyncPlanner.h"
#include "analysis/RhythmAnalyzer.h"
#include "analysis/TimingAuthorityController.h"
#include "core/JamStyleProfile.h"
#include "core/SectionSequencer.h"
#include "audio/DrumSamplePlayer.h"
#include "plugin/HostTransportAdapter.h"
#include "plugin/JamEngine.h"
#include <array>
#include <atomic>
#include <cstdint>

class RoboDrummerAudioProcessor final : public juce::AudioProcessor {
public:
    RoboDrummerAudioProcessor();
    ~RoboDrummerAudioProcessor() override = default;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void setInternalBpm(double bpm) noexcept;
    double getInternalBpm() const noexcept { return internalBpm_.load(std::memory_order_relaxed); }
    void setIntensity(float value) noexcept;
    float getIntensity() const noexcept { return intensity_.load(std::memory_order_relaxed); }

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
    void setArrangementSection(int index, robodrummer::JamStyle style, int bars, float intensity, bool enabled, bool autoAdvance) noexcept;
    robodrummer::SectionDefinition getArrangementSection(int index) const noexcept;
    bool isArrangementSectionEnabled(int index) const noexcept;

    robodrummer::HostTransportSnapshot getLastTransport() const noexcept;
    double getDetectedGuitarBpm() const noexcept { return detectedGuitarBpm_.load(std::memory_order_relaxed); }
    float getGuitarTempoConfidence() const noexcept { return guitarTempoConfidence_.load(std::memory_order_relaxed); }
    float getGuitarBeatConfidence() const noexcept { return guitarBeatConfidence_.load(std::memory_order_relaxed); }
    double getGuitarBeatPhase() const noexcept { return guitarBeatPhase_.load(std::memory_order_relaxed); }
    double getPredictedNextGuitarBeatSeconds() const noexcept { return predictedNextGuitarBeatSeconds_.load(std::memory_order_relaxed); }
    bool isGuitarTrackerLocked() const noexcept { return guitarTrackerLocked_.load(std::memory_order_acquire); }
    void requestFill() noexcept { pendingUiCommands_.fetch_or(FillBit, std::memory_order_release); }
    void resetJamPhase() noexcept { pendingUiCommands_.fetch_or(ResetBit, std::memory_order_release); }

private:
    enum PendingUiBits : std::uint32_t { FillBit = 1u << 0, ResetBit = 1u << 1 };

    void installStarterKit(double sampleRate);
    void installStarterArrangement() noexcept;
    void applyCurrentArrangementSection() noexcept;
    void rebuildArrangementFromSlots() noexcept;
    static int midiNoteFor(robodrummer::DrumInstrument) noexcept;

    robodrummer::JamEngine jam_{};
    robodrummer::LiveRhythmAnalyzer rhythmAnalyzer_{};
    robodrummer::PerformanceAnalyzer performanceAnalyzer_{};
    robodrummer::TimingAuthorityController timingAuthority_{};
    robodrummer::PhaseFollower phaseFollower_{};
    robodrummer::ResyncPlanner resyncPlanner_{};
    robodrummer::JamBrain jamBrain_{};
    robodrummer::SectionSequencer<16> arrangement_{};
    robodrummer::DrumSamplePlayer samplePlayer_{};
    double sampleRate_{48000.0};
    bool adaptiveJoined_{false};
    int lastAudioMode_{static_cast<int>(robodrummer::LeadershipMode::DrummerLeads)};
    double jamBrainCooldownSeconds_{0.0};
    bool lastArrangementEnabled_{false};
    long long lastArrangementBarIndex_{0};
    std::uint32_t appliedArrangementRevision_{0};

    std::atomic<double> internalBpm_{120.0};
    std::atomic<float> intensity_{0.5f};
    std::atomic<float> dynamicFollow_{0.60f};
    std::atomic<int> jamStyle_{static_cast<int>(robodrummer::JamStyle::Rock)};
    std::atomic<bool> arrangementEnabled_{false};
    std::atomic<int> currentArrangementSection_{0};
    std::array<std::atomic<int>, ArrangementSlotCount> arrangementStyle_{};
    std::array<std::atomic<int>, ArrangementSlotCount> arrangementBars_{};
    std::array<std::atomic<float>, ArrangementSlotCount> arrangementIntensity_{};
    std::array<std::atomic<bool>, ArrangementSlotCount> arrangementSlotEnabled_{};
    std::array<std::atomic<bool>, ArrangementSlotCount> arrangementAutoAdvance_{};
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
    std::atomic<float> guitarDownbeatConfidence_{0.0f};
    std::atomic<int> phraseState_{static_cast<int>(robodrummer::PhraseState::Stable)};
    std::atomic<float> phraseFillStrength_{0.0f};
    std::atomic<bool> phraseBoundary_{false};
    std::atomic<bool> guitarTrackerLocked_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessor)
};
