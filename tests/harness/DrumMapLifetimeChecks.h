#pragma once
#include "plugin/EditorPanels.h"

namespace robodrummer::test {

template <typename Check>
void runDrumMapLifetimeChecks(RoboDrummerAudioProcessor& processor, Check&& check) {
    juce::Component window;
    window.setBounds(0, 0, 1000, 700); window.addToDesktop(0); window.setVisible(true);
    auto panel = std::make_unique<rdui::SettingsPanel>(processor, rdui::SettingsPanel::Callbacks{}, rdui::EditorSettings::Values{});
    window.addAndMakeVisible(*panel); panel->setBounds(window.getLocalBounds());
    juce::TextButton* open = nullptr;
    for (auto* child : panel->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getButtonText() == "EDIT DRUM MAP...") open = button;
    check(open != nullptr, "settings exposes the actual drum-map callout action");
    if (open == nullptr) return;
    open->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    juce::Component::SafePointer<rdui::DrumMapEditor> map;
    std::function<void(juce::Component&)> visit = [&](juce::Component& component) {
        if (auto* found = dynamic_cast<rdui::DrumMapEditor*>(&component)) map = found;
        for (auto* child : component.getChildren()) visit(*child);
    };
    auto& desktop = juce::Desktop::getInstance();
    for (int n = 0; n < desktop.getNumComponents(); ++n) visit(*desktop.getComponent(n));
    check(map != nullptr, "drum map opens as an actual asynchronous desktop callout");
    if (map == nullptr) return;
    juce::Component::SafePointer<juce::ComboBox> note;
    for (auto* child : map->getChildren()) if (auto* choice = dynamic_cast<juce::ComboBox*>(child)) { note = choice; break; }
    check(note != nullptr, "drum-map callout contains editable production note controls");
    const int before = processor.getMidiDrumNote(DrumInstrument::Kick);
    panel.reset();
    if (note != nullptr) note->setSelectedId((before + 1) % 128 + 1, juce::sendNotificationSync);
    check(processor.getMidiDrumNote(DrumInstrument::Kick) == before,
          "queued callout edits cannot reach the processor after their owning editor closes");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    check(map == nullptr, "closing settings dismisses and destroys its desktop drum-map callout");
    if (map != nullptr) {
        if (auto* callout = map->template findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    }
    window.removeFromDesktop();
}

} // namespace robodrummer::test
