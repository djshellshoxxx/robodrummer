#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "analysis/DynamicsFollower.h"
#include "analysis/PhraseGrooveModifier.h"
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

robodrummer::Style styleForJamStyle(robodrummer::JamStyle style) {
    switch (style) {
        case robodrummer::JamStyle::Blues: return robodrummer::Style::blues();
        case robodrummer::JamStyle::Funk: return robodrummer::Style::funk();
        case robodrummer::JamStyle::Punk: return robodrummer::Style::punk();
        case robodrummer::JamStyle::Metal: return robodrummer::Style::metal();
        case robodrummer::JamStyle::Shuffle: return robodrummer::Style::shuffle();
        case robodrummer::JamStyle::Rock: return robodrummer::Style::basicRock();
    }
    return robodrummer::Style::basicRock();
}
}

RoboDrummerAudioProcessor::RoboDrummerAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Guitar / Analysis", juce::AudioChannelSet::stereo(), true)
          .withOutput("Drums + Monitor", juce::AudioChannelSet::stereo(), true)) {
    installStarterArrangement();
}

void RoboDrummerAudioProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    jam_.prepare(sampleRate_);
    rhythmAnalyzer_.prepare(sampleRate_);
    performanceAnalyzer_.prepare(sampleRate_);
    timingAuthority_.reset(internalBpm_.load(std::memory_order_relaxed));
    resyncPlanner_.reset();
    jamBrain_.reset();
    silenceController_.reset();
    silenceIntensityMultiplierAudio_ = 1.0f;
    silentBarsVisible_.store(0, std::memory_order_relaxed);
    waitingForResume_.store(false, std::memory_order_relaxed);
    jamBrainCooldownSeconds_ = 0.0;
    arrangement_.reset();
    lastArrangementEnabled_ = false;
    lastArrangementBarIndex_ = 0;
    currentArrangementSection_.store(0, std::memory_order_relaxed);
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
    if ((pending & FillBit) != 0) {
        manualFillSinceMemoryBar_ = true;
        jam_.apply(robodrummer::MidiCommand::Fill);
    }
    if ((pending & ResetBit) != 0) {
        jam_.apply(robodrummer::MidiCommand::ResetListening);
        rhythmAnalyzer_.reset();
        performanceAnalyzer_.reset();
        timingAuthority_.reset(internalBpm_.load(std::memory_order_relaxed));
        resyncPlanner_.reset();
        jamBrain_.reset();
        silenceController_.reset();
        silenceIntensityMultiplierAudio_ = 1.0f;
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
        guitarBeatInBar_.store(rhythm.beatInBar, std::memory_order_relaxed);
        guitarDownbeatConfidence_.store(rhythm.downbeatConfidence, std::memory_order_relaxed);
        guitarTrackerLocked_.store(rhythm.locked, std::memory_order_release);
        guitarIntensity_.store(performance.intensity, std::memory_order_relaxed);
    }

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

    if (auto* playHead = getPlayHead()) {
        if (auto pos = playHead->getPosition()) {
            hostPositionAvailable = true;
            const auto hostBpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            const auto sig = pos->getTimeSignature();
            if (hostBpm.hasValue()) baseBpm = *hostBpm;
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
        resyncPlanner_.reset();
        jamBrain_.reset();
        jamBrainCooldownSeconds_ = 0.0;
        phraseState_.store(static_cast<int>(robodrummer::PhraseState::Stable), std::memory_order_relaxed);
        phraseFillStrength_.store(0.0f, std::memory_order_relaxed);
        phraseBoundary_.store(false, std::memory_order_relaxed);
        if (settings.mode != robodrummer::LeadershipMode::DrummerLeads)
            timingAuthority_.reset(baseBpm);
    }

    const double blockSeconds = buffer.getNumSamples() > 0 ? static_cast<double>(buffer.getNumSamples()) / sampleRate_ : 0.0;
    const auto authority = timingAuthority_.update(baseBpm, rhythm, settings, blockSeconds);
    effectiveDrummerBpm_.store(authority.outputBpm, std::memory_order_relaxed);
    effectiveGuitarAuthority_.store(authority.effectiveGuitarAuthority, std::memory_order_relaxed);

    const bool arrangementEnabled = arrangementEnabled_.load(std::memory_order_relaxed);
    const auto arrangementRevision = arrangementRevision_.load(std::memory_order_acquire);
    if (arrangementRevision != appliedArrangementRevision_) {
        rebuildArrangementFromSlots();
        appliedArrangementRevision_ = arrangementRevision;
        lastArrangementBarIndex_ = jam_.currentBarIndex();
        currentArrangementSection_.store(0, std::memory_order_relaxed);
        if (arrangementEnabled)
            applyCurrentArrangementSection();
    }

    if (arrangementEnabled != lastArrangementEnabled_) {
        arrangement_.reset();
        lastArrangementBarIndex_ = jam_.currentBarIndex();
        currentArrangementSection_.store(0, std::memory_order_relaxed);
        lastArrangementEnabled_ = arrangementEnabled;
        if (arrangementEnabled)
            applyCurrentArrangementSection();
    }

    if (arrangementEnabled) {
        const auto currentBar = jam_.currentBarIndex();
        while (lastArrangementBarIndex_ < currentBar) {
            const auto step = arrangement_.advanceBar();
            ++lastArrangementBarIndex_;
            if (step.sectionChanged) {
                currentArrangementSection_.store(static_cast<int>(step.enteredIndex), std::memory_order_relaxed);
                applyCurrentArrangementSection();
                jam_.apply(robodrummer::MidiCommand::Crash);
            }
        }
        if (currentBar < lastArrangementBarIndex_)
            lastArrangementBarIndex_ = currentBar;
    }

    const auto styleProfile = robodrummer::JamStyleProfile::forStyle(getJamStyle());
    const auto memoryRecommendations = sessionMemory_.recommendations();
    const bool memoryCanAdapt = jamMemoryEnabled_.load(std::memory_order_relaxed) && !arrangementEnabled;
    const auto memoryPolicy = robodrummer::SessionMemoryPolicy::apply(
        styleProfile.toBrainSettings(),
        dynamicFollow_.load(std::memory_order_relaxed),
        memoryRecommendations,
        memoryCanAdapt);

    const float manualIntensity = intensity_.load(std::memory_order_relaxed);
    const float followedIntensity = robodrummer::DynamicsFollower::blend(
        manualIntensity,
        performance.intensity,
        memoryPolicy.dynamicFollow,
        authority.effectiveGuitarAuthority,
        settings.mode);
    const float effectiveIntensity = std::clamp(followedIntensity * silenceIntensityMultiplierAudio_, 0.0f, 1.0f);
    effectiveDrummerIntensity_.store(effectiveIntensity, std::memory_order_relaxed);

    jam_.setTempo(authority.outputBpm);
    jam_.setMeter(numerator, denominator);
    jam_.setIntensity(effectiveIntensity);
    const auto phraseState = getPhraseState();
    const float phraseAmount = phraseState == robodrummer::PhraseState::Break ? 1.0f : 0.75f;
    jam_.setStyle(robodrummer::PhraseGrooveModifier::apply(
        styleForJamStyle(getJamStyle()), phraseState, phraseAmount));

    if (hasPpq && settings.mode == robodrummer::LeadershipMode::DrummerLeads)
        jam_.syncToPpq(hostPpq);

    if (settings.mode != robodrummer::LeadershipMode::DrummerLeads) {
        const auto phase = phaseFollower_.update(jam_.currentBeatPhase(), rhythm, authority.outputBpm,
                                                 authority.effectiveGuitarAuthority, settings.response, sampleRate_);
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

            robodrummer::SilenceSettings silenceSettings;
            silenceSettings.mode = getSilenceMode();
            silenceSettings.stopAfterBars = silenceStopBars_.load(std::memory_order_relaxed);
            const auto silenceDecision = silenceController_.update(performance.activity, silenceSettings);
            silenceIntensityMultiplierAudio_ = silenceDecision.intensityMultiplier;
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

            robodrummer::JamBrainDecision decision;
            if (silenceDecision.suppressAdaptiveChanges) {
                decision.phraseState = getPhraseState();
            } else {
                decision = jamBrain_.update(observation, memoryPolicy.brainSettings);
            }

            phraseState_.store(static_cast<int>(decision.phraseState), std::memory_order_relaxed);
            phraseFillStrength_.store(decision.fillStrength, std::memory_order_relaxed);
            phraseBoundary_.store(decision.phraseBoundary, std::memory_order_relaxed);

            if (decision.requestFill && !jam_.state().fillRequested)
                jam_.requestFill(decision.fillStrength);

            if (jamMemoryEnabled_.load(std::memory_order_relaxed) && !arrangementEnabled) {
                robodrummer::SessionBarObservation memoryObservation;
                memoryObservation.tempoBpm = authority.outputBpm;
                memoryObservation.intensity = performance.intensity;
                memoryObservation.activity = performance.activity;
                memoryObservation.phraseBoundary = decision.phraseBoundary;
                memoryObservation.manualFillRequested = manualFillSinceMemoryBar_;
                memoryObservation.fillOccurred = manualFillSinceMemoryBar_ || decision.requestFill || silenceDecision.requestFill;
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

    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 16) {
            if (const auto command = robodrummer::mapNoteOn(msg.getNoteNumber(), msg.getFloatVelocity())) {
                if (arrangementEnabled && *command == robodrummer::MidiCommand::NextSection) {
                    if (arrangement_.next()) {
                        currentArrangementSection_.store(static_cast<int>(arrangement_.currentSectionIndex()), std::memory_order_relaxed);
                        applyCurrentArrangementSection();
                        jam_.apply(robodrummer::MidiCommand::Crash);
                    }
                } else if (arrangementEnabled && *command == robodrummer::MidiCommand::PreviousSection) {
                    if (arrangement_.previous()) {
                        currentArrangementSection_.store(static_cast<int>(arrangement_.currentSectionIndex()), std::memory_order_relaxed);
                        applyCurrentArrangementSection();
                        jam_.apply(robodrummer::MidiCommand::Crash);
                    }
                } else {
                    if (*command == robodrummer::MidiCommand::Fill)
                        manualFillSinceMemoryBar_ = true;
                    jam_.apply(*command);
                }
            }
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
    state.setProperty("jamStyle", jamStyle_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("arrangementEnabled", arrangementEnabled_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("jamMemoryEnabled", jamMemoryEnabled_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("manualMeterEnabled", manualMeterEnabled_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("manualMeterNumerator", manualMeterNumerator_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("manualMeterDenominator", manualMeterDenominator_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("silenceMode", silenceMode_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("silenceStopBars", silenceStopBars_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("leadershipMode", leadershipMode_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("leadership", leadership_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("followRange", followRangeBpm_.load(std::memory_order_relaxed), nullptr);
    for (int i = 0; i < ArrangementSlotCount; ++i) {
        const auto section = getArrangementSection(i);
        const auto prefix = juce::String("arr") + juce::String(i);
        state.setProperty(juce::Identifier(prefix + "Style"), static_cast<int>(section.style), nullptr);
        state.setProperty(juce::Identifier(prefix + "Bars"), section.bars, nullptr);
        state.setProperty(juce::Identifier(prefix + "Intensity"), section.intensityTarget, nullptr);
        state.setProperty(juce::Identifier(prefix + "Enabled"), isArrangementSectionEnabled(i), nullptr);
        state.setProperty(juce::Identifier(prefix + "Auto"), section.autoAdvance, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void RoboDrummerAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid() && state.hasType("RoboDrummerState")) {
            setInternalBpm(static_cast<double>(state.getProperty("bpm", 120.0)));
            setIntensity(static_cast<float>(state.getProperty("intensity", 0.5f)));
            setDynamicFollow(static_cast<float>(state.getProperty("dynamicFollow", 0.60f)));
            const int style = juce::jlimit(0, 5, static_cast<int>(state.getProperty("jamStyle", 0)));
            setJamStyle(static_cast<robodrummer::JamStyle>(style));
            setArrangementEnabled(static_cast<bool>(state.getProperty("arrangementEnabled", false)));
            setJamMemoryEnabled(static_cast<bool>(state.getProperty("jamMemoryEnabled", true)));
            setManualMeterEnabled(static_cast<bool>(state.getProperty("manualMeterEnabled", false)));
            setManualMeter(
                static_cast<int>(state.getProperty("manualMeterNumerator", 4)),
                static_cast<int>(state.getProperty("manualMeterDenominator", 4)));
            const int silenceMode = juce::jlimit(0, 5, static_cast<int>(state.getProperty("silenceMode", 0)));
            setSilenceMode(static_cast<robodrummer::SilenceMode>(silenceMode));
            setSilenceStopBars(static_cast<int>(state.getProperty("silenceStopBars", 2)));
            const int mode = juce::jlimit(0, 2, static_cast<int>(state.getProperty("leadershipMode", 0)));
            setLeadershipMode(static_cast<robodrummer::LeadershipMode>(mode));
            setLeadership(static_cast<float>(state.getProperty("leadership", 0.5f)));
            setFollowRange(static_cast<double>(state.getProperty("followRange", 15.0)));
            for (int i = 0; i < ArrangementSlotCount; ++i) {
                const auto current = getArrangementSection(i);
                const auto prefix = juce::String("arr") + juce::String(i);
                const int slotStyle = juce::jlimit(0, 5, static_cast<int>(state.getProperty(juce::Identifier(prefix + "Style"), static_cast<int>(current.style))));
                const int slotBars = juce::jlimit(1, 64, static_cast<int>(state.getProperty(juce::Identifier(prefix + "Bars"), current.bars)));
                const float slotIntensity = juce::jlimit(0.0f, 1.0f, static_cast<float>(state.getProperty(juce::Identifier(prefix + "Intensity"), current.intensityTarget)));
                const bool slotEnabled = static_cast<bool>(state.getProperty(juce::Identifier(prefix + "Enabled"), isArrangementSectionEnabled(i)));
                const bool slotAuto = static_cast<bool>(state.getProperty(juce::Identifier(prefix + "Auto"), current.autoAdvance));
                setArrangementSection(i, static_cast<robodrummer::JamStyle>(slotStyle), slotBars, slotIntensity, slotEnabled, slotAuto);
            }
        }
    }
}

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

void RoboDrummerAudioProcessor::setArrangementSection(int index,
                                                      robodrummer::JamStyle style,
                                                      int bars,
                                                      float intensity,
                                                      bool enabled,
                                                      bool autoAdvance) noexcept {
    if (index < 0 || index >= ArrangementSlotCount) return;
    arrangementStyle_[static_cast<std::size_t>(index)].store(juce::jlimit(0, 5, static_cast<int>(style)), std::memory_order_relaxed);
    arrangementBars_[static_cast<std::size_t>(index)].store(juce::jlimit(1, 64, bars), std::memory_order_relaxed);
    arrangementIntensity_[static_cast<std::size_t>(index)].store(juce::jlimit(0.0f, 1.0f, intensity), std::memory_order_relaxed);
    arrangementSlotEnabled_[static_cast<std::size_t>(index)].store(enabled, std::memory_order_relaxed);
    arrangementAutoAdvance_[static_cast<std::size_t>(index)].store(autoAdvance, std::memory_order_relaxed);
    arrangementRevision_.fetch_add(1, std::memory_order_release);
}

robodrummer::SectionDefinition RoboDrummerAudioProcessor::getArrangementSection(int index) const noexcept {
    if (index < 0 || index >= ArrangementSlotCount) return {};
    const auto i = static_cast<std::size_t>(index);
    robodrummer::SectionDefinition result;
    result.style = static_cast<robodrummer::JamStyle>(arrangementStyle_[i].load(std::memory_order_relaxed));
    result.bars = arrangementBars_[i].load(std::memory_order_relaxed);
    result.intensityTarget = arrangementIntensity_[i].load(std::memory_order_relaxed);
    result.autoAdvance = arrangementAutoAdvance_[i].load(std::memory_order_relaxed);
    return result;
}

bool RoboDrummerAudioProcessor::isArrangementSectionEnabled(int index) const noexcept {
    if (index < 0 || index >= ArrangementSlotCount) return false;
    return arrangementSlotEnabled_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
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
    const std::array<robodrummer::SectionDefinition, ArrangementSlotCount> defaults {{
        {robodrummer::JamStyle::Rock, 8, 0.50f, true},
        {robodrummer::JamStyle::Funk, 4, 0.66f, true},
        {robodrummer::JamStyle::Rock, 8, 0.78f, true},
        {robodrummer::JamStyle::Blues, 8, 0.48f, true},
        {robodrummer::JamStyle::Punk, 4, 0.82f, true},
        {robodrummer::JamStyle::Rock, 8, 0.60f, false}
    }};

    for (int i = 0; i < ArrangementSlotCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        arrangementStyle_[index].store(static_cast<int>(defaults[index].style), std::memory_order_relaxed);
        arrangementBars_[index].store(defaults[index].bars, std::memory_order_relaxed);
        arrangementIntensity_[index].store(defaults[index].intensityTarget, std::memory_order_relaxed);
        arrangementSlotEnabled_[index].store(true, std::memory_order_relaxed);
        arrangementAutoAdvance_[index].store(defaults[index].autoAdvance, std::memory_order_relaxed);
    }
    arrangementRevision_.store(1, std::memory_order_release);
    rebuildArrangementFromSlots();
    appliedArrangementRevision_ = 1;
}

void RoboDrummerAudioProcessor::rebuildArrangementFromSlots() noexcept {
    arrangement_.clear();
    for (int i = 0; i < ArrangementSlotCount; ++i) {
        if (!arrangementSlotEnabled_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed))
            continue;
        (void) arrangement_.add(getArrangementSection(i));
    }
    arrangement_.reset();
    currentArrangementSection_.store(0, std::memory_order_relaxed);
}

void RoboDrummerAudioProcessor::applyCurrentArrangementSection() noexcept {
    if (arrangement_.empty()) return;
    const auto& section = arrangement_.current();
    setJamStyle(section.style);
    intensity_.store(section.intensityTarget, std::memory_order_relaxed);
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
