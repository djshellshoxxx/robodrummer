#include "PluginEditor.h"

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setSize(720, 610);

    title_.setText("RoboDrummer", juce::dontSendNotification);
    title_.setFont(juce::Font(28.0f, juce::Font::bold));
    title_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title_);

    bpmCaption_.setText("Internal BPM", juce::dontSendNotification);
    intensityCaption_.setText("Intensity", juce::dontSendNotification);
    modeCaption_.setText("Timing mode", juce::dontSendNotification);
    leadershipCaption_.setText("Leadership", juce::dontSendNotification);
    followRangeCaption_.setText("Follow range", juce::dontSendNotification);
    dynamicFollowCaption_.setText("Dynamic follow", juce::dontSendNotification);
    for (auto* label : { &bpmCaption_, &intensityCaption_, &modeCaption_, &leadershipCaption_, &followRangeCaption_, &dynamicFollowCaption_ })
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
    for (auto* label : { &tempoLabel_, &transportLabel_, &guitarLabel_, &trackingLabel_, &authorityLabel_, &dynamicsLabel_ })
        addAndMakeVisible(*label);

    fillButton_.onClick = [this] { processor_.requestFill(); };
    resetButton_.onClick = [this] { processor_.resetJamPhase(); };
    addAndMakeVisible(fillButton_);
    addAndMakeVisible(resetButton_);

    startTimerHz(12);
}

void RoboDrummerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(17, 20, 25));
    g.setColour(juce::Colour::fromRGB(55, 68, 80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 12.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(190, 198, 205));
    g.setFont(13.0f);
    g.drawText("Adaptive tempo, phase and dynamics are confidence-gated. Large phase errors wait for a musical resync path.",
               24, 568, getWidth() - 48, 24, juce::Justification::centredLeft);
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

    area.removeFromTop(12);
    tempoLabel_.setBounds(area.removeFromTop(25));
    transportLabel_.setBounds(area.removeFromTop(25));
    guitarLabel_.setBounds(area.removeFromTop(25));
    trackingLabel_.setBounds(area.removeFromTop(25));
    authorityLabel_.setBounds(area.removeFromTop(25));
    dynamicsLabel_.setBounds(area.removeFromTop(25));

    area.removeFromTop(10);
    auto buttons = area.removeFromTop(42);
    fillButton_.setBounds(buttons.removeFromLeft(160));
    buttons.removeFromLeft(12);
    resetButton_.setBounds(buttons.removeFromLeft(190));
}

void RoboDrummerAudioProcessorEditor::timerCallback() {
    const auto t = processor_.getLastTransport();
    tempoLabel_.setText("Drummer tempo: " + juce::String(processor_.getEffectiveDrummerBpm(), 1) + " BPM", juce::dontSendNotification);
    transportLabel_.setText(
        t.validTempo ? (juce::String("Transport: host | ") + (t.playing ? "playing" : "stopped"))
                     : "Transport: internal fallback",
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
            " | beat confidence " + juce::String(beatConfidence * 100.0f, 0) + "% | phase " +
            juce::String(processor_.getGuitarBeatPhase(), 2) +
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
}
