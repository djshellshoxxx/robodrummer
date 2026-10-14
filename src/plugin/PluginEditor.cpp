// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#include "PluginEditor.h"
#include <cmath>

namespace {
constexpr std::array<const char*, 8> kTabNames { "LIVE", "KIT", "DRUMMER", "STYLE", "SONG", "MIDI", "ANALYSIS", "SETTINGS" };

const char* kHelpText =
    "ROBODRUMMER HELP\n\n"
    "QUICK START\n"
    "1. Pick a Style, Kit and Drummer on the LIVE tab. 2. Choose a Timing mode: Drummer Leads (the host / internal BPM / MIDI clock is the boss), "
    "Hybrid (shared) or Guitarist Leads (the guitar sets the tempo). 3. Press LISTEN and play a few bars. RoboDrummer acquires tempo and beat, "
    "waits for a reliable beat, then joins on a beat boundary. 4. Keep playing - fills, dynamics and re-entry after silences are automatic.\n\n"
    "TABS\n"
    "LIVE: the main controls, tempo/beat/section read-outs, tracker confidence and the performance buttons.\n"
    "KIT: choose a built-in kit or your own WAV/AIFF folder (kick.wav, snare_v1..v3.wav for velocity layers, snare_v2_rr2.wav for round robin, "
    "closedhat, openhat, ride, crash, tom1, tom2, floortom). The mixer has Kick, Snare, Toms, Hi-Hat, Ride, Cymbals, Room and Master with gain, pan, "
    "mute and solo. Enable the extra output buses in your DAW for separate Kick / Snare / Toms / Hats + Cymbals outputs.\n"
    "DRUMMER: personality, humanize, feel (push / lay back), complexity, crash tendency, and the fill controls (frequency, length 1 beat to 2 bars, "
    "complexity, surprise).\n"
    "STYLE: 22 styles from Rock to Jazz / Swing, manual meter (3/4, 6/8, 5/4, 7/8...), Jam memory and the guitar-silence behaviour.\n"
    "SONG: a six-section programmed arrangement with per-section style, length, intensity, half/double time, transition (crash / fill / break / none), "
    "drummer personality, fill frequency, leadership and follow strength.\n"
    "MIDI: tempo source (host, internal, MIDI clock), note commands with MIDI Learn, controller mapping with MIDI Learn (expression pedal defaults: "
    "CC11 = Intensity, CC12 = Leadership).\n"
    "ANALYSIS: onsets, predicted beat grid, tempo hypotheses, phase error, confidence history, input level / gate and state transitions.\n"
    "SETTINGS: guitar input selection, gain and noise gate, optional calibration, tooltips.\n\n"
    "PRESETS\n"
    "The header bar loads and saves Jam, Drummer, Style, Kit, Song and Analysis presets. Each type only stores its own slice, so a Kit preset never "
    "changes the groove. Factory Jam presets are also available as host programs.\n\n"
    "DEFAULT MIDI COMMANDS (channel 16 note-ons; consumed, not forwarded)\n"
    "36 Fill (queued) | 37 Next section | 38 Previous section | 39 Crash | 40 Intensity up | 41 Intensity down | 42 Half-time | 43 Double-time | "
    "44 Break | 45 Stop | 46 Resume | 47 Reset listening | 48 Solo support | 49 End jam | 50 Immediate fill.\n"
    "Generated drum MIDI is sent on channel 10 using the General MIDI drum map. Other MIDI passes through unchanged.\n\n"
    "HOST AUTOMATION\n"
    "Every main control is an automatable host parameter. Intensity and Style also follow MIDI intensity commands and programmed sections, and the host is informed.\n\n"
    "TROUBLESHOOTING\n"
    "If tracking is unstable, check the ANALYSIS tab for a clean input level, use the noise gate or run calibration, and wait for the confidence bars "
    "to rise before expecting tight synchronisation. If the host provides no tempo, RoboDrummer uses Internal BPM.\n";
}

juce::File RoboDrummerAudioProcessorEditor::settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Circuit Drift Labs").getChildFile("RoboDrummer").getChildFile("settings.xml");
}

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setLookAndFeel(&lookAndFeel_);

    title_.setText("RoboDrummer", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, rdui::kText);
    addAndMakeVisible(title_);
    rdui::styleCaption(subtitle_, "adaptive jam partner", 13.0f);
    addAndMakeVisible(subtitle_);

    bool tooltipsOn = true;
    if (auto xml = juce::XmlDocument::parse(settingsFile()))
        tooltipsOn = xml->getBoolAttribute("tooltips", true);

    for (int i = 0; i < TabCount; ++i) {
        auto& button = tabButtons_[static_cast<std::size_t>(i)];
        button.setButtonText(kTabNames[static_cast<std::size_t>(i)]);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(9001);
        button.onClick = [this, i] { selectTab(i); };
        addAndMakeVisible(button);
    }
    panels_[0] = std::make_unique<rdui::LivePanel>(processor_);
    panels_[1] = std::make_unique<rdui::KitPanel>(processor_);
    panels_[2] = std::make_unique<rdui::DrummerPanel>(processor_);
    panels_[3] = std::make_unique<rdui::StylePanel>(processor_);
    panels_[4] = std::make_unique<rdui::SongPanel>(processor_);
    panels_[5] = std::make_unique<rdui::MidiPanel>(processor_);
    panels_[6] = std::make_unique<rdui::AnalysisPanel>(processor_);
    panels_[7] = std::make_unique<rdui::SettingsPanel>(processor_, [this](bool on) { applyTooltipSetting(on); }, tooltipsOn, [this] { showHelp(); });
    for (auto& panel : panels_) addChildComponent(*panel);

    for (int t = 0; t < robodrummer::kPresetTypeCount; ++t)
        presetType_.addItem(robodrummer::PresetManager::typeName(static_cast<robodrummer::PresetType>(t)), t + 1);
    presetType_.setSelectedId(1, juce::dontSendNotification);
    presetType_.setTooltip("Preset type. Each type only stores its own slice of the settings.");
    presetType_.onChange = [this] { rebuildPresetList(); };
    presetList_.setTooltip("Choose a preset to load it (factory presets first, then your own).");
    presetList_.setTextWhenNothingSelected("Load preset...");
    presetList_.onChange = [this] {
        const int index = presetList_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(presetEntries_.size()))
            robodrummer::PresetManager::load(processor_, static_cast<robodrummer::PresetType>(presetType_.getSelectedId() - 1), presetEntries_[static_cast<std::size_t>(index)]);
    };
    presetSave_.setTooltip("Save the current settings as a user preset of this type.");
    presetSave_.onClick = [this] { promptSavePreset(); };
    presetDelete_.setTooltip("Delete the selected user preset (factory presets cannot be deleted).");
    presetDelete_.onClick = [this] {
        const int index = presetList_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(presetEntries_.size()) && !presetEntries_[static_cast<std::size_t>(index)].factory) {
            robodrummer::PresetManager::deleteUser(static_cast<robodrummer::PresetType>(presetType_.getSelectedId() - 1), presetEntries_[static_cast<std::size_t>(index)].name);
            rebuildPresetList();
        }
    };
    helpButton_.setTooltip("Open the full RoboDrummer help section.");
    helpButton_.onClick = [this] { showHelp(); };
    for (auto* c : std::initializer_list<juce::Component*>{ &presetType_, &presetList_, &presetSave_, &presetDelete_, &helpButton_ }) addAndMakeVisible(*c);
    rebuildPresetList();

    helpText_.setMultiLine(true);
    helpText_.setReadOnly(true);
    helpText_.setScrollbarsShown(true);
    helpText_.setCaretVisible(false);
    helpText_.setFont(juce::FontOptions(14.0f));
    helpText_.setText(kHelpText);
    addChildComponent(helpText_);
    closeHelpButton_.setTooltip("Close the RoboDrummer help section.");
    closeHelpButton_.onClick = [this] { helpText_.setVisible(false); closeHelpButton_.setVisible(false); };
    addChildComponent(closeHelpButton_);

    rdui::styleCaption(footer_, "Adaptive tempo, phase and dynamics are confidence-gated; the drummer joins on a reliable beat boundary.", 12.0f);
    addAndMakeVisible(footer_);

    applyTooltipSetting(tooltipsOn);
    selectTab(0);
    // Sized last: every child must exist before the first resized() call.
    setResizable(true, true);
    setResizeLimits(900, 640, 1600, 1100);
    setSize(1040, 720);
    startTimerHz(20);
}

RoboDrummerAudioProcessorEditor::~RoboDrummerAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void RoboDrummerAudioProcessorEditor::applyTooltipSetting(bool enabled) {
    if (enabled) {
        if (tooltipWindow_ == nullptr) tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 600);
    } else {
        tooltipWindow_.reset();
    }
    auto file = settingsFile();
    if (file.getParentDirectory().createDirectory().wasOk()) {
        juce::XmlElement xml("RoboDrummerSettings");
        xml.setAttribute("tooltips", enabled);
        xml.writeTo(file);
    }
}

void RoboDrummerAudioProcessorEditor::showHelp() {
    helpText_.setVisible(true);
    closeHelpButton_.setVisible(true);
    resized();
    helpText_.toFront(false);
    closeHelpButton_.toFront(false);
}

void RoboDrummerAudioProcessorEditor::selectTab(int index) {
    currentTab_ = juce::jlimit(0, TabCount - 1, index);
    for (int i = 0; i < TabCount; ++i) panels_[static_cast<std::size_t>(i)]->setVisible(i == currentTab_);
    panels_[static_cast<std::size_t>(currentTab_)]->refresh();
    for (int i = 0; i < TabCount; ++i)
        tabButtons_[static_cast<std::size_t>(i)].setToggleState(i == currentTab_, juce::dontSendNotification);
}

void RoboDrummerAudioProcessorEditor::rebuildPresetList() {
    const auto type = static_cast<robodrummer::PresetType>(juce::jmax(0, presetType_.getSelectedId() - 1));
    presetEntries_ = robodrummer::PresetManager::list(type);
    presetList_.clear(juce::dontSendNotification);
    int id = 1;
    bool addedUserHeader = false;
    for (const auto& entry : presetEntries_) {
        if (!entry.factory && !addedUserHeader) { presetList_.addSeparator(); addedUserHeader = true; }
        presetList_.addItem((entry.factory ? "" : "[User] ") + entry.name, id++);
    }
    presetList_.setSelectedId(0, juce::dontSendNotification);
}

void RoboDrummerAudioProcessorEditor::promptSavePreset() {
    auto* window = new juce::AlertWindow("Save preset", "Name for the new " + juce::String(robodrummer::PresetManager::typeName(
        static_cast<robodrummer::PresetType>(presetType_.getSelectedId() - 1))) + " preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", "", "Preset name");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<RoboDrummerAudioProcessorEditor> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
        if (safe == nullptr || result != 1) return;
        const auto name = window->getTextEditorContents("name");
        const auto type = static_cast<robodrummer::PresetType>(safe->presetType_.getSelectedId() - 1);
        if (robodrummer::PresetManager::saveUser(safe->processor_, type, name)) safe->rebuildPresetList();
    }), true);
}

void RoboDrummerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(rdui::kBackground);
    g.setColour(rdui::kAccent);
    g.fillRect(0, 0, getWidth(), 3);
}

void RoboDrummerAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(12, 8);
    area.removeFromTop(3);
    auto header = area.removeFromTop(40);
    title_.setBounds(header.removeFromLeft(180));
    subtitle_.setBounds(header.removeFromLeft(140));
    helpButton_.setBounds(header.removeFromRight(70).reduced(0, 6)); header.removeFromRight(8);
    presetDelete_.setBounds(header.removeFromRight(70).reduced(0, 6)); header.removeFromRight(4);
    presetSave_.setBounds(header.removeFromRight(60).reduced(0, 6)); header.removeFromRight(6);
    presetList_.setBounds(header.removeFromRight(230).reduced(0, 6)); header.removeFromRight(6);
    presetType_.setBounds(header.removeFromRight(100).reduced(0, 6));
    area.removeFromTop(4);
    auto tabs = area.removeFromTop(32);
    const int tabWidth = tabs.getWidth() / TabCount;
    for (auto& b : tabButtons_) b.setBounds(tabs.removeFromLeft(tabWidth).reduced(2, 0));
    area.removeFromTop(6);
    footer_.setBounds(area.removeFromBottom(20));
    for (auto& panel : panels_)
        if (panel != nullptr) panel->setBounds(area);

    if (helpText_.isVisible()) {
        auto helpArea = getLocalBounds().reduced(48, 44);
        auto closeRow = helpArea.removeFromBottom(40);
        closeHelpButton_.setBounds(closeRow.withSizeKeepingCentre(130, 30));
        helpText_.setBounds(helpArea);
        helpText_.toFront(false);
        closeHelpButton_.toFront(false);
    }
}

void RoboDrummerAudioProcessorEditor::timerCallback() {
    panels_[static_cast<std::size_t>(currentTab_)]->refresh();
}
