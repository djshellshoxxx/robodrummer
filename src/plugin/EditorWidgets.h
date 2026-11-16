// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <sstream>
#include <locale>
#include "PluginProcessor.h"

namespace rdui {

using APVTS = juce::AudioProcessorValueTreeState;

// Circuit Drift Labs palette (docs/standards/CDL_PLUGIN_BASELINE.md section 6.1).
inline const juce::Colour kBackground { 0xff0e1116 };
inline const juce::Colour kPanel { 0xff171b22 };
inline const juce::Colour kPanelRaised { 0xff222833 };
inline const juce::Colour kBorder { 0xff2a303a };
inline const juce::Colour kAccent { 0xffe8532a };
inline const juce::Colour kAccentAlt { 0xff4fb6c4 };
inline const juce::Colour kText { 0xffe6e8ec };
inline const juce::Colour kDim { 0xff8a929e };
inline const juce::Colour kGood { 0xff7bc96f };
inline const juce::Colour kWarn { 0xfff2c14e };
inline const juce::Colour kError { 0xffef5350 };

class RoboLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    RoboLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, kBackground);
        setColour(juce::Label::textColourId, kText);
        setColour(juce::Slider::thumbColourId, kAccent);
        setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.85f));
        setColour(juce::Slider::backgroundColourId, kPanelRaised);
        setColour(juce::Slider::rotarySliderFillColourId, kAccent);
        setColour(juce::Slider::rotarySliderOutlineColourId, kPanelRaised);
        setColour(juce::Slider::textBoxTextColourId, kText);
        setColour(juce::Slider::textBoxBackgroundColourId, kPanelRaised);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId, kPanelRaised);
        setColour(juce::ComboBox::textColourId, kText);
        setColour(juce::ComboBox::outlineColourId, kBorder);
        setColour(juce::ComboBox::arrowColourId, kDim);
        setColour(juce::PopupMenu::backgroundColourId, kPanelRaised);
        setColour(juce::PopupMenu::textColourId, kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent);
        setColour(juce::TextButton::buttonColourId, kPanelRaised);
        setColour(juce::TextButton::buttonOnColourId, kAccent);
        setColour(juce::TextButton::textColourOffId, kText);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::ToggleButton::textColourId, kText);
        setColour(juce::ToggleButton::tickColourId, kAccentAlt);
        setColour(juce::ToggleButton::tickDisabledColourId, kDim);
        setColour(juce::TextEditor::backgroundColourId, kPanel);
        setColour(juce::TextEditor::textColourId, kText);
        setColour(juce::TextEditor::outlineColourId, kBorder);
        setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
        setColour(juce::TooltipWindow::backgroundColourId, kPanelRaised);
        setColour(juce::TooltipWindow::textColourId, kText);
        setColour(juce::TooltipWindow::outlineColourId, kAccent);
    }
};

// Per-user interface preferences (settings.xml). Message thread only.
struct UiPrefs {
    bool reducedMotion{false};
    bool highContrast{false};
};
inline UiPrefs& uiPrefs() { static UiPrefs prefs; return prefs; }

inline juce::Label* styleCaption(juce::Label& label, const juce::String& text, float size = 13.0f, juce::Colour colour = kDim) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(size));
    label.setColour(juce::Label::textColourId, colour);
    label.setMinimumHorizontalScale(0.7f);
    return &label;
}

// High contrast: dim captions become full-strength text. Reversible, walks the whole tree.
inline void applyHighContrast(juce::Component& root, bool on) {
    if (auto* label = dynamic_cast<juce::Label*>(&root)) {
        const auto colour = label->findColour(juce::Label::textColourId);
        if (on && colour == kDim) { label->setColour(juce::Label::textColourId, kText); label->getProperties().set("rdWasDim", true); }
        else if (!on && static_cast<bool>(label->getProperties()["rdWasDim"])) { label->setColour(juce::Label::textColourId, kDim); label->getProperties().remove("rdWasDim"); }
    }
    for (auto* child : root.getChildren()) applyHighContrast(*child, on);
}

// ---- context menu helpers (spec 15) ----------------------------------------------------------------------------------
// Pure parsers, unit-tested by the harness. They never guess: anything unclear is rejected.
inline bool parseChoiceText(const juce::String& raw, const juce::StringArray& choices, int& index) {
    const auto text = raw.trim();
    if (text.isEmpty() || text.length() > 64) return false;
    for (int i = 0; i < choices.size(); ++i)
        if (choices[i].equalsIgnoreCase(text)) { index = i; return true; }
    if (text.length() <= 4 && text.containsOnly("0123456789")) {
        const int value = text.getIntValue();
        if (value >= 0 && value < choices.size()) { index = value; return true; }
    }
    return false;
}

inline bool parseToggleText(const juce::String& raw, bool& value) {
    const auto text = raw.trim().toLowerCase();
    if (text == "on" || text == "true" || text == "1") { value = true; return true; }
    if (text == "off" || text == "false" || text == "0") { value = false; return true; }
    return false;
}

inline bool parseSliderText(const juce::String& raw, double minimum, double maximum, double& value) {
    const auto text = raw.trim();
    if (text.isEmpty() || text.length() >= 24) return false;
    std::istringstream input(text.toStdString());
    input.imbue(std::locale::classic());
    double parsed = 0.0;
    input >> std::noskipws >> parsed;
    if (input.fail() || !input.eof() || !std::isfinite(parsed) || parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

inline int findLearnTarget(const juce::String& id) {
    for (int i = 0; i < robodrummer::kLearnTargetCount; ++i)
        if (id == robodrummer::kLearnTargets[static_cast<std::size_t>(i)].id) return i;
    return -1;
}

inline void setParameterPlain(juce::RangedAudioParameter* parameter, float plain) {
    if (parameter == nullptr) return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    parameter->endChangeGesture();
}

struct ParamMenuActions {
    std::function<void()> reset;
    std::function<juce::String()> copyText;
    std::function<void(const juce::String&)> paste;
};

struct ParamMenuSession {
    juce::PopupMenu menu;
    std::function<void(int)> complete;
};

// The same session drives the asynchronous menu and direct action/gesture tests.
// Items: Reset, Copy, Paste, then only functional MIDI Learn / Cancel / Clear.
inline ParamMenuSession makeParamContextMenu(juce::Component& target, const juce::String& paramId, RoboDrummerAudioProcessor* host, ParamMenuActions actions) {
    const int learn = host != nullptr ? findLearnTarget(paramId) : -1;
    ParamMenuSession session;
    auto& menu = session.menu;
    menu.addItem(1, "Reset to default");
    menu.addItem(2, "Copy value");
    menu.addItem(3, "Paste value");
    if (learn >= 0) {
        menu.addSeparator();
        if (host->getMidiLearn().armedTarget() == learn) menu.addItem(11, "Cancel MIDI Learn");
        else menu.addItem(10, "MIDI Learn");
        if (host->getMidiLearn().ccForTarget(learn) >= 0)
            menu.addItem(12, "Clear MIDI mapping (CC " + juce::String(host->getMidiLearn().ccForTarget(learn)) + ")");
    }
    juce::Component::SafePointer<juce::Component> safe(&target);
    session.complete = [safe, actions, host, learn](int choice) {
        if (safe == nullptr || choice == 0) return;
        if (choice == 1 && actions.reset) actions.reset();
        else if (choice == 2 && actions.copyText) juce::SystemClipboard::copyTextToClipboard(actions.copyText());
        else if (choice == 3 && actions.paste) actions.paste(juce::SystemClipboard::getTextFromClipboard());
        else if (choice == 10 && host != nullptr) host->getMidiLearn().armTarget(learn);
        else if (choice == 11 && host != nullptr) host->getMidiLearn().cancelLearn();
        else if (choice == 12 && host != nullptr) host->getMidiLearn().unbindTarget(learn);
    };
    return session;
}

inline void showParamContextMenu(juce::Component& target, const juce::String& paramId, RoboDrummerAudioProcessor* host, ParamMenuActions actions) {
    auto session = makeParamContextMenu(target, paramId, host, std::move(actions));
    session.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target), std::move(session.complete));
}

inline RoboDrummerAudioProcessor* learnHostOf(APVTS& state) { return dynamic_cast<RoboDrummerAudioProcessor*>(&state.processor); }

inline void describe(juce::Component& c, const juce::String& title, const juce::String& description, const char* paramId = nullptr) {
    c.setTitle(title);
    c.setDescription(description);
    if (paramId != nullptr) c.getProperties().set("paramID", juce::String(paramId));
}

// A labelled slider bound to a host parameter.
class ParamSlider final : public juce::Component {
public:
    ParamSlider(APVTS& state, const char* id, const juce::String& caption, const juce::String& tooltip,
                juce::Slider::SliderStyle style = juce::Slider::LinearHorizontal, int textBoxWidth = 74)
        : style_(style), id_(id), host_(learnHostOf(state)) {
        styleCaption(label_, caption);
        addAndMakeVisible(label_);
        slider_.setSliderStyle(style);
        slider_.setTextBoxStyle(style != juce::Slider::LinearHorizontal ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight,
                                false, textBoxWidth, 22);
        slider_.setTooltip(tooltip);
        label_.setTooltip(tooltip);
        slider_.setWantsKeyboardFocus(true);
        describe(slider_, caption, tooltip, id);
        addAndMakeVisible(slider_);
        attachment_ = std::make_unique<APVTS::SliderAttachment>(state, id, slider_);
        parameter_ = state.getParameter(id);
        if (parameter_ != nullptr) {
            defaultValue_ = parameter_->convertFrom0to1(parameter_->getDefaultValue());
            slider_.setDoubleClickReturnValue(true, defaultValue_);
        }
        slider_.addMouseListener(this, true);
    }
    ~ParamSlider() override { slider_.removeMouseListener(this); }

    // Explicit unit text ("-18.0 dB", "+35 %"). Typed values are parsed back leniently by dropping unit characters.
    void setUnit(const juce::String& unit, int decimals, bool showSign = false) {
        slider_.textFromValueFunction = [unit, decimals, showSign](double v) {
            return juce::String(showSign && v > 0.0005 ? "+" : "") + juce::String(v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        };
        slider_.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.-").getDoubleValue(); };
        slider_.updateText();
    }

    ParamMenuActions menuActions() {
        ParamMenuActions actions;
        juce::Component::SafePointer<ParamSlider> safe(this);
        actions.reset = [safe] { if (safe != nullptr) setParameterPlain(safe->parameter_, static_cast<float>(safe->defaultValue_)); };
        actions.copyText = [safe] { return safe != nullptr ? juce::String(safe->slider_.getValue(), 4) : juce::String(); };
        actions.paste = [safe](const juce::String& raw) {
            if (safe == nullptr) return;
            double value = 0.0;
            auto& slider = safe->slider_;
            if (parseSliderText(raw, slider.getMinimum(), slider.getMaximum(), value))
                setParameterPlain(safe->parameter_, static_cast<float>(value));
        };
        return actions;
    }
    void showMenu() { showParamContextMenu(slider_, id_, host_, menuActions()); }
    // Right-click: reset to default, copy or paste the value (pasted text is range-checked, never silently altered).
    void mouseDown(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) showMenu(); }
    bool keyPressed(const juce::KeyPress& k) override {
        const double range = slider_.getMaximum() - slider_.getMinimum();
        if (k.isKeyCode(juce::KeyPress::backspaceKey) || k.isKeyCode(juce::KeyPress::deleteKey)) { slider_.setValue(defaultValue_, juce::sendNotificationSync); return true; }
        if (k.isKeyCode(juce::KeyPress::pageUpKey)) { slider_.setValue(slider_.getValue() + range * 0.1, juce::sendNotificationSync); return true; }
        if (k.isKeyCode(juce::KeyPress::pageDownKey)) { slider_.setValue(slider_.getValue() - range * 0.1, juce::sendNotificationSync); return true; }
        if (k.isKeyCode(juce::KeyPress::homeKey)) { slider_.setValue(slider_.getMinimum(), juce::sendNotificationSync); return true; }
        if (k.isKeyCode(juce::KeyPress::endKey)) { slider_.setValue(slider_.getMaximum(), juce::sendNotificationSync); return true; }
        if (k == juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)) { showMenu(); return true; }
        return false;
    }
    void resized() override {
        auto area = getLocalBounds();
        if (style_ == juce::Slider::LinearHorizontal) {
            label_.setBounds(area.removeFromLeft(juce::jmin(130, area.getWidth() / 2)));
            slider_.setBounds(area);
        } else {
            label_.setBounds(area.removeFromTop(18));
            slider_.setBounds(area);
        }
    }
    juce::Slider& slider() noexcept { return slider_; }
    juce::Label& caption() noexcept { return label_; }
private:
    juce::Slider::SliderStyle style_;
    juce::String id_;
    RoboDrummerAudioProcessor* host_{nullptr};
    juce::RangedAudioParameter* parameter_{nullptr};
    double defaultValue_{0.0};
    juce::Label label_;
    juce::Slider slider_;
    std::unique_ptr<APVTS::SliderAttachment> attachment_;
};

class ParamCombo final : public juce::Component {
public:
    ParamCombo(APVTS& state, const char* id, const juce::String& caption, const juce::String& tooltip)
        : id_(id), host_(learnHostOf(state)), parameter_(state.getParameter(id)) {
        styleCaption(label_, caption);
        addAndMakeVisible(label_);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter_))
            combo_.addItemList(choice->choices, 1); // item ids are choice index + 1, as ComboBoxAttachment expects
        combo_.setTooltip(tooltip);
        label_.setTooltip(tooltip);
        describe(combo_, caption, tooltip, id);
        addAndMakeVisible(combo_);
        attachment_ = std::make_unique<APVTS::ComboBoxAttachment>(state, id, combo_);
        combo_.addMouseListener(this, true);
    }
    ~ParamCombo() override { combo_.removeMouseListener(this); }
    ParamMenuActions menuActions() {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter_);
        if (choice == nullptr) return {};
        ParamMenuActions actions;
        auto* ranged = parameter_;
        actions.reset = [ranged] { setParameterPlain(ranged, ranged->convertFrom0to1(ranged->getDefaultValue())); };
        actions.copyText = [choice] { return choice->getCurrentChoiceName(); };
        actions.paste = [choice](const juce::String& raw) {
            int index = 0;
            if (parseChoiceText(raw, choice->choices, index)) setParameterPlain(choice, static_cast<float>(index));
        };
        return actions;
    }
    void showMenu() { if (dynamic_cast<juce::AudioParameterChoice*>(parameter_) != nullptr) showParamContextMenu(combo_, id_, host_, menuActions()); }
    void mouseDown(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) showMenu(); }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)) { showMenu(); return true; }
        return false;
    }
    void resized() override {
        auto area = getLocalBounds();
        label_.setBounds(area.removeFromLeft(juce::jmin(130, area.getWidth() / 2)));
        combo_.setBounds(area.reduced(0, 2));
    }
    juce::ComboBox& combo() noexcept { return combo_; }
private:
    juce::String id_;
    RoboDrummerAudioProcessor* host_{nullptr};
    juce::RangedAudioParameter* parameter_{nullptr};
    juce::Label label_;
    juce::ComboBox combo_;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment_;
};

class ParamToggle final : public juce::ToggleButton {
public:
    ParamToggle(APVTS& state, const char* id, const juce::String& caption, const juce::String& tooltip)
        : juce::ToggleButton(caption), id_(id), host_(learnHostOf(state)), parameter_(state.getParameter(id)) {
        setTooltip(tooltip);
        describe(*this, caption, tooltip, id);
        attachment_ = std::make_unique<APVTS::ButtonAttachment>(state, id, *this);
    }
    ParamMenuActions menuActions() {
        if (parameter_ == nullptr) return {};
        ParamMenuActions actions;
        auto* parameter = parameter_;
        actions.reset = [parameter] { setParameterPlain(parameter, parameter->convertFrom0to1(parameter->getDefaultValue())); };
        juce::Component::SafePointer<ParamToggle> safe(this);
        actions.copyText = [safe] { return safe != nullptr && safe->getToggleState() ? juce::String("On") : juce::String("Off"); };
        actions.paste = [parameter](const juce::String& raw) {
            bool value = false;
            if (parseToggleText(raw, value)) setParameterPlain(parameter, value ? 1.0f : 0.0f);
        };
        return actions;
    }
    void showMenu() { if (parameter_ != nullptr) showParamContextMenu(*this, id_, host_, menuActions()); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) { showMenu(); return; } // a right click never toggles
        juce::ToggleButton::mouseDown(e);
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress(juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)) { showMenu(); return true; }
        return juce::ToggleButton::keyPressed(k);
    }
    // Compact: a small push-style button (mute / solo). The state is a shape (filled, thick outline) and the tooltip and
    // accessibility title name the function, not only a colour.
    void setCompact(bool compact) { compact_ = compact; repaint(); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        if (!compact_) { juce::ToggleButton::paintButton(g, over, down); return; }
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();
        g.setColour(on ? kAccent : kPanelRaised);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(on ? kText : kBorder);
        g.drawRoundedRectangle(r, 4.0f, on ? 2.0f : 1.0f);
        g.setColour(on ? juce::Colours::white : kText);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
private:
    bool compact_{false};
    juce::String id_;
    RoboDrummerAudioProcessor* host_{nullptr};
    juce::RangedAudioParameter* parameter_{nullptr};
    std::unique_ptr<APVTS::ButtonAttachment> attachment_;
};

// Horizontal level/confidence bar with an optional marker line. Bipolar mode grows from the centre (gain meters).
// Always paired with a text read-out; the bar itself never carries the only copy of a value.
class BarMeter final : public juce::Component {
public:
    BarMeter() { setInterceptsMouseClicks(false, false); }
    void setValue(float v) {
        const float lo = bipolar_ ? -1.0f : 0.0f;
        const float next = juce::jlimit(lo, 1.0f, std::isfinite(v) ? v : 0.0f);
        if (next != value_) { value_ = next; repaint(); }
    }
    void setBipolar(bool b) { bipolar_ = b; }
    void setMarker(float m) { marker_ = m; }
    void setColour(juce::Colour c) { colour_ = c; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(kPanelRaised);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(colour_);
        if (bipolar_) {
            const float mid = r.getCentreX();
            const float w = value_ * r.getWidth() * 0.5f;
            g.fillRect(juce::Rectangle<float>(w >= 0 ? mid : mid + w, r.getY(), std::abs(w), r.getHeight()));
            g.setColour(kText);
            g.drawLine(mid, r.getY(), mid, r.getBottom(), 1.0f);
        } else {
            g.fillRoundedRectangle(r.withWidth(r.getWidth() * value_), 3.0f);
        }
        if (marker_ >= 0.0f) {
            g.setColour(kWarn);
            const float x = r.getX() + r.getWidth() * juce::jlimit(0.0f, 1.0f, marker_);
            g.drawLine(x, r.getY(), x, r.getBottom(), 2.0f);
        }
    }
private:
    float value_{0.0f};
    float marker_{-1.0f};
    bool bipolar_{false};
    juce::Colour colour_{kAccentAlt};
};

inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) {
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    if (uiPrefs().highContrast) {
        g.setColour(kText);
        g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 8.0f, 2.0f);
    }
    if (title.isNotEmpty()) {
        g.setColour(kAccent);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(), bounds.getX() + 12, bounds.getY() + 6, bounds.getWidth() - 24, 18, juce::Justification::centredLeft);
    }
}

// Visible keyboard focus: a 2 px ring (a shape, not just a colour) around whichever control holds keyboard focus.
// One overlay for the whole editor so every custom and stock widget gets the same cue.
class FocusRing final : public juce::Component, private juce::FocusChangeListener {
public:
    FocusRing() {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        juce::Desktop::getInstance().addFocusChangeListener(this);
    }
    ~FocusRing() override { juce::Desktop::getInstance().removeFocusChangeListener(this); }
    void setTarget(juce::Component* c) { target_ = c; refresh(); }
    juce::Component* getTarget() const { return target_.getComponent(); }
    void refresh() {
        juce::Rectangle<int> next;
        auto* t = target_.getComponent();
        if (t != nullptr && getParentComponent() != nullptr && getParentComponent()->isParentOf(t) && visibleWithin(t, getParentComponent()))
            next = getLocalArea(t, t->getLocalBounds()).expanded(2);
        if (next != ring_) { ring_ = next; repaint(); }
    }
    juce::Rectangle<int> getRing() const { return ring_; }
    // True when the component and every ancestor up to (not including) the editor are visible.
    static bool visibleWithin(const juce::Component* c, const juce::Component* root) {
        for (; c != nullptr && c != root; c = c->getParentComponent()) if (!c->isVisible()) return false;
        return c == root;
    }
    void paint(juce::Graphics& g) override {
        if (ring_.isEmpty()) return;
        g.setColour(kAccentAlt);
        g.drawRoundedRectangle(ring_.toFloat(), 4.0f, uiPrefs().highContrast ? 3.0f : 2.0f);
    }
private:
    void globalFocusChanged(juce::Component* focused) override {
        const bool inside = focused != nullptr && getParentComponent() != nullptr && getParentComponent()->isParentOf(focused) && focused != this;
        setTarget(inside ? focused : nullptr);
    }
    juce::Component::SafePointer<juce::Component> target_;
    juce::Rectangle<int> ring_;
};

} // namespace rdui
