#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "plugin/MidiCommandMapper.h"
#include <array>
#include <cmath>

namespace {
robodrummer::DrumSamplePlayer::Sample makeKick(double sampleRate) {
    robodrummer::DrumSamplePlayer::Sample s;
    const int n = static_cast<int>(sampleRate * 0.35);
    s.left.resize(static_cast<std::size_t>(n));
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double env = std::exp(-t * 13.0);
        const double freq = 95.0 - 55.0 * std::min(1.0, t / 0.12);
        phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
        s.left[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(phase) * env * 0.9);
    }
    return s;
}

robodrummer::DrumSamplePlayer::Sample makeSnare(double sampleRate) {
    robodrummer::DrumSamplePlayer::Sample s;
    const int n = static_cast<int>(sampleRate * 0.22);
    s.left.resize(static_cast<std::size_t>(n));
    std::uint32_t rng = 0x51A9E123u;
    for (int i = 0; i < n; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = static_cast<float>((rng >> 8) & 0x00FFFFFFu) / 8388608.0f - 1.0f;
        const double t = static_cast<double>(i) / sampleRate;
        const float tone = std::sin(static_cast<float>(juce::MathConstants<double>::twoPi * 185.0 * t));
        s.left[static_cast<std::size_t>(i)] = (noise * 0.75f + tone * 0.25f) * static_cast<float>(std::exp(-t * 22.0)) * 0.55f;
    }
    return s;
}

robodrummer::DrumSamplePlayer::Sample makeTom(double sampleRate, double frequency) {
    robodrummer::DrumSamplePlayer::Sample s;
    const int n = static_cast<int>(sampleRate * 0.30);
    s.left.resize(static_cast<std::size_t>(n));
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double f = frequency * (1.0 - 0.16 * std::min(1.0, t / 0.09));
        phase += juce::MathConstants<double>::twoPi * f / sampleRate;
        s.left[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(phase) * std::exp(-t * 10.0) * 0.50);
    }
    return s;
}

robodrummer::DrumSamplePlayer::Sample makeHat(double sampleRate, double seconds = 0.08) {
    robodrummer::DrumSamplePlayer::Sample s;
    const int n = static_cast<int>(sampleRate * seconds);
    s.left.resize(static_cast<std::size_t>(n));
    std::uint32_t rng = 0xC105ED42u;
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        rng = rng * 1103515245u + 12345u;
        const float noise = static_cast<float>((rng >> 9) & 0x007FFFFFu) / 4194304.0f - 1.0f;
        const float hp = noise - prev * 0.86f;
        prev = noise;
        const double t = static_cast<double>(i) / sampleRate;
        const double decay = seconds > 0.1 ? 14.0 : 55.0;
        s.left[static_cast<std::size_t>(i)] = hp * static_cast<float>(std::exp(-t * decay)) * 0.24f;
    }
    return s;
}

robodrummer::DrumSamplePlayer::Sample makeCrash(double sampleRate) {
    robodrummer::DrumSamplePlayer::Sample s;
    const int n = static_cast<int>(sampleRate * 1.2);
    s.left.resize(static_cast<std::size_t>(n));
    std::uint32_t rng = 0xCA45A112u;
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = static_cast<float>((rng >> 8) & 0x00FFFFFFu) / 8388608.0f - 1.0f;
        const float hp = noise - prev * 0.93f;
        prev = noise;
        const double t = static_cast<double>(i) / sampleRate;
        s.left[static_cast<std::size_t>(i)] = hp * static_cast<float>(std::exp(-t * 3.8)) * 0.17f;
    }
    return s;
}
}

RoboDrummerAudioProcessor::RoboDrummerAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Guitar / Analysis", juce::AudioChannelSet::stereo(), true)
          .withOutput("Drums + Monitor", juce::AudioChannelSet::stereo(), true)) {}

void RoboDrummerAudioProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    jam_.prepare(sampleRate_);
    rhythmAnalyzer_.prepare(sampleRate_);
    performanceAnalyzer_.prepare(sampleRate_);
    timingAuthority_.reset(internalBpm_.load(std::memory_order_relaxed));
    jam_.setTempo(internalBpm_.load(std::memory_order_relaxed));
    jam_.setIntensity(intensity_.load(std::memory_order_relaxed));
    adaptiveJoined_ = false;
    adaptiveJoinedVisible_.store(false, std::memory_order_relaxed);
    lastAudioMode_ = leadershipMode_.load(std::memory_order_relaxed);
    samplePlayer_.clear();
    installStarterKit(sampleRate_);
}

void RoboDrummerAudioProcessor::releaseResources() { samplePlayer_.clear(); }

bool RoboDrummerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::disabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void RoboDrummerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const auto pending = pendingUiCommands_.exchange(0, std::memory_order_acquire);
    if ((pending & FillBit) != 0) jam_.apply(robodrummer::MidiCommand::Fill);
    if ((pending & ResetBit) != 0) {
        jam_.apply(robodrummer::MidiCommand::ResetListening);
        rhythmAnalyzer_.reset();
        performanceAnalyzer_.reset();
        timingAuthority_.reset(internalBpm_.load(std::memory_order_relaxed));
        adaptiveJoined_ = false;
        adaptiveJoinedVisible_.store(false, std::memory_order_relaxed);
    }

    robodrummer::RhythmState rhythm{};
    robodrummer::PerformanceState performance{};
    if (getTotalNumInputChannels() > 0 && buffer.getNumSamples() > 0) {
        const auto* guitar = buffer.getReadPointer(0);
        rhythmAnalyzer_.processBlock(guitar, buffer.getNumSamples());
        performanceAnalyzer_.processBlock(guitar, buffer.getNumSamples());
        rhythm = rhythmAnalyzer_.state();
        performance = performanceAnalyzer_.state();
        detectedGuitarBpm_.store(rhythm.tempoBpm, std::memory_order_relaxed);
        guitarTempoConfidence_.store(rhythm.tempoConfidence, std::memory_order_relaxed);
        guitarBeatConfidence_.store(rhythm.beatConfidence, std::memory_order_relaxed);
        guitarBeatPhase_.store(rhythm.beatPhase, std::memory_order_relaxed);
        predictedNextGuitarBeatSeconds_.store(rhythm.predictedNextBeatSeconds, std::memory_order_relaxed);
        guitarTrackerLocked_.store(rhythm.locked, std::memory_order_release);
        guitarIntensity_.store(performance.intensity, std::memory_order_relaxed);
    }

    double baseBpm = internalBpm_.load(std::memory_order_relaxed);
    int numerator = 4;
    int denominator = 4;
    bool playing = true;
    bool hostPositionAvailable = false;
    bool hasPpq = false;
    double hostPpq = 0.0;

    lastHostTempoValid_.store(false, std::memory_order_relaxed);
    lastHostPpqValid_.store(false, std::memory_order_relaxed);

    if (auto* playHead = getPlayHead()) {
        if (auto pos = playHead->getPosition()) {
            hostPositionAvailable = true;
            const auto hostBpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            const auto sig = pos->getTimeSignature();
            if (hostBpm.hasValue()) baseBpm = *hostBpm;
            if (sig.hasValue()) { numerator = sig->numerator; denominator = sig->denominator; }
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

    robodrummer::TimingAuthoritySettings settings;
    settings.mode = getLeadershipMode();
    settings.leadership = leadership_.load(std::memory_order_relaxed);
    settings.followRangeBpm = followRangeBpm_.load(std::memory_order_relaxed);
    settings.response = robodrummer::FollowResponse::Balanced;

    const int modeValue = static_cast<int>(settings.mode);
    if (modeValue != lastAudioMode_) {
        adaptiveJoined_ = settings.mode == robodrummer::LeadershipMode::DrummerLeads;
        adaptiveJoinedVisible_.store(adaptiveJoined_, std::memory_order_relaxed);
        lastAudioMode_ = modeValue;
        if (settings.mode != robodrummer::LeadershipMode::DrummerLeads)
            timingAuthority_.reset(baseBpm);
    }

    const double blockSeconds = buffer.getNumSamples() > 0 ? static_cast<double>(buffer.getNumSamples()) / sampleRate_ : 0.0;
    const auto authority = timingAuthority_.update(baseBpm, rhythm, settings, blockSeconds);
    effectiveDrummerBpm_.store(authority.outputBpm, std::memory_order_relaxed);
    effectiveGuitarAuthority_.store(authority.effectiveGuitarAuthority, std::memory_order_relaxed);

    const float manualIntensity = intensity_.load(std::memory_order_relaxed);
    float effectiveIntensity = manualIntensity;
    if (settings.mode != robodrummer::LeadershipMode::DrummerLeads) {
        const float dynamicAmount = dynamicFollow_.load(std::memory_order_relaxed) * authority.effectiveGuitarAuthority;
        effectiveIntensity = std::clamp(manualIntensity + (performance.intensity - manualIntensity) * dynamicAmount, 0.0f, 1.0f);
    }
    effectiveDrummerIntensity_.store(effectiveIntensity, std::memory_order_relaxed);

    jam_.setTempo(authority.outputBpm);
    jam_.setMeter(numerator, denominator);
    jam_.setIntensity(effectiveIntensity);

    if (hasPpq && settings.mode == robodrummer::LeadershipMode::DrummerLeads)
        jam_.syncToPpq(hostPpq);

    if (settings.mode != robodrummer::LeadershipMode::DrummerLeads) {
        const auto phase = phaseFollower_.update(jam_.currentBeatPhase(), rhythm, authority.outputBpm,
                                                 authority.effectiveGuitarAuthority, settings.response, sampleRate_);
        phaseErrorCycles_.store(phase.phaseErrorCycles, std::memory_order_relaxed);
        hardResyncRecommended_.store(phase.hardResyncRecommended, std::memory_order_relaxed);
        if (!phase.hardResyncRecommended && adaptiveJoined_)
            jam_.nudgePhaseSamples(phase.correctionSamples);

        if (!adaptiveJoined_) {
            const bool confident = rhythm.locked && rhythm.tempoConfidence >= 0.60f && rhythm.beatConfidence >= 0.55f;
            const bool nearBeatBoundary = rhythm.beatPhase <= 0.08 || rhythm.beatPhase >= 0.92;
            if (confident && nearBeatBoundary) {
                jam_.resetPhase();
                adaptiveJoined_ = true;
                adaptiveJoinedVisible_.store(true, std::memory_order_release);
            }
        }
    } else {
        adaptiveJoined_ = true;
        adaptiveJoinedVisible_.store(true, std::memory_order_relaxed);
        phaseErrorCycles_.store(0.0, std::memory_order_relaxed);
        hardResyncRecommended_.store(false, std::memory_order_relaxed);
    }

    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 16) {
            if (const auto command = robodrummer::mapNoteOn(msg.getNoteNumber(), msg.getFloatVelocity()))
                jam_.apply(*command);
        }
    }

    if (hostPositionAvailable && !playing && settings.mode == robodrummer::LeadershipMode::DrummerLeads) return;
    if (settings.mode != robodrummer::LeadershipMode::DrummerLeads && !adaptiveJoined_) return;

    std::array<robodrummer::DrumEvent, 128> events{};
    const auto eventCount = jam_.processBlock(buffer.getNumSamples(), events.data(), events.size());

    int renderedUntil = 0;
    for (std::size_t i = 0; i < eventCount; ++i) {
        const auto offset = juce::jlimit(0, buffer.getNumSamples(), events[i].sampleOffset);
        const int span = offset - renderedUntil;
        if (span > 0) {
            float* outputs[2] { buffer.getWritePointer(0, renderedUntil), buffer.getWritePointer(1, renderedUntil) };
            samplePlayer_.render(outputs, 2, span);
        }
        samplePlayer_.trigger(events[i]);
        const int note = midiNoteFor(events[i].instrument);
        if (note >= 0 && buffer.getNumSamples() > 0) {
            midi.addEvent(juce::MidiMessage::noteOn(10, note, events[i].velocity), offset);
            midi.addEvent(juce::MidiMessage::noteOff(10, note), juce::jmin(buffer.getNumSamples() - 1, offset + 1));
        }
        renderedUntil = offset;
    }
    if (renderedUntil < buffer.getNumSamples()) {
        float* outputs[2] { buffer.getWritePointer(0, renderedUntil), buffer.getWritePointer(1, renderedUntil) };
        samplePlayer_.render(outputs, 2, buffer.getNumSamples() - renderedUntil);
    }
}

juce::AudioProcessorEditor* RoboDrummerAudioProcessor::createEditor() { return new RoboDrummerAudioProcessorEditor(*this); }

void RoboDrummerAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("RoboDrummerState");
    state.setProperty("bpm", internalBpm_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("intensity", intensity_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("dynamicFollow", dynamicFollow_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("leadershipMode", leadershipMode_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("leadership", leadership_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("followRange", followRangeBpm_.load(std::memory_order_relaxed), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void RoboDrummerAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid() && state.hasType("RoboDrummerState")) {
            setInternalBpm(static_cast<double>(state.getProperty("bpm", 120.0)));
            setIntensity(static_cast<float>(state.getProperty("intensity", 0.5f)));
            setDynamicFollow(static_cast<float>(state.getProperty("dynamicFollow", 0.60f)));
            const int mode = juce::jlimit(0, 2, static_cast<int>(state.getProperty("leadershipMode", 0)));
            setLeadershipMode(static_cast<robodrummer::LeadershipMode>(mode));
            setLeadership(static_cast<float>(state.getProperty("leadership", 0.5f)));
            setFollowRange(static_cast<double>(state.getProperty("followRange", 15.0)));
        }
    }
}

void RoboDrummerAudioProcessor::setInternalBpm(double bpm) noexcept {
    internalBpm_.store(juce::jlimit(20.0, 400.0, std::isfinite(bpm) ? bpm : 120.0), std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::setIntensity(float value) noexcept {
    intensity_.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed);
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

void RoboDrummerAudioProcessor::installStarterKit(double sampleRate) {
    samplePlayer_.setSample(robodrummer::DrumInstrument::Kick, makeKick(sampleRate));
    samplePlayer_.setSample(robodrummer::DrumInstrument::Snare, makeSnare(sampleRate));
    samplePlayer_.setSample(robodrummer::DrumInstrument::ClosedHat, makeHat(sampleRate));
    samplePlayer_.setSample(robodrummer::DrumInstrument::OpenHat, makeHat(sampleRate, 0.24));
    samplePlayer_.setSample(robodrummer::DrumInstrument::Ride, makeHat(sampleRate, 0.35));
    samplePlayer_.setSample(robodrummer::DrumInstrument::Crash, makeCrash(sampleRate));
    samplePlayer_.setSample(robodrummer::DrumInstrument::HighTom, makeTom(sampleRate, 190.0));
    samplePlayer_.setSample(robodrummer::DrumInstrument::MidTom, makeTom(sampleRate, 145.0));
    samplePlayer_.setSample(robodrummer::DrumInstrument::FloorTom, makeTom(sampleRate, 105.0));
}

int RoboDrummerAudioProcessor::midiNoteFor(robodrummer::DrumInstrument i) noexcept {
    using I = robodrummer::DrumInstrument;
    switch (i) {
        case I::Kick: return 36;
        case I::Snare: return 38;
        case I::ClosedHat: return 42;
        case I::OpenHat: return 46;
        case I::Ride: return 51;
        case I::Crash: return 49;
        case I::HighTom: return 50;
        case I::MidTom: return 47;
        case I::FloorTom: return 43;
        case I::Rimshot: return 37;
        case I::Sidestick: return 37;
        case I::China: return 52;
        case I::Splash: return 55;
        case I::Cowbell: return 56;
        case I::Tambourine: return 54;
        case I::Clap: return 39;
    }
    return -1;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RoboDrummerAudioProcessor(); }
