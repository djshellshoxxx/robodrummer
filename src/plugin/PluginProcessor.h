#pragma once
#include <JuceHeader.h>
#include "audio/DrumSamplePlayer.h"
#include "plugin/HostTransportAdapter.h"
#include "plugin/JamEngine.h"
#include <atomic>

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
    double getInternalBpm() const noexcept { return internalBpm_.load(); }
    void setIntensity(float value) noexcept;
    float getIntensity() const noexcept { return intensity_.load(); }
    robodrummer::HostTransportSnapshot getLastTransport() const noexcept { return lastTransport_; }
    void requestFill() noexcept { jam_.apply(robodrummer::MidiCommand::Fill); }
    void resetJamPhase() noexcept { jam_.resetPhase(); }

private:
    void installStarterKit(double sampleRate);
    static int midiNoteFor(robodrummer::DrumInstrument) noexcept;

    robodrummer::JamEngine jam_{};
    robodrummer::DrumSamplePlayer samplePlayer_{};
    std::atomic<double> internalBpm_{120.0};
    std::atomic<float> intensity_{0.5f};
    robodrummer::HostTransportSnapshot lastTransport_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessor)
};
