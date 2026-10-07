#include "PluginEditor.h"
#include <cmath>

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setSize(860, 972);

    title_.setText("RoboDrummer", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    title_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title_);

    helpButton_.setTooltip("Open the full RoboDrummer help section.");
    optionsButton_.setTooltip("Open interface options, including the global tooltip switch.");
    helpButton_.onClick = [this] { showHelp(); };
    optionsButton_.onClick = [this] { showOptions(); };
    addAndMakeVisible(helpButton_);
    addAndMakeVisible(optionsButton_);

    helpText_.setMultiLine(true);
    helpText_.setReadOnly(true);
    helpText_.setScrollbarsShown(true);
    helpText_.setCaretVisible(false);
    helpText_.setText(
        "ROBODRUMMER HELP\n\n"
        "GETTING STARTED\n"
        "Choose the timing source, output mode, meter and jam style. RoboDrummer can follow host timing or use its internal BPM, "
        "then adapt its playing to detected guitar tempo, dynamics, phrase state and confidence.\n\n"
        "TIMING AND LEADERSHIP\n"
        "Internal BPM is used when host timing is unavailable. Leadership controls how strongly the drummer leads versus follows. "
        "Follow Range limits tempo movement and Dynamic Follow controls how strongly guitar dynamics affect the drummer.\n\n"
        "METER AND STYLE\n"
        "Use Manual Meter when you need a fixed numerator/denominator. Jam Style selects the groove family. Output Mode selects how "
        "the generated performance is delivered.\n\n"
        "ARRANGEMENT\n"
        "Programmed Arrangement enables section-based playback. Select a section slot, style, bar length and intensity, then enable "
        "or disable that slot and choose whether it auto-advances.\n\n"
        "JAM MEMORY AND SILENCE\n"
        "Learn this jam stores recurring tempo, phrase and dynamic tendencies while free-jamming. Guitar Silence determines whether "
        "the drummer keeps playing, reduces intensity, holds, fills, stops after a number of bars or waits for the guitar to resume.\n\n"
        "LIVE STATUS\n"
        "The lower readouts show transport, guitar tempo confidence, beat/downbeat confidence, authority, dynamics, phrase detection, "
        "jam memory, silence state, coordinator state and current arrangement section. Treat low-confidence estimates as provisional.\n\n"
        "PERFORMANCE CONTROLS\n"
        "FILL plays a fill into the next bar. CRASH hits a crash now. BREAK drops out for one bar and re-enters. HALF-TIME and "
        "DOUBLE-TIME toggle the feel (press again for normal time). STOP silences the drummer until RESUME. PREV/NEXT SECTION move "
        "through the programmed arrangement (NEXT also cues a transition in free jam). SOLO cues solo support and END JAM cues an "
        "ending. RESET LISTENING clears tempo, phase, phrase, silence and jam-memory tracking so the tracker can reacquire.\n\n"
        "MIDI CONTROL (channel 16 note-ons; consumed, not forwarded)\n"
        "36 Fill | 37 Next section | 38 Previous section | 39 Crash | 40 Intensity up | 41 Intensity down | 42 Half-time | "
        "43 Double-time | 44 Break | 45 Stop | 46 Resume | 47 Reset listening | 48 Solo support | 49 End jam\n"
        "Generated drum MIDI is sent on channel 10 (GM drum map). Other MIDI passes through unchanged.\n\n"
        "HOST AUTOMATION\n"
        "All main controls are host parameters and can be automated. Intensity and Jam Style also move when MIDI intensity notes "
        "or arrangement sections change them, and the host is informed. Double-click a slider to reset it to its default.\n\n"
        "TOOLTIPS\n"
        "Hover any control for a description. OPTIONS > Show tooltips globally enables or disables hover help.\n\n"
        "TROUBLESHOOTING\n"
        "If tracking is unstable, verify a clean input and wait for confidence to rise before expecting hard synchronization. If the "
        "host supplies no valid tempo, RoboDrummer reports internal fallback and uses Internal BPM.\n");
    helpText_.setVisible(false);
    addChildComponent(helpText_);

    closeHelpButton_.setTooltip("Close the RoboDrummer help section.");
    closeHelpButton_.onClick = [this] { helpText_.setVisible(false); closeHelpButton_.setVisible(false); };
    closeHelpButton_.setVisible(false);
    addChildComponent(closeHelpButton_);
    applyTooltipSetting();

    bpmCaption_.setText("Internal BPM", juce::dontSendNotification);
    intensityCaption_.setText("Intensity", juce::dontSendNotification);
    modeCaption_.setText("Timing mode", juce::dontSendNotification);
    outputModeCaption_.setText("Output mode", juce::dontSendNotification);
    meterCaption_.setText("Meter", juce::dontSendNotification);
    leadershipCaption_.setText("Leadership", juce::dontSendNotification);
    followRangeCaption_.setText("Follow range", juce::dontSendNotification);
    dynamicFollowCaption_.setText("Dynamic follow", juce::dontSendNotification);
    styleCaption_.setText("Jam style", juce::dontSendNotification);
    silenceCaption_.setText("Guitar silence", juce::dontSendNotification);
    for (auto* label : { &bpmCaption_, &intensityCaption_, &modeCaption_, &outputModeCaption_, &meterCaption_, &leadershipCaption_, &followRangeCaption_, &dynamicFollowCaption_, &styleCaption_, &silenceCaption_ })
        addAndMakeVisible(*label);

    auto& params = processor_.getParameters();
    namespace ids = robodrummer::ParamIDs;
    for (auto* slider : { &bpm_, &intensity_, &leadership_, &followRange_, &dynamicFollow_, &silenceStopBars_ }) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 24);
        addAndMakeVisible(*slider);
    }
    bpm_.setTextValueSuffix(" BPM");
    followRange_.setTextValueSuffix(" BPM");
    silenceStopBars_.setTextValueSuffix(" bars");
    bpmAttachment_ = std::make_unique<SliderAttachment>(params, ids::bpm, bpm_);
    intensityAttachment_ = std::make_unique<SliderAttachment>(params, ids::intensity, intensity_);
    leadershipAttachment_ = std::make_unique<SliderAttachment>(params, ids::leadership, leadership_);
    followRangeAttachment_ = std::make_unique<SliderAttachment>(params, ids::followRange, followRange_);
    dynamicFollowAttachment_ = std::make_unique<SliderAttachment>(params, ids::dynamicFollow, dynamicFollow_);
    silenceStopBarsAttachment_ = std::make_unique<SliderAttachment>(params, ids::silenceStopBars, silenceStopBars_);
    bpm_.setDoubleClickReturnValue(true, 120.0);
    intensity_.setDoubleClickReturnValue(true, 0.5);
    leadership_.setDoubleClickReturnValue(true, 0.5);
    followRange_.setDoubleClickReturnValue(true, 15.0);
    dynamicFollow_.setDoubleClickReturnValue(true, 0.6);
    silenceStopBars_.setDoubleClickReturnValue(true, 2.0);

    // Combo box item IDs are choice index + 1, which is what ComboBoxAttachment expects.
    leadershipMode_.addItemList({ "Drummer Leads", "Hybrid", "Guitarist Leads" }, 1);
    outputMode_.addItemList({ "Internal Drums", "MIDI Only", "Internal + MIDI" }, 1);
    for (int numerator = 2; numerator <= 12; ++numerator)
        meterNumerator_.addItem(juce::String(numerator), numerator - 1);
    meterDenominator_.addItemList({ "2", "4", "8", "16" }, 1);
    jamStyle_.addItemList({ "Rock", "Blues", "Funk", "Punk", "Metal", "Shuffle" }, 1);
    silenceMode_.addItemList({ "Keep playing", "Reduce intensity", "Hold groove", "Fill during silence", "Stop after bars", "Wait for resume" }, 1);
    for (auto* box : { &leadershipMode_, &outputMode_, &meterNumerator_, &meterDenominator_, &jamStyle_, &silenceMode_ })
        addAndMakeVisible(*box);
    leadershipModeAttachment_ = std::make_unique<ComboBoxAttachment>(params, ids::leadershipMode, leadershipMode_);
    outputModeAttachment_ = std::make_unique<ComboBoxAttachment>(params, ids::outputMode, outputMode_);
    meterNumeratorAttachment_ = std::make_unique<ComboBoxAttachment>(params, ids::meterNumerator, meterNumerator_);
    meterDenominatorAttachment_ = std::make_unique<ComboBoxAttachment>(params, ids::meterDenominator, meterDenominator_);
    jamStyleAttachment_ = std::make_unique<ComboBoxAttachment>(params, ids::jamStyle, jamStyle_);
    silenceModeAttachment_ = std::make_unique<ComboBoxAttachment>(params, ids::silenceMode, silenceMode_);

    for (auto* toggle : { &arrangementToggle_, &jamMemoryToggle_, &manualMeterToggle_ })
        addAndMakeVisible(*toggle);
    arrangementAttachment_ = std::make_unique<ButtonAttachment>(params, ids::arrangementEnabled, arrangementToggle_);
    jamMemoryAttachment_ = std::make_unique<ButtonAttachment>(params, ids::jamMemoryEnabled, jamMemoryToggle_);
    manualMeterAttachment_ = std::make_unique<ButtonAttachment>(params, ids::manualMeterEnabled, manualMeterToggle_);

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

    arrangementStyle_.addItemList({ "Rock", "Blues", "Funk", "Punk", "Metal", "Shuffle" }, 1);
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

    bpm_.setTooltip("Fallback drummer tempo used when valid host tempo is unavailable (40-240 BPM). Double-click resets to 120.");
    intensity_.setTooltip("Base drummer intensity before dynamic follow, silence and coordinator adjustments.");
    leadershipMode_.setTooltip("Choose who owns the clock: host/internal tempo, a blend, or the detected guitar.");
    outputMode_.setTooltip("Internal drum audio, generated MIDI on channel 10, or both.");
    manualMeterToggle_.setTooltip("Override the host time signature with the manually selected meter.");
    meterNumerator_.setTooltip("Manual time-signature numerator (beats per bar).");
    meterDenominator_.setTooltip("Manual time-signature denominator (beat unit).");
    jamStyle_.setTooltip("Groove family used in free jam. Programmed arrangement sections set this automatically.");
    arrangementToggle_.setTooltip("Use the programmed section arrangement instead of free-jam structure.");
    jamMemoryToggle_.setTooltip("Learn recurring tempo, phrase and dynamic tendencies during free jam.");
    silenceMode_.setTooltip("Choose what the drummer does when guitar input becomes silent.");
    silenceStopBars_.setTooltip("Number of silent bars before stopping when Stop after bars is selected.");
    arrangementSlot_.setTooltip("Choose the arrangement section to edit.");
    arrangementStyle_.setTooltip("Style used by the selected arrangement section.");
    arrangementBars_.setTooltip("Length of the selected arrangement section in bars.");
    arrangementIntensity_.setTooltip("Target intensity for the selected arrangement section.");
    arrangementSlotEnabled_.setTooltip("Include this arrangement section in playback.");
    arrangementAutoAdvance_.setTooltip("Advance automatically when this section finishes; otherwise wait for a NEXT SECTION cue.");
    leadership_.setTooltip("Balance drummer leadership against following the detected guitar performance (Hybrid/Guitarist Leads).");
    followRange_.setTooltip("Maximum tempo-following range around the base tempo, in BPM.");
    dynamicFollow_.setTooltip("How strongly guitar dynamics influence drummer intensity.");

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

    using C = robodrummer::MidiCommand;
    setupPerformanceButton(fillButton_, C::Fill, "Play a fill leading into the next bar (MIDI ch16 note 36).");
    setupPerformanceButton(crashButton_, C::Crash, "Hit a crash cymbal now (MIDI ch16 note 39).");
    setupPerformanceButton(breakButton_, C::Break, "Drop out for one bar, then re-enter automatically (MIDI ch16 note 44).");
    setupPerformanceButton(halfTimeButton_, C::HalfTime, "Toggle half-time feel; press again to return to normal time (MIDI ch16 note 42).");
    setupPerformanceButton(doubleTimeButton_, C::DoubleTime, "Toggle double-time feel; press again to return to normal time (MIDI ch16 note 43).");
    setupPerformanceButton(stopButton_, C::Stop, "Stop the drummer until RESUME (MIDI ch16 note 45).");
    setupPerformanceButton(resumeButton_, C::Resume, "Resume the drummer after STOP (MIDI ch16 note 46).");
    setupPerformanceButton(previousSectionButton_, C::PreviousSection, "Jump to the previous arrangement section (MIDI ch16 note 38).");
    setupPerformanceButton(nextSectionButton_, C::NextSection, "Advance to the next arrangement section, or cue a transition in free jam (MIDI ch16 note 37).");
    setupPerformanceButton(soloButton_, C::SoloSupport, "Cue solo support: the drummer settles and restrains busy fills (MIDI ch16 note 48).");
    setupPerformanceButton(endJamButton_, C::EndJam, "Cue the end of the jam (MIDI ch16 note 49).");
    setupPerformanceButton(resetButton_, C::ResetListening,
                           "Clear tempo, phase, phrase, silence and jam-memory tracking so listening can reacquire (MIDI ch16 note 47).");

    seenArrangementRevision_ = processor_.getArrangementRevision();
    loadArrangementEditorSlot();
    startTimerHz(12);
}

void RoboDrummerAudioProcessorEditor::setupPerformanceButton(juce::TextButton& button,
                                                             robodrummer::MidiCommand command,
                                                             const juce::String& tooltip) {
    button.setTooltip(tooltip);
    button.onClick = [this, command] { processor_.requestCommand(command); };
    addAndMakeVisible(button);
}

void RoboDrummerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(17, 20, 25));
    g.setColour(juce::Colour::fromRGB(55, 68, 80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 12.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(190, 198, 205));
    g.setFont(13.0f);
    g.drawText("Adaptive tempo, phase, bar position and dynamics are confidence-gated. Hard resync waits for a reliable beat 1.",
               24, getHeight() - 43, getWidth() - 48, 24, juce::Justification::centredLeft);
}

void RoboDrummerAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    auto titleRow = area.removeFromTop(42);
    title_.setBounds(titleRow.removeFromLeft(420));
    helpButton_.setBounds(titleRow.removeFromRight(70).reduced(0, 5));
    titleRow.removeFromRight(6);
    optionsButton_.setBounds(titleRow.removeFromRight(84).reduced(0, 5));
    area.removeFromTop(8);

    auto row = area.removeFromTop(32);
    bpmCaption_.setBounds(row.removeFromLeft(112));
    bpm_.setBounds(row);
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    intensityCaption_.setBounds(row.removeFromLeft(112));
    intensity_.setBounds(row);
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    modeCaption_.setBounds(row.removeFromLeft(112));
    leadershipMode_.setBounds(row.removeFromLeft(220));
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    outputModeCaption_.setBounds(row.removeFromLeft(112));
    outputMode_.setBounds(row.removeFromLeft(220));
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    meterCaption_.setBounds(row.removeFromLeft(112));
    manualMeterToggle_.setBounds(row.removeFromLeft(135));
    row.removeFromLeft(8);
    meterNumerator_.setBounds(row.removeFromLeft(70));
    row.removeFromLeft(8);
    meterDenominator_.setBounds(row.removeFromLeft(70));
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    styleCaption_.setBounds(row.removeFromLeft(112));
    jamStyle_.setBounds(row.removeFromLeft(220));
    row.removeFromLeft(14);
    arrangementToggle_.setBounds(row.removeFromLeft(220));
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    leadershipCaption_.setBounds(row.removeFromLeft(112));
    leadership_.setBounds(row);
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    followRangeCaption_.setBounds(row.removeFromLeft(112));
    followRange_.setBounds(row);
    area.removeFromTop(4);

    row = area.removeFromTop(32);
    dynamicFollowCaption_.setBounds(row.removeFromLeft(112));
    dynamicFollow_.setBounds(row);

    area.removeFromTop(4);
    row = area.removeFromTop(32);
    row.removeFromLeft(112);
    jamMemoryToggle_.setBounds(row.removeFromLeft(180));

    area.removeFromTop(4);
    row = area.removeFromTop(32);
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

    row = area.removeFromTop(32);
    row.removeFromLeft(112);
    arrangementBars_.setBounds(row.removeFromLeft(255));
    row.removeFromLeft(10);
    arrangementIntensity_.setBounds(row);

    area.removeFromTop(12);
    tempoLabel_.setBounds(area.removeFromTop(23));
    transportLabel_.setBounds(area.removeFromTop(23));
    guitarLabel_.setBounds(area.removeFromTop(23));
    trackingLabel_.setBounds(area.removeFromTop(23));
    authorityLabel_.setBounds(area.removeFromTop(23));
    dynamicsLabel_.setBounds(area.removeFromTop(23));
    phraseLabel_.setBounds(area.removeFromTop(23));
    memoryLabel_.setBounds(area.removeFromTop(23));
    silenceLabel_.setBounds(area.removeFromTop(23));
    coordinatorLabel_.setBounds(area.removeFromTop(23));
    sectionLabel_.setBounds(area.removeFromTop(23));

    const auto layoutButtonRow = [](juce::Rectangle<int> buttonRow, std::initializer_list<juce::Component*> buttons) {
        const int gap = 8;
        const int count = static_cast<int>(buttons.size());
        const int width = (buttonRow.getWidth() - gap * (count - 1)) / count;
        for (auto* button : buttons) {
            button->setBounds(buttonRow.removeFromLeft(width));
            buttonRow.removeFromLeft(gap);
        }
    };
    area.removeFromTop(10);
    layoutButtonRow(area.removeFromTop(36), { &fillButton_, &crashButton_, &breakButton_, &halfTimeButton_, &doubleTimeButton_, &stopButton_, &resumeButton_ });
    area.removeFromTop(8);
    layoutButtonRow(area.removeFromTop(36), { &previousSectionButton_, &nextSectionButton_, &soloButton_, &endJamButton_, &resetButton_ });

    if (helpText_.isVisible()) {
        auto helpArea = getLocalBounds().reduced(48, 44);
        auto closeRow = helpArea.removeFromBottom(40);
        closeHelpButton_.setBounds(closeRow.withSizeKeepingCentre(130, 30));
        helpText_.setBounds(helpArea);
        helpText_.toFront(false);
        closeHelpButton_.toFront(false);
    }
}

void RoboDrummerAudioProcessorEditor::applyTooltipSetting() {
    if (tooltipsEnabled_) {
        if (tooltipWindow_ == nullptr)
            tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 600);
    } else {
        tooltipWindow_.reset();
    }
}

void RoboDrummerAudioProcessorEditor::showOptions() {
    juce::PopupMenu menu;
    menu.addSectionHeader("Interface");
    menu.addItem(1, "Show tooltips", true, tooltipsEnabled_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&optionsButton_),
                       [this](int result) {
                           if (result == 1) {
                               tooltipsEnabled_ = !tooltipsEnabled_;
                               applyTooltipSetting();
                           }
                       });
}

void RoboDrummerAudioProcessorEditor::showHelp() {
    helpText_.setVisible(true);
    closeHelpButton_.setVisible(true);
    resized();
    helpText_.toFront(false);
    closeHelpButton_.toFront(false);
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
    // Host parameters are kept in sync by the attachments; the arrangement slots are plain state and
    // can change underneath the editor (session recall), so reload the visible slot when they do.
    const auto arrangementRevision = processor_.getArrangementRevision();
    if (arrangementRevision != seenArrangementRevision_) {
        seenArrangementRevision_ = arrangementRevision;
        if (!arrangementBars_.isMouseButtonDown() && !arrangementIntensity_.isMouseButtonDown())
            loadArrangementEditorSlot();
    }
    stopButton_.setToggleState(processor_.isDrummerStopped(), juce::dontSendNotification);
    const auto timeScale = processor_.getTimeScale();
    halfTimeButton_.setToggleState(timeScale < 0.75, juce::dontSendNotification);
    doubleTimeButton_.setToggleState(timeScale > 1.5, juce::dontSendNotification);

    const auto t = processor_.getLastTransport();
    juce::String feel;
    if (timeScale < 0.75) feel = " | HALF-TIME";
    else if (timeScale > 1.5) feel = " | DOUBLE-TIME";
    tempoLabel_.setText("Drummer tempo: " + juce::String(processor_.getEffectiveDrummerBpm(), 1) + " BPM" + feel +
                            (processor_.isDrummerStopped() ? " | STOPPED (press RESUME)" : ""),
                        juce::dontSendNotification);
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
