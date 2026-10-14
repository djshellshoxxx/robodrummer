// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <JuceHeader.h>
#include <memory>

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

inline juce::Label* styleCaption(juce::Label& label, const juce::String& text, float size = 13.0f, juce::Colour colour = kDim) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(size));
    label.setColour(juce::Label::textColourId, colour);
    label.setMinimumHorizontalScale(0.7f);
    return &label;
}

// A labelled slider bound to a host parameter.
class ParamSlider final : public juce::Component {
public:
    ParamSlider(APVTS& state, const char* id, const juce::String& caption, const juce::String& tooltip,
                juce::Slider::SliderStyle style = juce::Slider::LinearHorizontal, int textBoxWidth = 74)
        : style_(style) {
        styleCaption(label_, caption);
        addAndMakeVisible(label_);
        slider_.setSliderStyle(style);
        slider_.setTextBoxStyle(style != juce::Slider::LinearHorizontal ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight,
                                false, textBoxWidth, 22);
        slider_.setTooltip(tooltip);
        label_.setTooltip(tooltip);
        addAndMakeVisible(slider_);
        attachment_ = std::make_unique<APVTS::SliderAttachment>(state, id, slider_);
        if (auto* parameter = state.getParameter(id))
            slider_.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
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
private:
    juce::Slider::SliderStyle style_;
    juce::Label label_;
    juce::Slider slider_;
    std::unique_ptr<APVTS::SliderAttachment> attachment_;
};

class ParamCombo final : public juce::Component {
public:
    ParamCombo(APVTS& state, const char* id, const juce::String& caption, const juce::String& tooltip) {
        styleCaption(label_, caption);
        addAndMakeVisible(label_);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id)))
            combo_.addItemList(choice->choices, 1); // item ids are choice index + 1, as ComboBoxAttachment expects
        combo_.setTooltip(tooltip);
        label_.setTooltip(tooltip);
        addAndMakeVisible(combo_);
        attachment_ = std::make_unique<APVTS::ComboBoxAttachment>(state, id, combo_);
    }
    void resized() override {
        auto area = getLocalBounds();
        label_.setBounds(area.removeFromLeft(juce::jmin(130, area.getWidth() / 2)));
        combo_.setBounds(area.reduced(0, 2));
    }
    juce::ComboBox& combo() noexcept { return combo_; }
private:
    juce::Label label_;
    juce::ComboBox combo_;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment_;
};

class ParamToggle final : public juce::ToggleButton {
public:
    ParamToggle(APVTS& state, const char* id, const juce::String& caption, const juce::String& tooltip)
        : juce::ToggleButton(caption) {
        setTooltip(tooltip);
        attachment_ = std::make_unique<APVTS::ButtonAttachment>(state, id, *this);
    }
private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment_;
};

// Horizontal level/confidence bar with an optional marker line.
class BarMeter final : public juce::Component {
public:
    void setValue(float v) { value_ = juce::jlimit(0.0f, 1.0f, v); repaint(); }
    void setMarker(float m) { marker_ = m; }
    void setColour(juce::Colour c) { colour_ = c; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(kPanelRaised);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(colour_);
        g.fillRoundedRectangle(r.withWidth(r.getWidth() * value_), 3.0f);
        if (marker_ >= 0.0f) {
            g.setColour(kWarn);
            const float x = r.getX() + r.getWidth() * juce::jlimit(0.0f, 1.0f, marker_);
            g.drawLine(x, r.getY(), x, r.getBottom(), 2.0f);
        }
    }
private:
    float value_{0.0f};
    float marker_{-1.0f};
    juce::Colour colour_{kAccentAlt};
};

inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title) {
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    if (title.isNotEmpty()) {
        g.setColour(kAccent);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(), bounds.getX() + 12, bounds.getY() + 6, bounds.getWidth() - 24, 18, juce::Justification::centredLeft);
    }
}

} // namespace rdui
