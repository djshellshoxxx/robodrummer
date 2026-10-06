#include "PluginEditor.h"
#include <cmath>

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setSize(860, 1015);

    title_.setText("RoboDrummer", juce::dontSendNotification);
    title_.setFont(juce::Font(28.0f, juce::Font::bold));
    title_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title_);

    bpmCaption_.setText("Internal BPM", juce::dontSendNotification);
    intensityCaption_.setText("Intensity", juce::dontSendNotification);
    modeCaption_.setText("Timing mode", juce::dontSendNotification);
    meterCaption_.setText("Meter", juce::dontSendNotification);
    leadershipCaption_.setText("Leadership", juce::dontSendNotification);
    followRangeCaption_.setText("Follow range", juce::dontSendNotification);
    dynamicFollowCaption_.setText("Dynamic follow", juce::dontSendNotification);
    styleCaption_.setText("Jam style", juce::dontSendNotification);
    silenceCaption_.setText("Guitar silence", juce::dontSendNotification);
    for (auto* label : { &bpmCaption_, &intensityCaption_, &modeCaption_, &meterCaption_, &leadershipCaption_, &followRangeCaption_, &dynamicFollowCaption_, &styleCaption_, &silenceCaption_ })
        addAndMakeVisible(*label);

    bpm_.setRange(40.0, 240.0, 0.1);
    bpm_.setValue(processor_.getInternalBpm(), juce::dontSendNotification);
    bpm_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    bpm_.onValueChange = [this] { processor_.setInternalBpm(bpm_.getValue()); };
    addAndMakeVisible(bpm_);

    intensity_.setRange(0.0, 1.0, 0.01);
    intensity_.setValue(processor_.getIntensity(), juce::dontSendNotification);
    intensity_.setSliderStyle(juce::Slider::LinearHorizontal);
    intensity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    intensity_.onValueChange = [this] { processor_.setIntensity(static_cast<float>(intensity_.getValue())); };
    addAndMakeVisible(intensity_);

    leadershipMode_.addItem("Drummer Leads", 1);
    leadershipMode_.addItem("Hybrid", 2);
    leadershipMode_.addItem("Guitarist Leads", 3);
    leadershipMode_.setSelectedId(static_cast<int>(processor_.getLeadershipMode()) + 1, juce::dontSendNotification);
    leadershipMode_.onChange = [this] {
        processor_.setLeadershipMode(static_cast<robodrummer::LeadershipMode>(juce::jlimit(0, 2, leadershipMode_.getSelectedId() - 1)));
    };
    addAndMakeVisible(leadershipMode_);

    manualMeterToggle_.setToggleState(processor_.isManualMeterEnabled(), juce::dontSendNotification);
    manualMeterToggle_.onClick = [this] { processor_.setManualMeterEnabled(manualMeterToggle_.getToggleState()); };
    addAndMakeVisible(manualMeterToggle_);

    for (int numerator = 2; numerator <= 12; ++numerator)
        meterNumerator_.addItem(juce::String(numerator), numerator - 1);
    meterNumerator_.setSelectedId(processor_.getManualMeterNumerator() - 1, juce::dontSendNotification);

    meterDenominator_.addItem("2", 1);
    meterDenominator_.addItem("4", 2);
    meterDenominator_.addItem("8", 3);
    meterDenominator_.addItem("16", 4);
    const auto denominatorToId = [](int denominator) {
        switch (denominator) {
            case 2: return 1;
            case 8: return 3;
            case 16: return 4;
            case 4:
            default: return 2;
        }
    };
    meterDenominator_.setSelectedId(denominatorToId(processor_.getManualMeterDenominator()), juce::dontSendNotification);

    const auto commitMeter = [this] {
        int denominator = 4;
        switch (meterDenominator_.getSelectedId()) {
            case 1: denominator = 2; break;
            case 3: denominator = 8; break;
            case 4: denominator = 16; break;
            default: break;
        }
        processor_.setManualMeter(juce::jlimit(2, 12, meterNumerator_.getSelectedId() + 1), denominator);
    };
    meterNumerator_.onChange = commitMeter;
    meterDenominator_.onChange = commitMeter;
    addAndMakeVisible(meterNumerator_);
    addAndMakeVisible(meterDenominator_);

    jamStyle_.addItem("Rock", 1);
    jamStyle_.addItem("Blues", 2);
    jamStyle_.addItem("Funk", 3);
    jamStyle_.addItem("Punk", 4);
    jamStyle_.addItem("Metal", 5);
    jamStyle_.addItem("Shuffle", 6);
    jamStyle_.setSelectedId(static_cast<int>(processor_.getJamStyle()) + 1, juce::dontSendNotification);
    jamStyle_.onChange = [this] {
        processor_.setJamStyle(static_cast<robodrummer::JamStyle>(juce::jlimit(0, 5, jamStyle_.getSelectedId() - 1)));
    };
    addAndMakeVisible(jamStyle_);

    arrangementToggle_.setToggleState(processor_.isArrangementEnabled(), juce::dontSendNotification);
    arrangementToggle_.onClick = [this] { processor_.setArrangementEnabled(arrangementToggle_.getToggleState()); };
    addAndMakeVisible(arrangementToggle_);

    jamMemoryToggle_.setToggleState(processor_.isJamMemoryEnabled(), juce::dontSendNotification);
    jamMemoryToggle_.onClick = [this] { processor_.setJamMemoryEnabled(jamMemoryToggle_.getToggleState()); };
    addAndMakeVisible(jamMemoryToggle_);

    silenceMode_.addItem("Keep playing", 1);
    silenceMode_.addItem("Reduce intensity", 2);
    silenceMode_.addItem("Hold groove", 3);
    silenceMode_.addItem("Fill during silence", 4);
    silenceMode_.addItem("Stop after bars", 5);
    silenceMode_.addItem("Wait for resume", 6);
    silenceMode_.setSelectedId(static_cast<int>(processor_.getSilenceMode()) + 1, juce::dontSendNotification);
    silenceMode_.onChange = [this] {
        processor_.setSilenceMode(static_cast<robodrummer::SilenceMode>(juce::jlimit(0, 5, silenceMode_.getSelectedId() - 1)));
    };
    addAndMakeVisible(silenceMode_);

    silenceStopBars_.setRange(1.0, 16.0, 1.0);
    silenceStopBars_.setValue(processor_.getSilenceStopBars(), juce::dontSendNotification);
    silenceStopBars_.setSliderStyle(juce::Slider::LinearHorizontal);
    silenceStopBars_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 24);
    silenceStopBars_.setTextValueSuffix(" bars");
    silenceStopBars_.onValueChange = [this] { processor_.setSilenceStopBars(static_cast<int>(std::lround(silenceStopBars_.getValue()))); };
    addAndMakeVisible(silenceStopBars_);

    arrangementEditCaption_.setText("Edit section", juce::dontSendNotification);
    addAndMakeVisible(arrangementEditCaption_);

    for (int i = 0; i < RoboDrummerAudioProcessor::ArrangementSlotCount; ++i)
        arrangementSlot_.addItem(juce::String(i + 1), i + 1);
    arrangementSlot_.setSelectedId(1, juce::dontSendNotification);
    arrangementSlot_.onChange = [this] {
        editingArrangementSlot_ = juce::jlimit(0, RoboDrummerAudioProcessor::ArrangementSlotCount - 1,
                                              arrangementSlot_.getSelectedId() - 1);
        loadArrangementEditorSlot();
    };
    addAndMakeVisible(arrangementSlot_);

    arrangementStyle_.addItem("Rock", 1);
    arrangementStyle_.addItem("Blues", 2);
    arrangementStyle_.addItem("Funk", 3);
    arrangementStyle_.addItem("Punk", 4);
    arrangementStyle_.addItem("Metal", 5);
    arrangementStyle_.addItem("Shuffle", 6);
    arrangementStyle_.onChange = [this] { commitArrangementEditorSlot(); };
    addAndMakeVisible(arrangementStyle_);

    arrangementBars_.setRange(1.0, 64.0, 1.0);
    arrangementBars_.setSliderStyle(juce::Slider::LinearHorizontal);
    arrangementBars_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 24);
    arrangementBars_.setTextValueSuffix(" bars");
    arrangementBars_.onValueChange = [this] { commitArrangementEditorSlot(); };
    addAndMakeVisible(arrangementBars_);

    arrangementIntensity_.setRange(0.0, 1.0, 0.01);
    arrangementIntensity_.setSliderStyle(juce::Slider::LinearHorizontal);
    arrangementIntensity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 24);
    arrangementIntensity_.onValueChange = [this] { commitArrangementEditorSlot(); };
    addAndMakeVisible(arrangementIntensity_);

    arrangementSlotEnabled_.onClick = [this] { commitArrangementEditorSlot(); };
    arrangementAutoAdvance_.onClick = [this] { commitArrangementEditorSlot(); };
    addAndMakeVisible(arrangementSlotEnabled_);
    addAndMakeVisible(arrangementAutoAdvance_);

    leadership_.setRange(0.0, 1.0, 0.01);
    leadership_.setValue(processor_.getLeadership(), juce::dontSendNotification);
    leadership_.setSliderStyle(juce::Slider::LinearHorizontal);
    leadership_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    leadership_.onValueChange = [this] { processor_.setLeadership(static_cast<float>(leadership_.getValue())); };
    addAndMakeVisible(leadership_);

    followRange_.setRange(0.0, 80.0, 1.0);
    followRange_.setValue(processor_.getFollowRange(), juce::dontSendNotification);
    followRange_.setSliderStyle(juce::Slider::LinearHorizontal);
    followRange_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    followRange_.setTextValueSuffix(" BPM");
    followRange_.onValueChange = [this] { processor_.setFollowRange(followRange_.getValue()); };
    addAndMakeVisible(followRange_);

    dynamicFollow_.setRange(0.0, 1.0, 0.01);
    dynamicFollow_.setValue(processor_.getDynamicFollow(), juce::dontSendNotification);
    dynamicFollow_.setSliderStyle(juce::Slider::LinearHorizontal);
    dynamicFollow_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    dynamicFollow_.onValueChange = [this] { processor_.setDynamicFollow(static_cast<float>(dynamicFollow_.getValue())); };
    addAndMakeVisible(dynamicFollow_);

    tempoLabel_.setText("Drummer tempo: --", juce::dontSendNotification);
    transportLabel_.setText("Transport: internal", juce::dontSendNotification);
    guitarLabel_.setText("Guitar estimate: listening", juce::dontSendNotification);
    trackingLabel_.setText("Tracker: acquiring", juce::dontSendNotification);
    authorityLabel_.setText("Guitar authority: 0%", juce::dontSendNotification);
    dynamicsLabel_.setText("Dynamics: listening", juce::dontSendNotification);
    phraseLabel_.setText("Phrase: stable", juce::dontSendNotification);
    sectionLabel_.setText("Arrangement: free jam", juce::dontSendNotification);
    memoryLabel_.setText("Jam memory: learning", juce::dontSendNotification);
    silenceLabel_.setText("Silence behavior: active", juce::dontSendNotification);
    coordinatorLabel_.setText("Jam coordinator: establishing groove", juce::dontSendNotification);
    for (auto* label : { &tempoLabel_, &transportLabel_, &guitarLabel_, &trackingLabel_, &authorityLabel_, &dynamicsLabel_, &phraseLabel_, &memoryLabel_, &silenceLabel_, &coordinatorLabel_, &sectionLabel_ })
        addAndMakeVisible(*label);

    fillButton_.onClick = [this] { processor_.requestFill(); };
    resetButton_.onClick = [this] { processor_.resetJamPhase(); };
    addAndMakeVisible(fillButton_);
    addAndMakeVisible(resetButton_);

    loadArrangementEditorSlot();
    startTimerHz(12);
}

void RoboDrummerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(17, 20, 25));
    g.setColour(juce::Colour::fromRGB(55, 68, 80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 12.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(190, 198, 205));
    g.setFont(13.0f);
    g.drawText("Adaptive tempo, phase, bar position and dynamics are confidence-gated. Hard resync waits for a reliable beat 1.",
               24, 972, getWidth() - 48, 24, juce::Justification::centredLeft);
}

void RoboDrummerAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    title_.setBounds(area.removeFromTop(42));
    area.removeFromTop(8);

    auto row = area.removeFromTop(36);
    bpmCaption_.setBounds(row.removeFromLeft(112));
    bpm_.setBounds(row);
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    intensityCaption_.setBounds(row.removeFromLeft(112));
    intensity_.setBounds(row);
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    modeCaption_.setBounds(row.removeFromLeft(112));
    leadershipMode_.setBounds(row.removeFromLeft(220));
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    meterCaption_.setBounds(row.removeFromLeft(112));
    manualMeterToggle_.setBounds(row.removeFromLeft(135));
    row.removeFromLeft(8);
    meterNumerator_.setBounds(row.removeFromLeft(70));
    row.removeFromLeft(8);
    meterDenominator_.setBounds(row.removeFromLeft(70));
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    styleCaption_.setBounds(row.removeFromLeft(112));
    jamStyle_.setBounds(row.removeFromLeft(220));
    row.removeFromLeft(14);
    arrangementToggle_.setBounds(row.removeFromLeft(220));
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    leadershipCaption_.setBounds(row.removeFromLeft(112));
    leadership_.setBounds(row);
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    followRangeCaption_.setBounds(row.removeFromLeft(112));
    followRange_.setBounds(row);
    area.removeFromTop(5);

    row = area.removeFromTop(36);
    dynamicFollowCaption_.setBounds(row.removeFromLeft(112));
    dynamicFollow_.setBounds(row);

    area.removeFromTop(5);
    row = area.removeFromTop(32);
    row.removeFromLeft(112);
    jamMemoryToggle_.setBounds(row.removeFromLeft(180));

    area.removeFromTop(5);
    row = area.removeFromTop(36);
    silenceCaption_.setBounds(row.removeFromLeft(112));
    silenceMode_.setBounds(row.removeFromLeft(205));
    row.removeFromLeft(10);
    silenceStopBars_.setBounds(row);

    area.removeFromTop(8);
    row = area.removeFromTop(34);
    arrangementEditCaption_.setBounds(row.removeFromLeft(112));
    arrangementSlot_.setBounds(row.removeFromLeft(62));
    row.removeFromLeft(8);
    arrangementStyle_.setBounds(row.removeFromLeft(128));
    row.removeFromLeft(8);
    arrangementSlotEnabled_.setBounds(row.removeFromLeft(86));
    arrangementAutoAdvance_.setBounds(row.removeFromLeft(120));

    row = area.removeFromTop(36);
    row.removeFromLeft(112);
    arrangementBars_.setBounds(row.removeFromLeft(255));
    row.removeFromLeft(10);
    arrangementIntensity_.setBounds(row);

    area.removeFromTop(12);
    tempoLabel_.setBounds(area.removeFromTop(25));
    transportLabel_.setBounds(area.removeFromTop(25));
    guitarLabel_.setBounds(area.removeFromTop(25));
    trackingLabel_.setBounds(area.removeFromTop(25));
    authorityLabel_.setBounds(area.removeFromTop(25));
    dynamicsLabel_.setBounds(area.removeFromTop(25));
    phraseLabel_.setBounds(area.removeFromTop(25));
    memoryLabel_.setBounds(area.removeFromTop(25));
    silenceLabel_.setBounds(area.removeFromTop(25));
    coordinatorLabel_.setBounds(area.removeFromTop(25));
    sectionLabel_.setBounds(area.removeFromTop(25));

    area.removeFromTop(10);
    auto buttons = area.removeFromTop(42);
    fillButton_.setBounds(buttons.removeFromLeft(160));
    buttons.removeFromLeft(12);
    resetButton_.setBounds(buttons.removeFromLeft(190));
}

void RoboDrummerAudioProcessorEditor::loadArrangementEditorSlot() {
    loadingArrangementEditor_ = true;
    const auto section = processor_.getArrangementSection(editingArrangementSlot_);
    arrangementStyle_.setSelectedId(static_cast<int>(section.style) + 1, juce::dontSendNotification);
    arrangementBars_.setValue(section.bars, juce::dontSendNotification);
    arrangementIntensity_.setValue(section.intensityTarget, juce::dontSendNotification);
    arrangementSlotEnabled_.setToggleState(processor_.isArrangementSectionEnabled(editingArrangementSlot_), juce::dontSendNotification);
    arrangementAutoAdvance_.setToggleState(section.autoAdvance, juce::dontSendNotification);
    loadingArrangementEditor_ = false;
}

void RoboDrummerAudioProcessorEditor::commitArrangementEditorSlot() {
    if (loadingArrangementEditor_) return;
    const int styleIndex = juce::jlimit(0, 5, arrangementStyle_.getSelectedId() - 1);
    processor_.setArrangementSection(
        editingArrangementSlot_,
        static_cast<robodrummer::JamStyle>(styleIndex),
        static_cast<int>(std::lround(arrangementBars_.getValue())),
        static_cast<float>(arrangementIntensity_.getValue()),
        arrangementSlotEnabled_.getToggleState(),
        arrangementAutoAdvance_.getToggleState());
}

void RoboDrummerAudioProcessorEditor::timerCallback() {
    arrangementToggle_.setToggleState(processor_.isArrangementEnabled(), juce::dontSendNotification);
    jamMemoryToggle_.setToggleState(processor_.isJamMemoryEnabled(), juce::dontSendNotification);
    silenceMode_.setSelectedId(static_cast<int>(processor_.getSilenceMode()) + 1, juce::dontSendNotification);
    silenceStopBars_.setValue(processor_.getSilenceStopBars(), juce::dontSendNotification);
    manualMeterToggle_.setToggleState(processor_.isManualMeterEnabled(), juce::dontSendNotification);
    meterNumerator_.setSelectedId(processor_.getManualMeterNumerator() - 1, juce::dontSendNotification);
    switch (processor_.getManualMeterDenominator()) {
        case 2: meterDenominator_.setSelectedId(1, juce::dontSendNotification); break;
        case 8: meterDenominator_.setSelectedId(3, juce::dontSendNotification); break;
        case 16: meterDenominator_.setSelectedId(4, juce::dontSendNotification); break;
        default: meterDenominator_.setSelectedId(2, juce::dontSendNotification); break;
    }
    jamStyle_.setSelectedId(static_cast<int>(processor_.getJamStyle()) + 1, juce::dontSendNotification);
    if (processor_.isArrangementEnabled())
        intensity_.setValue(processor_.getIntensity(), juce::dontSendNotification);

    const auto t = processor_.getLastTransport();
    tempoLabel_.setText("Drummer tempo: " + juce::String(processor_.getEffectiveDrummerBpm(), 1) + " BPM", juce::dontSendNotification);
    transportLabel_.setText(
        t.validTempo
            ? (juce::String("Transport: host | ") + (t.playing ? "playing" : "stopped") +
               " | meter " + juce::String(processor_.getEffectiveMeterNumerator()) + "/" +
               juce::String(processor_.getEffectiveMeterDenominator()))
            : ("Transport: internal fallback | meter " + juce::String(processor_.getEffectiveMeterNumerator()) + "/" +
               juce::String(processor_.getEffectiveMeterDenominator())),
        juce::dontSendNotification);

    const auto detected = processor_.getDetectedGuitarBpm();
    const auto tempoConfidence = processor_.getGuitarTempoConfidence();
    const auto beatConfidence = processor_.getGuitarBeatConfidence();
    guitarLabel_.setText(
        "Guitar estimate: " + juce::String(detected, 1) + " BPM | tempo confidence " +
            juce::String(tempoConfidence * 100.0f, 0) + "%",
        juce::dontSendNotification);
    trackingLabel_.setText(
        juce::String("Tracker: ") + (processor_.isGuitarTrackerLocked() ? "LOCKED" : "acquiring") +
            " | beat " + juce::String(processor_.getGuitarBeatInBar()) + "/" +
            juce::String(processor_.getEffectiveMeterNumerator()) +
            " | beat conf " + juce::String(beatConfidence * 100.0f, 0) + "%" +
            " | downbeat conf " + juce::String(processor_.getGuitarDownbeatConfidence() * 100.0f, 0) + "%" +
            (processor_.hasAdaptiveJoined() ? " | joined" : " | waiting to join"),
        juce::dontSendNotification);
    authorityLabel_.setText(
        "Guitar authority: " + juce::String(processor_.getEffectiveGuitarAuthority() * 100.0f, 0) +
            "% | phase error " + juce::String(processor_.getPhaseErrorCycles(), 3) +
            (processor_.isHardResyncRecommended() ? " | RESYNC NEEDED" : ""),
        juce::dontSendNotification);
    dynamicsLabel_.setText(
        "Dynamics: guitar " + juce::String(processor_.getDetectedGuitarIntensity() * 100.0f, 0) +
            "% | drummer " + juce::String(processor_.getEffectiveDrummerIntensity() * 100.0f, 0) + "%",
        juce::dontSendNotification);

    juce::String phraseName = "stable";
    switch (processor_.getPhraseState()) {
        case robodrummer::PhraseState::Build: phraseName = "BUILD"; break;
        case robodrummer::PhraseState::Release: phraseName = "RELEASE"; break;
        case robodrummer::PhraseState::Break: phraseName = "BREAK"; break;
        case robodrummer::PhraseState::Stable: break;
    }
    phraseLabel_.setText(
        "Phrase: " + phraseName +
            " | fill strength " + juce::String(processor_.getPhraseFillStrength() * 100.0f, 0) + "%" +
            (processor_.isPhraseBoundary() ? " | BOUNDARY" : ""),
        juce::dontSendNotification);

    if (!processor_.isJamMemoryEnabled()) {
        memoryLabel_.setText("Jam memory: disabled", juce::dontSendNotification);
    } else if (processor_.isArrangementEnabled()) {
        memoryLabel_.setText("Jam memory: paused while Programmed Arrangement is active", juce::dontSendNotification);
    } else {
        memoryLabel_.setText(
            "Jam memory: " + juce::String(processor_.getJamMemoryBars()) + " bars | confidence " +
                juce::String(processor_.getJamMemoryConfidence() * 100.0f, 0) + "% | avg tempo " +
                juce::String(processor_.getJamMemoryAverageTempo(), 1) + " | phrase " +
                juce::String(processor_.getJamMemoryAveragePhraseBars(), 1) + " bars | fill bias " +
                juce::String(processor_.getJamMemoryFillBias(), 2) + " | dyn x" +
                juce::String(processor_.getJamMemoryDynamicSensitivity(), 2),
            juce::dontSendNotification);
    }

    juce::String silenceModeName = "keep playing";
    switch (processor_.getSilenceMode()) {
        case robodrummer::SilenceMode::ReduceIntensity: silenceModeName = "reduce intensity"; break;
        case robodrummer::SilenceMode::HoldGroove: silenceModeName = "hold groove"; break;
        case robodrummer::SilenceMode::FillDuringSilence: silenceModeName = "fill during silence"; break;
        case robodrummer::SilenceMode::StopAfterBars: silenceModeName = "stop after bars"; break;
        case robodrummer::SilenceMode::WaitForResume: silenceModeName = "wait for resume"; break;
        case robodrummer::SilenceMode::KeepPlaying: break;
    }
    silenceLabel_.setText(
        "Silence: " + silenceModeName + " | silent bars " + juce::String(processor_.getSilentBars()) +
            (processor_.isWaitingForGuitarResume() ? " | WAITING FOR GUITAR" : ""),
        juce::dontSendNotification);

    juce::String coordinationName = "establishing groove";
    switch (processor_.getJamCoordinationState()) {
        case robodrummer::JamCoordinationState::StableJam: coordinationName = "STABLE JAM"; break;
        case robodrummer::JamCoordinationState::Building: coordinationName = "BUILDING"; break;
        case robodrummer::JamCoordinationState::Releasing: coordinationName = "RELEASING"; break;
        case robodrummer::JamCoordinationState::AwaitingCue: coordinationName = "AWAITING CUE"; break;
        case robodrummer::JamCoordinationState::TransitionLikely: coordinationName = "TRANSITION LIKELY"; break;
        case robodrummer::JamCoordinationState::Break: coordinationName = "BREAK"; break;
        case robodrummer::JamCoordinationState::SoloSupport: coordinationName = "SOLO SUPPORT"; break;
        case robodrummer::JamCoordinationState::EndingLikely: coordinationName = "ENDING LIKELY"; break;
        case robodrummer::JamCoordinationState::EstablishingGroove: break;
    }
    coordinatorLabel_.setText(
        "Coordinator: " + coordinationName +
            " | transition " + juce::String(processor_.getTransitionProbability() * 100.0f, 0) + "%" +
            " | ending " + juce::String(processor_.getEndingProbability() * 100.0f, 0) + "%" +
            (processor_.isCoordinatorSuppressingBusyFills() ? " | restrained fills" : ""),
        juce::dontSendNotification);

    if (processor_.isArrangementEnabled()) {
        sectionLabel_.setText(
            "Arrangement: section " + juce::String(processor_.getCurrentArrangementSection() + 1) +
                " | MIDI 37 next / 38 previous | 48 solo / 49 end",
            juce::dontSendNotification);
    } else {
        sectionLabel_.setText("Arrangement: free jam | MIDI 48 solo / 49 end", juce::dontSendNotification);
    }
}
