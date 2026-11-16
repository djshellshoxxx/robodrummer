#pragma once
#include "plugin/EditorWidgets.h"

namespace robodrummer::test {

template <typename Check>
void runParamMenuChecks(RoboDrummerAudioProcessor& processor, Check&& check) {
    auto& state = processor.getParameters();
    const auto contains = [](const rdui::ParamMenuSession& session, int id) {
        juce::PopupMenu::MenuItemIterator items(session.menu);
        while (items.next()) if (items.getItem().itemID == id) return true;
        return false;
    };
    struct Gestures final : juce::AudioProcessorParameter::Listener {
        int values{}, starts{}, ends{};
        void parameterValueChanged(int, float) override { ++values; }
        void parameterGestureChanged(int, bool starting) override { if (starting) ++starts; else ++ends; }
        void clear() { values = starts = ends = 0; }
    };
    juce::Component window;
    window.setBounds(0, 0, 420, 200); window.addToDesktop(0); window.setVisible(true);
    rdui::ParamSlider slider(state, "humanize", "Humanize", "Timing variation");
    rdui::ParamCombo combo(state, "outputMode", "Output", "Drum output mode");
    rdui::ParamToggle toggle(state, "jamMemoryEnabled", "Jam memory", "Learn this jam");
    for (auto* control : std::initializer_list<juce::Component*>{&slider, &combo, &toggle}) window.addAndMakeVisible(control);
    slider.setBounds(10, 10, 390, 40); combo.setBounds(10, 60, 390, 40); toggle.setBounds(10, 110, 390, 40);
    auto* parameter = state.getParameter("humanize");
    Gestures gestures; parameter->addListener(&gestures);
    rdui::setParameterPlain(parameter, 0.3f); gestures.clear();
    auto menu = rdui::makeParamContextMenu(slider.slider(), "humanize", &processor, slider.menuActions());
    for (const auto* invalid : {"0.2.3", "0.4garbage", "1e", "nan", "inf", "", "-1", "2"}) {
        const float before = parameter->getValue();
        juce::SystemClipboard::copyTextToClipboard(invalid); menu.complete(3);
        check(parameter->getValue() == before && gestures.starts == 0 && gestures.ends == 0 && gestures.values == 0,
              "invalid slider menu paste emits no parameter value or host gesture");
    }
    juce::SystemClipboard::copyTextToClipboard("0.75"); menu.complete(3);
    check(std::abs(parameter->getValue() - 0.75f) < 0.001f && gestures.starts == 1 && gestures.ends == 1,
          "slider menu paste emits a balanced host gesture");
    menu.complete(2);
    check(juce::SystemClipboard::getTextFromClipboard() == "0.7500", "slider Copy uses the actual current value");
    gestures.clear(); menu.complete(1);
    check(parameter->getValue() == parameter->getDefaultValue() && gestures.starts == 1 && gestures.ends == 1,
          "slider menu reset emits a balanced host gesture");
    parameter->removeListener(&gestures);

    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter("outputMode"));
    menu = rdui::makeParamContextMenu(combo.combo(), "outputMode", &processor, combo.menuActions());
    choice->addListener(&gestures); gestures.clear();
    juce::SystemClipboard::copyTextToClipboard("not an output mode"); menu.complete(3);
    check(gestures.starts == 0 && gestures.ends == 0 && gestures.values == 0, "invalid combo paste has no host gesture");
    rdui::setParameterPlain(choice, 0.0f); gestures.clear();
    juce::SystemClipboard::copyTextToClipboard(choice->choices[1]); menu.complete(3);
    check(choice->getIndex() == 1 && gestures.starts == 1 && gestures.ends == 1, "combo menu changes the actual host parameter");
    menu.complete(2);
    check(juce::SystemClipboard::getTextFromClipboard() == choice->choices[1], "combo Copy uses its current display text");
    menu.complete(1);
    auto* choiceParameter = state.getParameter("outputMode");
    check(choiceParameter->getValue() == choiceParameter->getDefaultValue(), "combo Reset restores its host parameter default");
    choice->removeListener(&gestures);
    auto* boolean = state.getParameter("jamMemoryEnabled");
    boolean->addListener(&gestures); gestures.clear();
    menu = rdui::makeParamContextMenu(toggle, "jamMemoryEnabled", &processor, toggle.menuActions());
    juce::SystemClipboard::copyTextToClipboard("maybe"); menu.complete(3);
    check(gestures.starts == 0 && gestures.ends == 0 && gestures.values == 0, "invalid toggle paste has no host gesture");
    rdui::setParameterPlain(boolean, 1.0f); gestures.clear();
    juce::SystemClipboard::copyTextToClipboard("off"); menu.complete(3);
    check(boolean->getValue() == 0.0f && gestures.starts == 1 && gestures.ends == 1, "toggle menu changes the actual host parameter");
    menu.complete(2);
    check(juce::SystemClipboard::getTextFromClipboard() == "Off", "toggle Copy uses the actual current state");
    menu.complete(1);
    check(boolean->getValue() == boolean->getDefaultValue(), "toggle Reset restores its host parameter default");
    boolean->removeListener(&gestures);

    const int learn = rdui::findLearnTarget("jamMemoryEnabled");
    check(contains(menu, 10) && !contains(menu, 11) && !contains(menu, 12), "idle unbound learn target offers only MIDI Learn");
    menu.complete(10); check(processor.getMidiLearn().armedTarget() == learn, "menu MIDI Learn arms the actual target");
    menu = rdui::makeParamContextMenu(toggle, "jamMemoryEnabled", &processor, toggle.menuActions());
    check(contains(menu, 11) && !contains(menu, 10), "armed learn target offers Cancel rather than a duplicate Learn action");
    menu.complete(11); check(processor.getMidiLearn().armedTarget() < 0, "menu cancels actual MIDI Learn");
    processor.getMidiLearn().bindCc(74, learn);
    menu = rdui::makeParamContextMenu(toggle, "jamMemoryEnabled", &processor, toggle.menuActions());
    check(contains(menu, 12), "Clear MIDI mapping appears only for an existing binding");
    menu.complete(12); check(processor.getMidiLearn().ccForTarget(learn) < 0, "menu clears the actual MIDI binding");
    auto nonLearnable = rdui::makeParamContextMenu(toggle, "mute_kick", &processor, toggle.menuActions());
    check(nonLearnable.menu.getNumItems() == 3, "nonlearnable parameters omit all MIDI items");

    // Open actual asynchronous popups through both supported gestures.
    for (auto* control : std::initializer_list<juce::Component*>{&slider, &combo, &toggle}) {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        juce::MouseEvent rightClick(source, {5.0f,5.0f}, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, control, control, juce::Time::getCurrentTime(), {5.0f,5.0f}, juce::Time::getCurrentTime(), 1, false);
        control->mouseDown(rightClick);
        check(juce::PopupMenu::dismissAllActiveMenus(), "right click opens the real parameter popup");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        const bool handled = control->keyPressed(juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0));
        check(handled && juce::PopupMenu::dismissAllActiveMenus(), "Shift+F10 opens the real parameter popup");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    // Select an actual native popup item, so the asynchronous JUCE dispatch
    // path is covered in addition to the production session's direct actions.
    rdui::setParameterPlain(parameter, parameter->getDefaultValue() > 0.5f ? 0.2f : 0.8f);
    parameter->addListener(&gestures); gestures.clear();
    slider.showMenu();
    juce::Component::SafePointer<juce::Component> resetItem;
    std::function<void(juce::Component&)> findReset = [&](juce::Component& component) {
        if (auto* handler = component.getAccessibilityHandler(); handler != nullptr && handler->getTitle() == "Reset to default")
            resetItem = &component;
        for (auto* child : component.getChildren()) findReset(*child);
    };
    if (auto* popup = juce::Component::getCurrentlyModalComponent()) findReset(*popup);
    const bool invoked = resetItem != nullptr && resetItem->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
    check(invoked, "actual native popup exposes an actionable Reset menu item");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while ((parameter->getValue() != parameter->getDefaultValue() || gestures.ends == 0)
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    check(parameter->getValue() == parameter->getDefaultValue() && gestures.starts == 1 && gestures.ends == 1,
          "actual popup item dispatch resets the host parameter with a balanced gesture");
    parameter->removeListener(&gestures);
    juce::PopupMenu::dismissAllActiveMenus();
    auto destroyed = std::make_unique<rdui::ParamToggle>(state, "jamMemoryEnabled", "Memory", "Learn the jam");
    window.addAndMakeVisible(*destroyed); destroyed->setBounds(10, 160, 390, 30);
    auto pending = rdui::makeParamContextMenu(*destroyed, "jamMemoryEnabled", &processor, destroyed->menuActions());
    const auto beforeDestroy = boolean->getValue();
    destroyed->showMenu();
    destroyed.reset();
    juce::PopupMenu::dismissAllActiveMenus();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    for (int action : {1, 2, 3, 10, 11, 12}) pending.complete(action);
    check(boolean->getValue() == beforeDestroy && processor.getMidiLearn().armedTarget() < 0,
          "asynchronous menu results after widget destruction change no parameter or mapping");
    window.removeFromDesktop();
}

} // namespace robodrummer::test
