// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#include "PluginEditor.h"
#include <cmath>

const char* RoboDrummerAudioProcessorEditor::tabName(int index) {
    static constexpr std::array<const char*, TabCount> names { "LIVE", "KIT", "DRUMMER", "STYLE", "SONG", "PRACTICE", "MIDI", "ANALYSIS", "SETTINGS" };
    return names[static_cast<std::size_t>(juce::jlimit(0, TabCount - 1, index))];
}

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p)
    : RoboDrummerAudioProcessorEditor(p, rdui::queryPlatformReducedMotion) {}

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p,
    std::function<std::optional<bool>()> platformMotionReader)
    : AudioProcessorEditor(&p), processor_(p) {
    setLookAndFeel(&lookAndFeel_);

    title_.setText("RoboDrummer", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, rdui::kText);
    addAndMakeVisible(title_);
    rdui::styleCaption(subtitle_, "adaptive jam partner", 13.0f);
    addAndMakeVisible(subtitle_);

    auto settings = rdui::EditorSettings::load();
    const auto savedSettings = juce::XmlDocument::parse(rdui::EditorSettings::file());
    const std::optional<bool> savedMotion = savedSettings != nullptr
        && savedSettings->hasTagName("RoboDrummerSettings") && savedSettings->hasAttribute("reducedMotion")
        ? std::optional<bool>{settings.reducedMotion} : std::nullopt;
    settings.reducedMotion = rdui::resolveReducedMotionPreference(savedMotion,
        !savedMotion && platformMotionReader ? platformMotionReader() : std::optional<bool>{});
    rdui::uiPrefs().reducedMotion = settings.reducedMotion;
    rdui::uiPrefs().highContrast = settings.highContrast;
    tooltipDelayMs_ = settings.tooltipDelayMs;
    setTitle("RoboDrummer editor");

    for (int i = 0; i < TabCount; ++i) {
        auto& button = tabButtons_[static_cast<std::size_t>(i)];
        button.setButtonText(tabName(i));
        button.setTitle(juce::String(tabName(i)) + " tab");
        button.setTooltip(juce::String("Show the ") + tabName(i) + " tab.");
        button.setClickingTogglesState(true);
        button.setRadioGroupId(9001);
        button.onClick = [this, i] { selectTab(i); };
        addAndMakeVisible(button);
    }
    rdui::SettingsPanel::Callbacks callbacks;
    callbacks.tooltips = [this](bool on) { applyTooltipSetting(on); };
    callbacks.tooltipDelay = [this](int ms) { applyTooltipDelay(ms); };
    callbacks.reducedMotion = [this](bool on) { applyReducedMotion(on); };
    callbacks.highContrast = [this](bool on) { applyHighContrast(on); };
    callbacks.showHelp = [this] { showHelp(); };
    callbacks.showTour = [this] { startTour(); };
    panels_[0] = std::make_unique<rdui::LivePanel>(processor_);
    panels_[1] = std::make_unique<rdui::KitPanel>(processor_);
    panels_[2] = std::make_unique<rdui::DrummerPanel>(processor_);
    panels_[3] = std::make_unique<rdui::StylePanel>(processor_);
    panels_[4] = std::make_unique<rdui::SongPanel>(processor_);
    panels_[5] = std::make_unique<rdui::PracticePanel>(processor_);
    panels_[6] = std::make_unique<rdui::MidiPanel>(processor_);
    panels_[7] = std::make_unique<rdui::AnalysisPanel>(processor_);
    panels_[8] = std::make_unique<rdui::SettingsPanel>(processor_, callbacks, settings);
    for (int i = 0; i < TabCount; ++i) panels_[static_cast<std::size_t>(i)]->setTitle(juce::String(tabName(i)) + " tab");
    for (auto& panel : panels_) addChildComponent(*panel);

    for (int t = 0; t < robodrummer::kPresetTypeCount; ++t)
        presetType_.addItem(robodrummer::PresetManager::typeName(static_cast<robodrummer::PresetType>(t)), t + 1);
    presetType_.setSelectedId(1, juce::dontSendNotification);
    presetType_.setTooltip("Preset type. Each type only stores its own slice of the settings.");
    presetType_.setTitle("Preset type"); presetList_.setTitle("Preset"); presetSave_.setTitle("Save preset"); presetDelete_.setTitle("Delete preset"); helpButton_.setTitle("Help and About");
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
    helpButton_.setTooltip("Open the RoboDrummer help guide, About information and the quick tour.");
    helpButton_.onClick = [this] { showHelp(); };
    for (auto* c : std::initializer_list<juce::Component*>{ &presetType_, &presetList_, &presetSave_, &presetDelete_, &helpButton_ }) addAndMakeVisible(*c);
    rebuildPresetList();

    helpOverlay_ = std::make_unique<rdui::HelpOverlay>(processor_, [this] { hideHelp(); }, [this] { hideHelp(); startTour(); });
    addChildComponent(*helpOverlay_);
    tour_ = std::make_unique<rdui::TourCard>([this](int tab) { selectTab(tab); }, [this] { rdui::EditorSettings::setBool("tourSeen", true); });
    addChildComponent(*tour_);

    rdui::styleCaption(footer_, "Adaptive tempo, phase and dynamics are confidence-gated; the drummer joins on a reliable beat boundary.", 12.0f);
    addAndMakeVisible(footer_);

    addAndMakeVisible(focusRing_);
    setWantsKeyboardFocus(true);
    if (settings.tooltips) tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, tooltipDelayMs_);
    if (settings.highContrast) rdui::applyHighContrast(*this, true);
    selectTab(0);
    // Sized last: every child must exist before the first resized() call.
    setResizable(true, true);
    setResizeLimits(kMinWidth, kMinHeight, 1700, 1200);
    setSize(kDefaultWidth, kDefaultHeight);
    setTimerRate(settings.reducedMotion ? 5 : 20);
    if (!settings.tourSeen) startTour();
}

RoboDrummerAudioProcessorEditor::~RoboDrummerAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void RoboDrummerAudioProcessorEditor::setTimerRate(int hz) {
    timerHz_ = hz;
    startTimerHz(hz);
}

void RoboDrummerAudioProcessorEditor::applyTooltipSetting(bool enabled) {
    if (enabled) {
        if (tooltipWindow_ == nullptr) tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, tooltipDelayMs_);
    } else {
        tooltipWindow_.reset();
    }
    rdui::EditorSettings::setBool("tooltips", enabled);
}

void RoboDrummerAudioProcessorEditor::applyTooltipDelay(int ms) {
    tooltipDelayMs_ = ms;
    if (tooltipWindow_ != nullptr) tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, tooltipDelayMs_);
    rdui::EditorSettings::setInt("tooltipDelayMs", ms);
}

void RoboDrummerAudioProcessorEditor::applyReducedMotion(bool on) {
    rdui::uiPrefs().reducedMotion = on;
    setTimerRate(on ? 5 : 20);
    static_cast<rdui::AnalysisPanel&>(*panels_[7]).setReducedMotion(on);
    rdui::EditorSettings::setBool("reducedMotion", on);
}

void RoboDrummerAudioProcessorEditor::applyHighContrast(bool on) {
    rdui::uiPrefs().highContrast = on;
    rdui::applyHighContrast(*this, on);
    rdui::EditorSettings::setBool("highContrast", on);
    repaint();
}

void RoboDrummerAudioProcessorEditor::showHelp() {
    helpOverlay_->setBounds(getLocalBounds());
    helpOverlay_->setVisible(true);
    helpOverlay_->toFront(true);
    focusRing_.toFront(false);
    helpOverlay_->focusClose();
}

void RoboDrummerAudioProcessorEditor::hideHelp() {
    helpOverlay_->setVisible(false);
    grabKeyboardFocus();
}

void RoboDrummerAudioProcessorEditor::startTour() {
    resized();
    tour_->start();
    tour_->toFront(false);
    focusRing_.toFront(false);
}

bool RoboDrummerAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && isHelpOpen()) { hideHelp(); return true; }
    const bool forward = key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier, 0);
    const bool backward = key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
    if ((forward || backward) && !isHelpOpen()) { selectTab((currentTab_ + (forward ? 1 : TabCount - 1)) % TabCount); return true; }
    return false;
}

void RoboDrummerAudioProcessorEditor::selectTab(int index) {
    currentTab_ = juce::jlimit(0, TabCount - 1, index);
    for (int i = 0; i < TabCount; ++i) panels_[static_cast<std::size_t>(i)]->setVisible(i == currentTab_);
    panels_[static_cast<std::size_t>(currentTab_)]->refresh();
    for (int i = 0; i < TabCount; ++i)
        tabButtons_[static_cast<std::size_t>(i)].setToggleState(i == currentTab_, juce::dontSendNotification);
    if (rdui::uiPrefs().highContrast) rdui::applyHighContrast(*panels_[static_cast<std::size_t>(currentTab_)], true);
    focusRing_.refresh();
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

    helpOverlay_->setBounds(getLocalBounds());
    focusRing_.setBounds(getLocalBounds());
    if (tour_ != nullptr) {
        const int w = juce::jmin(460, getWidth() - 24), h = 138;
        tour_->setBounds(getWidth() - w - 14, getHeight() - h - 30, w, h);
    }
}

void RoboDrummerAudioProcessorEditor::timerCallback() {
    // Keep consuming the audio producer even while another tab is visible or
    // the onset presentation is frozen. Only the message-thread model changes.
    static_cast<rdui::AnalysisPanel&>(*panels_[7]).drainOnsets();
    panels_[static_cast<std::size_t>(currentTab_)]->refresh();
    focusRing_.refresh();
}
