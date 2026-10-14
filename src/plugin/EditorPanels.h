// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#pragma once
#include "EditorWidgets.h"
#include "PluginProcessor.h"
#include "core/StyleLibrary.h"
#include <deque>

namespace rdui {

using robodrummer::MidiCommand;

class PanelBase : public juce::Component {
public:
    virtual void refresh() {}
};

// Stacks components top to bottom inside an area.
inline void stack(juce::Rectangle<int>& area, int rowHeight, int gap, std::initializer_list<juce::Component*> items) {
    for (auto* item : items) {
        item->setBounds(area.removeFromTop(rowHeight));
        area.removeFromTop(gap);
    }
}

// whole(float) prints full precision rather than rounding, so whole numbers go through here.
inline juce::String whole(double value) { return juce::String(juce::roundToInt(value)); }

inline void rowOfButtons(juce::Rectangle<int> area, int gap, std::initializer_list<juce::Component*> items) {
    const int count = static_cast<int>(items.size());
    if (count == 0) return;
    const int width = (area.getWidth() - gap * (count - 1)) / count;
    for (auto* item : items) {
        item->setBounds(area.removeFromLeft(width));
        area.removeFromLeft(gap);
    }
}

inline juce::String phraseName(robodrummer::PhraseState state) {
    switch (state) {
        case robodrummer::PhraseState::Build: return "BUILD";
        case robodrummer::PhraseState::Release: return "RELEASE";
        case robodrummer::PhraseState::Break: return "BREAK";
        case robodrummer::PhraseState::Stable: break;
    }
    return "stable";
}

inline juce::String movementName(robodrummer::TempoMovement movement) {
    using M = robodrummer::TempoMovement;
    switch (movement) {
        case M::Steady: return "steady";
        case M::Jitter: return "micro-timing jitter";
        case M::Drift: return "slow drift";
        case M::Accelerating: return "ACCELERATING";
        case M::Decelerating: return "DECELERATING";
        case M::AbruptShift: return "ABRUPT TEMPO SHIFT";
        case M::HalfDoubleReinterpretation: return "half/double reinterpretation";
    }
    return "steady";
}

inline juce::String coordinationName(robodrummer::JamCoordinationState state) {
    using S = robodrummer::JamCoordinationState;
    switch (state) {
        case S::StableJam: return "STABLE JAM";
        case S::Building: return "BUILDING";
        case S::Releasing: return "RELEASING";
        case S::AwaitingCue: return "AWAITING CUE";
        case S::TransitionLikely: return "TRANSITION LIKELY";
        case S::Break: return "BREAK";
        case S::SoloSupport: return "SOLO SUPPORT";
        case S::EndingLikely: return "ENDING LIKELY";
        case S::EstablishingGroove: break;
    }
    return "establishing groove";
}

// ====================================================================================================================
// LIVE
// ====================================================================================================================
class LivePanel final : public PanelBase {
public:
    explicit LivePanel(RoboDrummerAudioProcessor& p)
        : processor_(p), apvts_(p.getParameters()),
          mode_(apvts_, robodrummer::ParamIDs::leadershipMode, "Timing mode", "Who owns the clock: the host/internal tempo (Drummer Leads), a blend (Hybrid) or the live guitar (Guitarist Leads)."),
          tempoSource_(apvts_, robodrummer::ParamIDs::tempoSource, "Tempo source", "Where the base tempo comes from: the host transport, the internal BPM only, or incoming MIDI clock."),
          style_(apvts_, robodrummer::ParamIDs::jamStyle, "Style", "Groove family. Programmed sections change this automatically."),
          kit_(apvts_, robodrummer::ParamIDs::kit, "Kit", "Built-in kits or your own WAV/AIFF folder (set it on the KIT tab)."),
          drummer_(apvts_, robodrummer::ParamIDs::drummer, "Drummer", "The drummer's personality: tight, laid-back, aggressive, busy, minimal..."),
          bpm_(apvts_, robodrummer::ParamIDs::bpm, "Internal BPM", "Tempo used when the host tempo is unavailable or the tempo source is Internal (40-240)."),
          leadership_(apvts_, robodrummer::ParamIDs::leadership, "Leadership", "0% = drummer leads completely, 100% = the guitarist leads. Used in Hybrid mode."),
          followRange_(apvts_, robodrummer::ParamIDs::followRange, "Follow range", "Maximum distance in BPM the drummer may move away from the base tempo."),
          followResponse_(apvts_, robodrummer::ParamIDs::followResponse, "Follow response", "How quickly tempo and beat corrections are applied. Very Stable resists change; Very Responsive chases it."),
          intensity_(apvts_, robodrummer::ParamIDs::intensity, "Intensity", "Base drummer intensity before guitar dynamics, silence and coordinator adjustments."),
          complexity_(apvts_, robodrummer::ParamIDs::complexity, "Complexity", "Syncopation, ghost notes and kick patterns. 50% is the style's own feel."),
          fillLevel_(apvts_, robodrummer::ParamIDs::fillFrequency, "Fill level", "How often automatic fills happen. 50% is the style's own cadence.") {
        for (auto* c : std::initializer_list<juce::Component*>{ &mode_, &tempoSource_, &style_, &kit_, &drummer_, &bpm_, &leadership_, &followRange_,
                                                               &followResponse_, &intensity_, &complexity_, &fillLevel_ })
            addAndMakeVisible(*c);

        tap_.setTooltip("Tap the beat to set the internal BPM.");
        tap_.onClick = [this] { processor_.tapTempo(); };
        addAndMakeVisible(tap_);

        tempoBig_.setFont(juce::FontOptions(46.0f, juce::Font::bold));
        tempoBig_.setColour(juce::Label::textColourId, kText);
        tempoBig_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(tempoBig_);
        for (auto* l : { &beatBar_, &section_, &tracker_, &guitar_, &status_ }) {
            l->setFont(juce::FontOptions(14.0f));
            l->setColour(juce::Label::textColourId, kText);
            l->setMinimumHorizontalScale(0.7f);
            addAndMakeVisible(*l);
        }
        for (auto* l : { &tempoCaption_, &beatCaption_, &downCaption_, &levelCaption_ }) addAndMakeVisible(*l);
        styleCaption(tempoCaption_, "Tempo confidence");
        styleCaption(beatCaption_, "Beat confidence");
        styleCaption(downCaption_, "Downbeat confidence");
        styleCaption(levelCaption_, "Guitar input");
        for (auto* m : { &tempoConf_, &beatConf_, &downConf_, &level_ }) addAndMakeVisible(*m);
        beatConf_.setColour(kAccent);
        downConf_.setColour(kWarn);
        level_.setColour(kGood);

        struct Spec { const char* text; MidiCommand command; const char* tip; };
        const std::array<Spec, 14> specs {{
            { "LISTEN", MidiCommand::ResetListening, "Clear tempo, phase, phrase and silence tracking and start listening to the guitar again (MIDI note 47)." },
            { "FILL", MidiCommand::Fill, "Queue a fill for the next suitable bar position (MIDI note 36)." },
            { "FILL NOW", MidiCommand::ImmediateFill, "Start a fill on the next beat (MIDI note 50)." },
            { "CRASH", MidiCommand::Crash, "Hit a crash cymbal now (MIDI note 39)." },
            { "BREAK", MidiCommand::Break, "Drop out for one bar, then re-enter (MIDI note 44)." },
            { "HALF-TIME", MidiCommand::HalfTime, "Toggle half-time feel (MIDI note 42)." },
            { "DOUBLE-TIME", MidiCommand::DoubleTime, "Toggle double-time feel (MIDI note 43)." },
            { "STOP", MidiCommand::Stop, "Stop the drummer until RESUME (MIDI note 45)." },
            { "RESUME", MidiCommand::Resume, "Resume the drummer (MIDI note 46)." },
            { "PREV SECTION", MidiCommand::PreviousSection, "Previous arrangement section (MIDI note 38)." },
            { "NEXT SECTION", MidiCommand::NextSection, "Next arrangement section, or cue a transition in free jam (MIDI note 37)." },
            { "INTENSITY +", MidiCommand::IntensityUp, "Raise the intensity by 10% (MIDI note 40)." },
            { "INTENSITY -", MidiCommand::IntensityDown, "Lower the intensity by 10% (MIDI note 41)." },
            { "SOLO / END", MidiCommand::SoloSupport, "Cue solo support. Hold SHIFT while clicking to cue the end of the jam instead (MIDI notes 48 / 49)." }
        }};
        for (std::size_t i = 0; i < specs.size(); ++i) {
            auto& b = buttons_[i];
            b.setButtonText(specs[i].text);
            b.setTooltip(specs[i].tip);
            const auto command = specs[i].command;
            b.onClick = [this, command] {
                if (command == MidiCommand::SoloSupport && juce::ModifierKeys::currentModifiers.isShiftDown())
                    processor_.requestCommand(MidiCommand::EndJam);
                else
                    processor_.requestCommand(command);
            };
            addAndMakeVisible(b);
        }
        buttons_[0].setColour(juce::TextButton::buttonColourId, kAccent.darker(0.3f));
        refresh();
    }

    void paint(juce::Graphics& g) override {
        drawPanel(g, timingArea_, "Timing");
        drawPanel(g, soundArea_, "Sound & feel");
        drawPanel(g, readoutArea_, "Now playing");
        drawPanel(g, performArea_, "Perform");
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto left = area.removeFromLeft(area.getWidth() * 47 / 100);
        area.removeFromLeft(8);
        timingArea_ = left.removeFromTop(left.getHeight() * 52 / 100);
        left.removeFromTop(8);
        soundArea_ = left;
        auto t = timingArea_.reduced(12, 6); t.removeFromTop(22);
        stack(t, 28, 3, { &mode_, &tempoSource_, &bpm_, &leadership_, &followRange_, &followResponse_ });
        auto bpmRow = bpm_.getBounds();
        tap_.setBounds(bpmRow.removeFromRight(56)); bpmRow.removeFromRight(6);
        bpm_.setBounds(bpmRow);
        auto s = soundArea_.reduced(12, 6); s.removeFromTop(22);
        stack(s, 28, 3, { &style_, &kit_, &drummer_, &intensity_, &complexity_, &fillLevel_ });

        readoutArea_ = area.removeFromTop(area.getHeight() * 47 / 100);
        area.removeFromTop(8);
        performArea_ = area;
        auto r = readoutArea_.reduced(12, 6); r.removeFromTop(20);
        tempoBig_.setBounds(r.removeFromTop(52));
        stack(r, 20, 1, { &beatBar_, &section_, &tracker_, &guitar_ });
        r.removeFromTop(2);
        for (auto [caption, meter] : { std::pair<juce::Component*, juce::Component*>{ &tempoCaption_, &tempoConf_ }, { &beatCaption_, &beatConf_ }, { &downCaption_, &downConf_ }, { &levelCaption_, &level_ } }) {
            auto line = r.removeFromTop(17);
            caption->setBounds(line.removeFromLeft(130));
            meter->setBounds(line.reduced(0, 4));
        }
        auto p = performArea_.reduced(12, 6); p.removeFromTop(22);
        status_.setBounds(p.removeFromBottom(22));
        const int rows = 5, cols = 3;
        const int cellW = (p.getWidth() - 8 * (cols - 1)) / cols;
        const int cellH = (p.getHeight() - 6 * (rows - 1)) / rows;
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
            buttons_[i].setBounds(p.getX() + col * (cellW + 8), p.getY() + row * (cellH + 6), cellW, cellH);
        }
    }

    void refresh() override {
        const auto timeScale = processor_.getTimeScale();
        buttons_[5].setToggleState(timeScale < 0.75, juce::dontSendNotification);
        buttons_[6].setToggleState(timeScale > 1.5, juce::dontSendNotification);
        buttons_[7].setToggleState(processor_.isDrummerStopped(), juce::dontSendNotification);
        for (std::size_t i : { 5u, 6u, 7u }) buttons_[i].setClickingTogglesState(false);

        juce::String feel;
        if (timeScale < 0.75) feel = "  HALF-TIME";
        else if (timeScale > 1.5) feel = "  DOUBLE-TIME";
        tempoBig_.setText(juce::String(processor_.getEffectiveDrummerBpm(), 1) + " BPM" + feel, juce::dontSendNotification);

        beatBar_.setText("Bar " + juce::String(processor_.getDrummerBarNumber()) + "   Beat " + juce::String(processor_.getDrummerBeatInBar()) + " / " +
                         juce::String(processor_.getEffectiveMeterNumerator()) + "   (" + juce::String(processor_.getEffectiveMeterNumerator()) + "/" +
                         juce::String(processor_.getEffectiveMeterDenominator()) + ")" +
                         (processor_.isDrummerStopped() ? "   STOPPED" : ""), juce::dontSendNotification);
        if (processor_.isArrangementEnabled()) {
            const int slot = processor_.getCurrentArrangementSection();
            auto name = processor_.getArrangementName(slot);
            section_.setText("Section: " + juce::String(slot + 1) + (name.isNotEmpty() ? " - " + name : juce::String()), juce::dontSendNotification);
        } else {
            section_.setText("Section: free jam   |   " + coordinationName(processor_.getJamCoordinationState()), juce::dontSendNotification);
        }
        const bool guitarMode = processor_.getLeadershipMode() != robodrummer::LeadershipMode::DrummerLeads;
        tracker_.setText(juce::String("Tracker: ") + (processor_.isGuitarTrackerLocked() ? "LOCKED" : "acquiring") +
                         (guitarMode ? (processor_.hasAdaptiveJoined() ? "  |  drummer joined" : "  |  waiting for a reliable beat to join") : "  |  guitar tracking is informational"),
                         juce::dontSendNotification);
        guitar_.setText("Guitar: " + juce::String(processor_.getDetectedGuitarBpm(), 1) + " BPM  |  authority " +
                        whole(processor_.getEffectiveGuitarAuthority() * 100.0f) + "%  |  dynamics " +
                        whole(processor_.getDetectedGuitarIntensity() * 100.0f) + "% -> drummer " +
                        whole(processor_.getEffectiveDrummerIntensity() * 100.0f) + "%", juce::dontSendNotification);
        tempoConf_.setValue(processor_.getGuitarTempoConfidence());
        beatConf_.setValue(processor_.getGuitarBeatConfidence());
        downConf_.setValue(processor_.getGuitarDownbeatConfidence());
        level_.setValue(juce::jlimit(0.0f, 1.0f, std::sqrt(processor_.getInputPeak())));

        const auto transport = processor_.getLastTransport();
        juce::String text;
        if (processor_.isMidiClockActive() && processor_.getParameters().getRawParameterValue(robodrummer::ParamIDs::tempoSource)->load() > 1.5f)
            text = "Tempo: MIDI clock " + juce::String(processor_.getMidiClockBpm(), 1) + " BPM";
        else if (transport.validTempo)
            text = juce::String("Transport: host | ") + (transport.playing ? "playing" : "stopped") + " | " + juce::String(transport.bpm, 1) + " BPM";
        else
            text = "Transport: internal tempo";
        if (processor_.isWaitingForGuitarResume()) text += "   |   WAITING FOR GUITAR";
        status_.setText(text, juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, kDim);
        repaintDirty();
    }

private:
    void repaintDirty() {}

    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamCombo mode_, tempoSource_, style_, kit_, drummer_;
    ParamSlider bpm_, leadership_, followRange_;
    ParamCombo followResponse_;
    ParamSlider intensity_, complexity_, fillLevel_;
    juce::TextButton tap_{"TAP"};
    juce::Label tempoBig_, beatBar_, section_, tracker_, guitar_, status_;
    juce::Label tempoCaption_, beatCaption_, downCaption_, levelCaption_;
    BarMeter tempoConf_, beatConf_, downConf_, level_;
    std::array<juce::TextButton, 14> buttons_;
    juce::Rectangle<int> timingArea_, soundArea_, readoutArea_, performArea_;
};

// ====================================================================================================================
// KIT (sounds + mixer)
// ====================================================================================================================
class MixerStrip final : public juce::Component {
public:
    MixerStrip(APVTS& state, std::size_t index)
        : index_(index),
          fader_(state, robodrummer::ParamIDs::mixGain[index], robodrummer::kMixerChannelNames[index], juce::String("Level of the ") + robodrummer::kMixerChannelNames[index] + " channel in dB.", juce::Slider::LinearVertical, 62),
          mute_(state, robodrummer::ParamIDs::mixMute[index], "M", "Mute this channel.") {
        addAndMakeVisible(fader_);
        if (index < 7) {
            pan_ = std::make_unique<ParamSlider>(state, robodrummer::ParamIDs::mixPan[index], "Pan", "Stereo position of this channel.", juce::Slider::RotaryHorizontalVerticalDrag, 54);
            addAndMakeVisible(*pan_);
        }
        addAndMakeVisible(mute_);
        if (index < 6) {
            solo_ = std::make_unique<ParamToggle>(state, robodrummer::ParamIDs::mixSolo[index], "S", "Solo this channel.");
            addAndMakeVisible(*solo_);
        }
    }
    void resized() override {
        auto area = getLocalBounds().reduced(3);
        auto buttons = area.removeFromBottom(26);
        if (solo_) { mute_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2)); solo_->setBounds(buttons); }
        else mute_.setBounds(buttons);
        if (pan_) pan_->setBounds(area.removeFromBottom(86));
        fader_.setBounds(area);
    }
private:
    std::size_t index_;
    ParamSlider fader_;
    std::unique_ptr<ParamSlider> pan_;
    ParamToggle mute_;
    std::unique_ptr<ParamToggle> solo_;
};

class KitPanel final : public PanelBase {
public:
    explicit KitPanel(RoboDrummerAudioProcessor& p)
        : processor_(p), apvts_(p.getParameters()),
          kit_(apvts_, robodrummer::ParamIDs::kit, "Kit", "Built-in synthesised kits, or your own folder of WAV/AIFF samples."),
          output_(apvts_, robodrummer::ParamIDs::outputMode, "Output mode", "Internal drum audio, generated MIDI on channel 10, or both."),
          room_(apvts_, robodrummer::ParamIDs::roomSize, "Room size", "Size of the room reverb that feeds the Room channel."),
          tune_(apvts_, robodrummer::ParamIDs::kitTune, "Kit tuning", "Pitch of every drum in semitones (-12 to +12). Applies to built-in and user kits.") {
        addAndMakeVisible(kit_); addAndMakeVisible(output_); addAndMakeVisible(room_); addAndMakeVisible(tune_);
        chooseFolder_.setTooltip("Pick a folder of WAV/AIFF drum samples. Name files like kick.wav, snare_v1.wav .. snare_v3.wav (velocity layers), snare_v2_rr2.wav (round robin), closedhat.wav, openhat.wav, ride.wav, crash.wav, tom1.wav, tom2.wav, floortom.wav.");
        chooseFolder_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Choose a drum sample folder", processor_.getUserKitFolder(), "");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                const auto result = fc.getResult();
                if (result.isDirectory()) {
                    processor_.setUserKitFolder(result);
                    if (auto* parameter = processor_.getParameters().getParameter(robodrummer::ParamIDs::kit)) {
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(robodrummer::kUserKitIndex)));
                        parameter->endChangeGesture();
                    }
                    processor_.requestKitReload();
                }
            });
        };
        addAndMakeVisible(chooseFolder_);
        reload_.setTooltip("Reload the samples from the folder (after adding or renaming files).");
        reload_.onClick = [this] { processor_.requestKitReload(); };
        addAndMakeVisible(reload_);
        for (auto* l : { &folder_, &status_, &routing_ }) {
            l->setFont(juce::FontOptions(13.0f));
            l->setColour(juce::Label::textColourId, kDim);
            l->setMinimumHorizontalScale(0.6f);
            addAndMakeVisible(*l);
        }
        routing_.setText("Multi-output: enable the Kick, Snare, Toms and Hats + Cymbals output buses in your DAW to receive each group separately; "
                         "disabled buses fall back to the main mix.", juce::dontSendNotification);
        routing_.setJustificationType(juce::Justification::topLeft);
        routing_.setMinimumHorizontalScale(1.0f);
        status_.setMinimumHorizontalScale(1.0f);
        for (std::size_t i = 0; i < strips_.size(); ++i) {
            strips_[i] = std::make_unique<MixerStrip>(apvts_, i);
            addAndMakeVisible(*strips_[i]);
        }
        addAndMakeVisible(meter_);
        meter_.setColour(kGood);
        refresh();
    }
    void paint(juce::Graphics& g) override {
        drawPanel(g, topArea_, "Kit");
        drawPanel(g, mixerArea_, "Mixer");
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        topArea_ = area.removeFromTop(184);
        area.removeFromTop(8);
        mixerArea_ = area;
        auto t = topArea_.reduced(12, 6); t.removeFromTop(22);
        auto left = t.removeFromLeft(t.getWidth() / 2 - 6);
        t.removeFromLeft(12);
        stack(left, 28, 3, { &kit_, &output_, &room_, &tune_ });
        auto buttons = left.removeFromTop(28);
        chooseFolder_.setBounds(buttons.removeFromLeft(150)); buttons.removeFromLeft(6);
        reload_.setBounds(buttons.removeFromLeft(90));
        folder_.setBounds(t.removeFromTop(22));
        status_.setBounds(t.removeFromTop(40));
        routing_.setBounds(t);
        auto m = mixerArea_.reduced(10, 6); m.removeFromTop(20);
        meter_.setBounds(m.removeFromBottom(10).reduced(0, 2));
        m.removeFromBottom(4);
        const int w = m.getWidth() / static_cast<int>(strips_.size());
        for (auto& strip : strips_) strip->setBounds(m.removeFromLeft(w));
    }
    void refresh() override {
        const auto folder = processor_.getUserKitFolder();
        folder_.setText("User folder: " + (folder.getFullPathName().isEmpty() ? juce::String("(none chosen)") : folder.getFullPathName()), juce::dontSendNotification);
        status_.setText("Status: " + processor_.getKitStatus(), juce::dontSendNotification);
        meter_.setValue(juce::jlimit(0.0f, 1.0f, std::sqrt(processor_.getOutputPeak())));
    }
private:
    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamCombo kit_, output_;
    ParamSlider room_, tune_;
    juce::TextButton chooseFolder_{"CHOOSE KIT FOLDER..."}, reload_{"RELOAD"};
    juce::Label folder_, status_, routing_;
    std::array<std::unique_ptr<MixerStrip>, robodrummer::kMixerChannelCount> strips_;
    BarMeter meter_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Rectangle<int> topArea_, mixerArea_;
};

// ====================================================================================================================
// DRUMMER
// ====================================================================================================================
class DrummerPanel final : public PanelBase {
public:
    explicit DrummerPanel(RoboDrummerAudioProcessor& p)
        : processor_(p), apvts_(p.getParameters()),
          personality_(apvts_, robodrummer::ParamIDs::drummer, "Personality", "The drummer's character. It shifts feel, complexity, fills and crashes on top of your settings."),
          humanize_(apvts_, robodrummer::ParamIDs::humanize, "Humanize", "Style-aware micro-timing and velocity variation. 0% is machine-exact."),
          feel_(apvts_, robodrummer::ParamIDs::feel, "Feel (push / lay back)", "Shifts the backbeat earlier (negative) or later (positive), in milliseconds."),
          complexity_(apvts_, robodrummer::ParamIDs::complexity, "Complexity", "Syncopation, ghost notes and kick patterns."),
          crash_(apvts_, robodrummer::ParamIDs::crashTendency, "Crash tendency", "Scales how readily the drummer crashes on section starts."),
          fillFrequency_(apvts_, robodrummer::ParamIDs::fillFrequency, "Fill frequency", "How often automatic fills happen."),
          fillLength_(apvts_, robodrummer::ParamIDs::fillLength, "Fill length", "1 beat, 2 beats, 1 bar or 2 bars. Longer fills end on a crash."),
          fillComplexity_(apvts_, robodrummer::ParamIDs::fillComplexity, "Fill complexity", "Density of the fill and whether the kick joins in."),
          fillSurprise_(apvts_, robodrummer::ParamIDs::fillSurprise, "Fill surprise", "0% = classic descending run, 100% = unpredictable voicing.") {
        for (auto* c : std::initializer_list<juce::Component*>{ &personality_, &humanize_, &feel_, &complexity_, &crash_, &fillFrequency_, &fillLength_, &fillComplexity_, &fillSurprise_ })
            addAndMakeVisible(*c);
        fill_.setTooltip("Queue a fill for the next suitable bar position.");
        fillNow_.setTooltip("Start a fill on the next beat.");
        fill_.onClick = [this] { processor_.requestCommand(MidiCommand::Fill); };
        fillNow_.onClick = [this] { processor_.requestCommand(MidiCommand::ImmediateFill); };
        addAndMakeVisible(fill_); addAndMakeVisible(fillNow_);
        note_.setFont(juce::FontOptions(13.0f)); note_.setColour(juce::Label::textColourId, kDim);
        note_.setText("Personality offsets your settings: Laid-back sits behind the beat, Pushing sits ahead, Aggressive adds intensity and crashes, "
                      "Busy plays more and fills more, Minimal stays out of the way, Tight tightens the timing.", juce::dontSendNotification);
        note_.setJustificationType(juce::Justification::topLeft);
        note_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(note_);
    }
    void paint(juce::Graphics& g) override { drawPanel(g, characterArea_, "Character"); drawPanel(g, fillArea_, "Fills"); }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        characterArea_ = area.removeFromLeft(area.getWidth() / 2 - 4);
        area.removeFromLeft(8);
        fillArea_ = area;
        auto c = characterArea_.reduced(12, 6); c.removeFromTop(22);
        stack(c, 30, 4, { &personality_, &humanize_, &feel_, &complexity_, &crash_ });
        note_.setBounds(c.removeFromTop(80));
        auto f = fillArea_.reduced(12, 6); f.removeFromTop(22);
        stack(f, 30, 4, { &fillFrequency_, &fillLength_, &fillComplexity_, &fillSurprise_ });
        f.removeFromTop(8);
        rowOfButtons(f.removeFromTop(36), 8, { &fill_, &fillNow_ });
    }
private:
    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamCombo personality_;
    ParamSlider humanize_, feel_, complexity_, crash_, fillFrequency_;
    ParamCombo fillLength_;
    ParamSlider fillComplexity_, fillSurprise_;
    juce::TextButton fill_{"FILL (QUEUED)"}, fillNow_{"FILL NOW"};
    juce::Label note_;
    juce::Rectangle<int> characterArea_, fillArea_;
};

// ====================================================================================================================
// STYLE
// ====================================================================================================================
class StylePanel final : public PanelBase {
public:
    explicit StylePanel(RoboDrummerAudioProcessor& p)
        : processor_(p), apvts_(p.getParameters()),
          style_(apvts_, robodrummer::ParamIDs::jamStyle, "Style", "The groove family used in free jam. Programmed sections set it automatically."),
          manual_(apvts_, robodrummer::ParamIDs::manualMeterEnabled, "Manual meter", "Override the host time signature with the meter chosen here."),
          numerator_(apvts_, robodrummer::ParamIDs::meterNumerator, "Beats per bar", "Manual time-signature numerator."),
          denominator_(apvts_, robodrummer::ParamIDs::meterDenominator, "Beat unit", "Manual time-signature denominator."),
          memory_(apvts_, robodrummer::ParamIDs::jamMemoryEnabled, "Learn this jam", "Learn recurring tempo, phrase and dynamic tendencies during free jam."),
          silence_(apvts_, robodrummer::ParamIDs::silenceMode, "When guitar stops", "What the drummer does when the guitar goes quiet."),
          silenceBars_(apvts_, robodrummer::ParamIDs::silenceStopBars, "Stop after", "Silent bars before stopping when 'Stop after bars' is selected.") {
        for (auto* c : std::initializer_list<juce::Component*>{ &style_, &manual_, &numerator_, &denominator_, &memory_, &silence_, &silenceBars_ })
            addAndMakeVisible(*c);
        for (auto* l : { &description_, &meterInfo_, &phrase_, &memoryInfo_, &silenceInfo_ }) {
            l->setFont(juce::FontOptions(14.0f));
            l->setColour(juce::Label::textColourId, kDim);
            l->setMinimumHorizontalScale(1.0f);
            l->setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(*l);
        }
        refresh();
    }
    void paint(juce::Graphics& g) override {
        drawPanel(g, styleArea_, "Style");
        drawPanel(g, meterArea_, "Meter");
        drawPanel(g, memoryArea_, "Jam memory");
        drawPanel(g, silenceArea_, "Silence & re-entry");
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto left = area.removeFromLeft(area.getWidth() / 2 - 4);
        area.removeFromLeft(8);
        styleArea_ = left.removeFromTop(left.getHeight() / 2 - 4);
        left.removeFromTop(8);
        meterArea_ = left;
        memoryArea_ = area.removeFromTop(area.getHeight() / 2 - 4);
        area.removeFromTop(8);
        silenceArea_ = area;
        auto s = styleArea_.reduced(12, 6); s.removeFromTop(22);
        style_.setBounds(s.removeFromTop(30)); s.removeFromTop(6);
        description_.setBounds(s);
        auto m = meterArea_.reduced(12, 6); m.removeFromTop(22);
        stack(m, 28, 4, { &manual_, &numerator_, &denominator_ });
        meterInfo_.setBounds(m);
        auto j = memoryArea_.reduced(12, 6); j.removeFromTop(22);
        memory_.setBounds(j.removeFromTop(28)); j.removeFromTop(4);
        memoryInfo_.setBounds(j);
        auto q = silenceArea_.reduced(12, 6); q.removeFromTop(22);
        stack(q, 28, 4, { &silence_, &silenceBars_ });
        silenceInfo_.setBounds(q.removeFromTop(40));
        phrase_.setBounds(q);
    }
    void refresh() override {
        const auto style = robodrummer::styleFor(processor_.getJamStyle());
        static const char* patterns[] = { "standard backbeat", "four-on-the-floor", "one-drop", "half-time", "breakbeat", "experimental", "swing ride pattern" };
        description_.setText("Pattern: " + juce::String(patterns[static_cast<int>(style.pattern)]) +
                             "\nTypical tempo: " + juce::String(juce::roundToInt(style.tempoMinBpm)) + " - " + juce::String(juce::roundToInt(style.tempoMaxBpm)) +
                             " BPM (a soft preference, never a limit)\nSwing: " + juce::String(juce::roundToInt(style.swing * 100.0f)) +
                             "%   Looseness: " + juce::String(style.humanizeMs, 1) + " ms\nGhost notes: " + juce::String(juce::roundToInt(style.ghostSnareProbability * 100.0f)) +
                             "%   Crash tendency: " + juce::String(juce::roundToInt(style.crashOnSectionStart * 100.0f)) + "%",
                             juce::dontSendNotification);
        meterInfo_.setText("Playing in " + juce::String(processor_.getEffectiveMeterNumerator()) + "/" + juce::String(processor_.getEffectiveMeterDenominator()) +
                           (processor_.isManualMeterEnabled() ? " (manual)" : " (host or 4/4)"), juce::dontSendNotification);
        if (!processor_.isJamMemoryEnabled()) memoryInfo_.setText("Jam memory is off.", juce::dontSendNotification);
        else if (processor_.isArrangementEnabled()) memoryInfo_.setText("Paused while the programmed arrangement is active.", juce::dontSendNotification);
        else memoryInfo_.setText(juce::String(processor_.getJamMemoryBars()) + " bars observed  |  confidence " +
                                 whole(processor_.getJamMemoryConfidence() * 100.0f) + "%\nAverage tempo " +
                                 juce::String(processor_.getJamMemoryAverageTempo(), 1) + " BPM  |  phrase " +
                                 juce::String(processor_.getJamMemoryAveragePhraseBars(), 1) + " bars\nFill bias " +
                                 juce::String(processor_.getJamMemoryFillBias(), 2) + "  |  dynamics x" +
                                 juce::String(processor_.getJamMemoryDynamicSensitivity(), 2), juce::dontSendNotification);
        silenceInfo_.setText("Silent bars: " + juce::String(processor_.getSilentBars()) +
                             (processor_.isWaitingForGuitarResume() ? "   WAITING FOR GUITAR TO RESUME" : ""), juce::dontSendNotification);
        phrase_.setText("Phrase: " + phraseName(processor_.getPhraseState()) + "  |  fill strength " +
                        whole(processor_.getPhraseFillStrength() * 100.0f) + "%" + (processor_.isPhraseBoundary() ? "  |  BOUNDARY" : "") +
                        "\nCoordinator: " + coordinationName(processor_.getJamCoordinationState()) + "  |  transition " +
                        whole(processor_.getTransitionProbability() * 100.0f) + "%  |  ending " +
                        whole(processor_.getEndingProbability() * 100.0f) + "%" +
                        (processor_.isCoordinatorSuppressingBusyFills() ? "  |  restrained fills" : ""), juce::dontSendNotification);
    }
private:
    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamCombo style_;
    ParamToggle manual_;
    ParamCombo numerator_, denominator_;
    ParamToggle memory_;
    ParamCombo silence_;
    ParamSlider silenceBars_;
    juce::Label description_, meterInfo_, phrase_, memoryInfo_, silenceInfo_;
    juce::Rectangle<int> styleArea_, meterArea_, memoryArea_, silenceArea_;
};

// ====================================================================================================================
// SONG (programmed arrangement)
// ====================================================================================================================
class SongPanel final : public PanelBase {
public:
    explicit SongPanel(RoboDrummerAudioProcessor& p)
        : processor_(p), apvts_(p.getParameters()),
          enabled_(apvts_, robodrummer::ParamIDs::arrangementEnabled, "Programmed arrangement", "Play the section list below instead of a free jam.") {
        addAndMakeVisible(enabled_);
        for (int i = 0; i < RoboDrummerAudioProcessor::ArrangementSlotCount; ++i) {
            auto& b = slotButtons_[static_cast<std::size_t>(i)];
            b.setRadioGroupId(4711);
            b.setClickingTogglesState(true);
            b.setTooltip("Select this section to edit it.");
            b.onClick = [this, i] { editing_ = i; loadSlot(); };
            addAndMakeVisible(b);
        }
        slotButtons_[0].setToggleState(true, juce::dontSendNotification);

        for (auto* l : { &nameCap_, &styleCap_, &barsCap_, &intensityCap_, &feelCap_, &transitionCap_, &personalityCap_ }) addAndMakeVisible(*l);
        styleCaption(nameCap_, "Name"); styleCaption(styleCap_, "Style"); styleCaption(barsCap_, "Length");
        styleCaption(intensityCap_, "Intensity"); styleCaption(feelCap_, "Time feel");
        styleCaption(transitionCap_, "Transition in"); styleCaption(personalityCap_, "Drummer");

        name_.setTooltip("A label for this section.");
        name_.onTextChange = [this] { if (!loading_) processor_.setArrangementName(editing_, name_.getText()); };
        addAndMakeVisible(name_);
        style_.addItemList(juce::StringArray(robodrummer::kJamStyleNames.data(), static_cast<int>(robodrummer::kJamStyleNames.size())), 1);
        feel_.addItemList({ "Normal", "Half-time", "Double-time" }, 1);
        transition_.addItemList({ "Crash", "Fill leading in", "Break leading in", "None (no crash)" }, 1);
        personality_.addItemList(juce::StringArray(robodrummer::kDrummerPersonalityNames.data(), static_cast<int>(robodrummer::kDrummerPersonalityNames.size())), 1);
        style_.setTooltip("Groove family for this section.");
        feel_.setTooltip("Half-time or double-time feel applied when the section starts.");
        transition_.setTooltip("How the previous section leads into this one.");
        personality_.setTooltip("Drummer personality while this section plays (Balanced = use the global setting).");
        for (auto* c : std::initializer_list<juce::ComboBox*>{ &style_, &feel_, &transition_, &personality_ }) {
            c->onChange = [this] { commit(); };
            addAndMakeVisible(*c);
        }
        bars_.setRange(1.0, 64.0, 1.0); bars_.setTextValueSuffix(" bars"); bars_.setTooltip("Length of this section in bars.");
        intensity_.setRange(0.0, 1.0, 0.01); intensity_.setTooltip("Target intensity for this section.");
        for (auto* s : { &bars_, &intensity_ }) {
            s->setSliderStyle(juce::Slider::LinearHorizontal);
            s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
            s->onValueChange = [this] { commit(); };
            addAndMakeVisible(*s);
        }
        const auto setupOverride = [this](OverrideRow& row, const char* caption, const char* tip) {
            styleCaption(row.caption, caption);
            row.toggle.setButtonText("Inherit global");
            row.toggle.setTooltip(juce::String(tip) + " Tick to use the global setting.");
            row.slider.setRange(0.0, 1.0, 0.01);
            row.slider.setSliderStyle(juce::Slider::LinearHorizontal);
            row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
            row.slider.setTooltip(tip);
            row.toggle.onClick = [this] { commit(); };
            row.slider.onValueChange = [this] { commit(); };
            addAndMakeVisible(row.caption); addAndMakeVisible(row.toggle); addAndMakeVisible(row.slider);
        };
        setupOverride(fillRow_, "Fill frequency", "How often this section plays automatic fills.");
        setupOverride(leadRow_, "Leadership", "Leadership while this section plays.");
        setupOverride(followRow_, "Follow strength", "Scales how far the drummer may follow the guitar tempo in this section.");
        enabledSlot_.setButtonText("Include in song"); enabledSlot_.setTooltip("Include this section in playback.");
        autoAdvance_.setButtonText("Auto advance"); autoAdvance_.setTooltip("Advance automatically when this section ends; otherwise wait for NEXT SECTION.");
        enabledSlot_.onClick = [this] { commit(); }; autoAdvance_.onClick = [this] { commit(); };
        addAndMakeVisible(enabledSlot_); addAndMakeVisible(autoAdvance_);
        prev_.onClick = [this] { processor_.requestCommand(MidiCommand::PreviousSection); };
        next_.onClick = [this] { processor_.requestCommand(MidiCommand::NextSection); };
        prev_.setTooltip("Go to the previous section now (MIDI note 38)."); next_.setTooltip("Go to the next section now (MIDI note 37).");
        addAndMakeVisible(prev_); addAndMakeVisible(next_);
        seenRevision_ = processor_.getArrangementRevision();
        loadSlot();
    }
    void paint(juce::Graphics& g) override { drawPanel(g, timelineArea_, "Song"); drawPanel(g, editArea_, "Edit section"); }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        timelineArea_ = area.removeFromTop(120);
        area.removeFromTop(8);
        editArea_ = area;
        auto t = timelineArea_.reduced(12, 6); t.removeFromTop(22);
        auto top = t.removeFromTop(26);
        enabled_.setBounds(top.removeFromLeft(240));
        next_.setBounds(top.removeFromRight(110)); top.removeFromRight(6);
        prev_.setBounds(top.removeFromRight(110));
        t.removeFromTop(6);
        const int bw = (t.getWidth() - 5 * 6) / 6;
        for (auto& b : slotButtons_) { b.setBounds(t.removeFromLeft(bw).withHeight(40)); t.removeFromLeft(6); }
        auto e = editArea_.reduced(12, 6); e.removeFromTop(22);
        auto leftCol = e.removeFromLeft(e.getWidth() / 2 - 8);
        e.removeFromLeft(16);
        const auto row = [](juce::Rectangle<int>& col, juce::Component& caption, juce::Component& field) {
            auto r = col.removeFromTop(28); col.removeFromTop(4);
            caption.setBounds(r.removeFromLeft(110)); field.setBounds(r);
        };
        row(leftCol, nameCap_, name_); row(leftCol, styleCap_, style_); row(leftCol, barsCap_, bars_); row(leftCol, intensityCap_, intensity_);
        row(leftCol, feelCap_, feel_); row(leftCol, transitionCap_, transition_);
        row(e, personalityCap_, personality_);
        for (auto* o : { &fillRow_, &leadRow_, &followRow_ }) {
            auto r = e.removeFromTop(28); e.removeFromTop(4);
            o->caption.setBounds(r.removeFromLeft(110)); o->toggle.setBounds(r.removeFromRight(120)); o->slider.setBounds(r);
        }
        auto toggles = e.removeFromTop(28);
        enabledSlot_.setBounds(toggles.removeFromLeft(toggles.getWidth() / 2)); autoAdvance_.setBounds(toggles);
    }
    void refresh() override {
        if (seenRevision_ != processor_.getArrangementRevision() && !bars_.isMouseButtonDown() && !intensity_.isMouseButtonDown()) {
            seenRevision_ = processor_.getArrangementRevision();
            loadSlot();
        }
        const int playing = processor_.isArrangementEnabled() ? processor_.getCurrentArrangementSection() : -1;
        for (int i = 0; i < RoboDrummerAudioProcessor::ArrangementSlotCount; ++i) {
            const auto slot = processor_.getArrangementSlot(i);
            auto& b = slotButtons_[static_cast<std::size_t>(i)];
            const auto name = processor_.getArrangementName(i);
            b.setButtonText(juce::String(i + 1) + (name.isNotEmpty() ? " " + name : juce::String()) + "\n" +
                            robodrummer::kJamStyleNames[static_cast<std::size_t>(slot.section.style)] + " / " + juce::String(slot.section.bars) + " bars" +
                            (slot.enabled ? "" : " (off)"));
            b.setColour(juce::TextButton::buttonColourId, i == playing ? kGood.darker(0.5f) : kPanelRaised);
            b.setColour(juce::TextButton::buttonOnColourId, i == playing ? kGood.darker(0.2f) : kAccent);
        }
    }
private:
    struct OverrideRow { juce::Label caption; juce::ToggleButton toggle; juce::Slider slider; };

    void loadSlot() {
        loading_ = true;
        const auto slot = processor_.getArrangementSlot(editing_);
        name_.setText(processor_.getArrangementName(editing_), false);
        style_.setSelectedId(static_cast<int>(slot.section.style) + 1, juce::dontSendNotification);
        bars_.setValue(slot.section.bars, juce::dontSendNotification);
        intensity_.setValue(slot.section.intensityTarget, juce::dontSendNotification);
        feel_.setSelectedId(static_cast<int>(slot.section.timeFeel) + 1, juce::dontSendNotification);
        transition_.setSelectedId(static_cast<int>(slot.section.transition) + 1, juce::dontSendNotification);
        personality_.setSelectedId(static_cast<int>(slot.section.personality) + 1, juce::dontSendNotification);
        const auto setOverride = [](OverrideRow& row, float value) {
            row.toggle.setToggleState(value < 0.0f, juce::dontSendNotification);
            row.slider.setValue(value < 0.0f ? 0.5 : value, juce::dontSendNotification);
            row.slider.setEnabled(value >= 0.0f);
        };
        setOverride(fillRow_, slot.section.fillFrequency);
        setOverride(leadRow_, slot.section.leadership);
        setOverride(followRow_, slot.section.followStrength);
        enabledSlot_.setToggleState(slot.enabled, juce::dontSendNotification);
        autoAdvance_.setToggleState(slot.section.autoAdvance, juce::dontSendNotification);
        loading_ = false;
    }
    void commit() {
        if (loading_) return;
        robodrummer::ArrangementSlot slot;
        slot.section.style = static_cast<robodrummer::JamStyle>(juce::jlimit(0, robodrummer::kJamStyleCount - 1, style_.getSelectedId() - 1));
        slot.section.bars = static_cast<int>(std::lround(bars_.getValue()));
        slot.section.intensityTarget = static_cast<float>(intensity_.getValue());
        slot.section.timeFeel = static_cast<robodrummer::SectionTimeFeel>(juce::jlimit(0, 2, feel_.getSelectedId() - 1));
        slot.section.transition = static_cast<robodrummer::SectionTransition>(juce::jlimit(0, 3, transition_.getSelectedId() - 1));
        slot.section.personality = static_cast<robodrummer::DrummerPersonality>(juce::jlimit(0, robodrummer::kDrummerPersonalityCount - 1, personality_.getSelectedId() - 1));
        const auto read = [](const OverrideRow& row) { return row.toggle.getToggleState() ? -1.0f : static_cast<float>(row.slider.getValue()); };
        slot.section.fillFrequency = read(fillRow_);
        slot.section.leadership = read(leadRow_);
        slot.section.followStrength = read(followRow_);
        slot.enabled = enabledSlot_.getToggleState();
        slot.section.autoAdvance = autoAdvance_.getToggleState();
        for (auto* row : { &fillRow_, &leadRow_, &followRow_ }) row->slider.setEnabled(!row->toggle.getToggleState());
        processor_.setArrangementSlot(editing_, slot);
        seenRevision_ = processor_.getArrangementRevision();
    }

    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamToggle enabled_;
    std::array<juce::TextButton, RoboDrummerAudioProcessor::ArrangementSlotCount> slotButtons_;
    juce::Label nameCap_, styleCap_, barsCap_, intensityCap_, feelCap_, transitionCap_, personalityCap_;
    juce::TextEditor name_;
    juce::ComboBox style_, feel_, transition_, personality_;
    juce::Slider bars_, intensity_;
    OverrideRow fillRow_, leadRow_, followRow_;
    juce::ToggleButton enabledSlot_, autoAdvance_;
    juce::TextButton prev_{"PREV SECTION"}, next_{"NEXT SECTION"};
    int editing_{0};
    bool loading_{false};
    std::uint32_t seenRevision_{0};
    juce::Rectangle<int> timelineArea_, editArea_;
};

// ====================================================================================================================
// MIDI
// ====================================================================================================================
class MidiPanel final : public PanelBase {
public:
    explicit MidiPanel(RoboDrummerAudioProcessor& p)
        : processor_(p), apvts_(p.getParameters()),
          tempoSource_(apvts_, robodrummer::ParamIDs::tempoSource, "Tempo source", "Host transport, internal BPM only, or incoming MIDI clock (24 ppqn with Start / Stop).") {
        addAndMakeVisible(tempoSource_);
        for (auto* l : { &clock_, &lastMidi_, &hint_ }) {
            l->setFont(juce::FontOptions(13.0f)); l->setColour(juce::Label::textColourId, kDim); l->setMinimumHorizontalScale(0.6f);
            addAndMakeVisible(*l);
        }
        hint_.setText("Learn: click Learn, then press the note (on any channel) or move the controller you want to use. Default commands live on MIDI channel 16 "
                      "(notes 36-50) and are consumed, not forwarded. Controllers mapped to a parameter are consumed too.", juce::dontSendNotification);
        hint_.setJustificationType(juce::Justification::topLeft);
        hint_.setMinimumHorizontalScale(1.0f);

        for (int c = 0; c < robodrummer::MidiLearnMap::CommandCount; ++c) {
            auto& row = commandRows_[static_cast<std::size_t>(c)];
            row.name.setText(robodrummer::kCommandNames[static_cast<std::size_t>(c)], juce::dontSendNotification);
            row.name.setColour(juce::Label::textColourId, kText);
            row.binding.setColour(juce::Label::textColourId, kDim);
            row.binding.setFont(juce::FontOptions(13.0f));
            row.learn.setButtonText("Learn");
            row.learn.setTooltip("Click, then press the MIDI note you want to trigger this command.");
            row.learn.onClick = [this, c] {
                auto& map = processor_.getMidiLearn();
                if (map.armedCommand() == c) map.cancelLearn(); else map.armCommand(c);
            };
            addAndMakeVisible(row.name); addAndMakeVisible(row.binding); addAndMakeVisible(row.learn);
        }
        for (int t = 0; t < robodrummer::kLearnTargetCount; ++t)
            target_.addItem(robodrummer::kLearnTargets[static_cast<std::size_t>(t)].label, t + 1);
        target_.setSelectedId(1, juce::dontSendNotification);
        target_.setTooltip("Choose the parameter to control with a MIDI controller (CC).");
        target_.onChange = [this] { refresh(); };
        learnCc_.setTooltip("Click, then move the controller (knob, fader or expression pedal) you want to assign.");
        learnCc_.onClick = [this] {
            auto& map = processor_.getMidiLearn();
            const int t = target_.getSelectedId() - 1;
            if (map.armedTarget() == t) map.cancelLearn(); else map.armTarget(t);
        };
        clearCc_.onClick = [this] { processor_.getMidiLearn().unbindTarget(target_.getSelectedId() - 1); refresh(); };
        defaults_.setTooltip("Restore the default note commands (channel 16) and the expression-pedal controllers.");
        defaults_.onClick = [this] { processor_.resetMidiMappingsToDefault(); refresh(); };
        for (auto* c : std::initializer_list<juce::Component*>{ &target_, &learnCc_, &clearCc_, &defaults_, &targetInfo_ }) addAndMakeVisible(*c);
        targetInfo_.setColour(juce::Label::textColourId, kDim);
        summary_.setMultiLine(true); summary_.setReadOnly(true); summary_.setScrollbarsShown(true);
        summary_.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(summary_);
        refresh();
    }
    void paint(juce::Graphics& g) override {
        drawPanel(g, sourceArea_, "Clock & monitor");
        drawPanel(g, commandArea_, "Commands (notes)");
        drawPanel(g, controllerArea_, "Controllers (CC)");
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        sourceArea_ = area.removeFromTop(112);
        area.removeFromTop(8);
        controllerArea_ = area.removeFromRight(area.getWidth() * 40 / 100);
        area.removeFromRight(8);
        commandArea_ = area;
        auto s = sourceArea_.reduced(12, 6); s.removeFromTop(22);
        auto left = s.removeFromLeft(s.getWidth() / 2 - 6);
        s.removeFromLeft(12);
        tempoSource_.setBounds(left.removeFromTop(28)); left.removeFromTop(4);
        clock_.setBounds(left.removeFromTop(20)); lastMidi_.setBounds(left.removeFromTop(20));
        hint_.setBounds(s);
        auto c = commandArea_.reduced(12, 6); c.removeFromTop(22);
        const int rowH = juce::jmin(34, c.getHeight() / robodrummer::MidiLearnMap::CommandCount);
        for (auto& row : commandRows_) {
            auto r = c.removeFromTop(rowH);
            row.name.setBounds(r.removeFromLeft(120));
            row.learn.setBounds(r.removeFromRight(70).reduced(0, 2));
            row.binding.setBounds(r);
        }
        auto k = controllerArea_.reduced(12, 6); k.removeFromTop(22);
        target_.setBounds(k.removeFromTop(28)); k.removeFromTop(4);
        targetInfo_.setBounds(k.removeFromTop(22)); k.removeFromTop(2);
        rowOfButtons(k.removeFromTop(28), 6, { &learnCc_, &clearCc_ });
        k.removeFromTop(6);
        defaults_.setBounds(k.removeFromBottom(28)); k.removeFromBottom(6);
        summary_.setBounds(k);
    }
    void refresh() override {
        auto& map = processor_.getMidiLearn();
        const bool clockActive = processor_.isMidiClockActive();
        clock_.setText(clockActive ? "MIDI clock: receiving " + juce::String(processor_.getMidiClockBpm(), 1) + " BPM" : "MIDI clock: none", juce::dontSendNotification);
        lastMidi_.setText("Last MIDI: " + processor_.getLastMidiText(), juce::dontSendNotification);
        for (int c = 0; c < robodrummer::MidiLearnMap::CommandCount; ++c) {
            auto& row = commandRows_[static_cast<std::size_t>(c)];
            const int note = map.noteForCommand(c);
            const bool armed = map.armedCommand() == c;
            row.binding.setText(armed ? "waiting for a note..." : (note < 0 ? "unassigned" : "Note " + juce::String(note) + " (" + juce::MidiMessage::getMidiNoteName(note, true, true, 3) + ")  ch " + juce::String(map.channelForNote(note))), juce::dontSendNotification);
            row.learn.setToggleState(armed, juce::dontSendNotification);
            row.learn.setClickingTogglesState(false);
        }
        const int t = target_.getSelectedId() - 1;
        const int cc = map.ccForTarget(t);
        const bool armedTarget = map.armedTarget() == t;
        targetInfo_.setText(armedTarget ? "Waiting for a controller..." : (cc < 0 ? "Not assigned" : "Controller CC " + juce::String(cc)), juce::dontSendNotification);
        learnCc_.setToggleState(armedTarget, juce::dontSendNotification);
        learnCc_.setClickingTogglesState(false);
        juce::String text;
        for (int i = 0; i < robodrummer::kLearnTargetCount; ++i) {
            const int assigned = map.ccForTarget(i);
            if (assigned >= 0) text << "CC " << assigned << "  ->  " << robodrummer::kLearnTargets[static_cast<std::size_t>(i)].label << "\n";
        }
        if (text.isEmpty()) text = "No controllers assigned.";
        if (text != lastSummary_) { lastSummary_ = text; summary_.setText(text, false); }
    }
private:
    struct CommandRow { juce::Label name, binding; juce::TextButton learn; };
    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamCombo tempoSource_;
    juce::Label clock_, lastMidi_, hint_, targetInfo_;
    std::array<CommandRow, robodrummer::MidiLearnMap::CommandCount> commandRows_;
    juce::ComboBox target_;
    juce::TextButton learnCc_{"Learn CC"}, clearCc_{"Clear"}, defaults_{"Restore default mappings"};
    juce::TextEditor summary_;
    juce::String lastSummary_;
    juce::Rectangle<int> sourceArea_, commandArea_, controllerArea_;
};

// ====================================================================================================================
// ANALYSIS
// ====================================================================================================================
class AnalysisPanel final : public PanelBase {
public:
    explicit AnalysisPanel(RoboDrummerAudioProcessor& p) : processor_(p) {
        for (auto& v : history_) v = {};
    }

    void paint(juce::Graphics& g) override {
        const double sr = processor_.getSampleRateForDisplay();
        const double nowSeconds = static_cast<double>(processor_.getAnalysisSampleCursor()) / std::max(1.0, sr);

        auto area = getLocalBounds().reduced(8);
        auto logArea = area.removeFromRight(area.getWidth() * 28 / 100);
        area.removeFromRight(8);

        // ---- onsets and predicted beat grid ----
        auto onsetArea = area.removeFromTop(area.getHeight() * 36 / 100);
        drawPanel(g, onsetArea, "Onsets (last 8 s) and predicted beat grid");
        auto plot = onsetArea.reduced(12, 6); plot.removeFromTop(22);
        g.setColour(kBackground);
        g.fillRoundedRectangle(plot.toFloat(), 4.0f);
        constexpr double window = 8.0;
        const auto xFor = [&](double t) { return plot.getX() + static_cast<float>((t - (nowSeconds - window)) / window) * plot.getWidth(); };
        const double bpm = processor_.getDetectedGuitarBpm();
        const double period = 60.0 / std::max(30.0, bpm);
        const double phase = processor_.getGuitarBeatPhase();
        const int beatInBar = processor_.getGuitarBeatInBar();
        const int meter = std::max(1, processor_.getEffectiveMeterNumerator());
        const double lastBeat = nowSeconds - phase * period;
        for (int k = 0; k < 60; ++k) {
            const double t = lastBeat - k * period;
            if (t < nowSeconds - window) break;
            const int number = ((beatInBar - 1 - k) % meter + meter) % meter;
            g.setColour(number == 0 ? kWarn.withAlpha(0.8f) : kDim.withAlpha(0.35f));
            g.drawLine(xFor(t), static_cast<float>(plot.getY()), xFor(t), static_cast<float>(plot.getBottom()), number == 0 ? 2.0f : 1.0f);
        }
        g.setColour(kAccentAlt.withAlpha(0.6f)); // the predicted next beat
        const double nextBeat = processor_.getPredictedNextGuitarBeatSeconds();
        if (nextBeat > nowSeconds - 0.001 && nextBeat < nowSeconds + 1.0) {
            const float x = std::min(xFor(nextBeat), static_cast<float>(plot.getRight()) - 1.0f);
            g.drawLine(x, static_cast<float>(plot.getY()), x, static_cast<float>(plot.getBottom()), 2.0f);
        }
        for (const auto& onset : onsets_) {
            const double t = static_cast<double>(onset.sample) / std::max(1.0, sr);
            if (t < nowSeconds - window) continue;
            const float h = 6.0f + onset.strength * (plot.getHeight() - 10.0f);
            g.setColour(kAccent);
            g.fillRect(juce::Rectangle<float>(xFor(t) - 1.5f, plot.getBottom() - h, 3.0f, h));
        }
        g.setColour(kDim); g.setFont(juce::FontOptions(11.0f));
        g.drawText("now", plot.getRight() - 40, plot.getY(), 38, 14, juce::Justification::centredRight);

        // ---- tempo candidates, active tempo, phase error ----
        area.removeFromTop(8);
        auto tempoArea = area.removeFromTop(area.getHeight() * 38 / 100);
        drawPanel(g, tempoArea, "Tempo hypotheses and phase");
        auto inner = tempoArea.reduced(12, 6); inner.removeFromTop(22);
        const auto candidates = processor_.getRichHypotheses();
        auto candidateArea = inner.removeFromLeft(inner.getWidth() * 58 / 100);
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto row = candidateArea.removeFromTop(22);
            g.setColour(kText); g.setFont(juce::FontOptions(13.0f));
            g.drawText(candidates[i].confidence > 0.0f ? juce::String(candidates[i].bpm, 1) + " BPM" : juce::String("--"), row.removeFromLeft(78), juce::Justification::centredLeft);
            auto details = row.removeFromRight(150);
            g.setColour(kDim); g.setFont(juce::FontOptions(11.0f));
            static const char* relations[] = { "primary", "half", "double", "related", "other" };
            if (candidates[i].confidence > 0.0f)
                g.drawText(juce::String(candidates[i].supportingEvents) + " ev  " + whole(candidates[i].consistency * 100.0) + "%  " + whole(candidates[i].ageSeconds) + "s  " +
                           relations[juce::jlimit(0, 4, static_cast<int>(candidates[i].relationToBest))], details, juce::Justification::centredRight);
            g.setColour(kPanelRaised); g.fillRoundedRectangle(row.reduced(0, 4).toFloat(), 3.0f);
            g.setColour(i == 0 ? kAccent : kAccentAlt);
            g.fillRoundedRectangle(row.reduced(0, 4).toFloat().withWidth(row.getWidth() * candidates[i].confidence), 3.0f);
            candidateArea.removeFromTop(3);
        }
        const auto perf = processor_.getPerformanceState();
        auto textRow = candidateArea.withWidth(tempoArea.getWidth() - 24);
        g.setColour(kDim); g.setFont(juce::FontOptions(11.0f));
        g.drawText("bands L/M/H " + whole(perf.bandLow * 100.0) + "/" + whole(perf.bandMid * 100.0) + "/" + whole(perf.bandHigh * 100.0) + "  flux " + whole(perf.spectralFlux * 100.0) +
                   "  density " + whole(perf.transientDensity * 100.0) + "  regularity " + whole(perf.regularity * 100.0) + "  accent " + whole(perf.accentProbability * 100.0) +
                   "  silence " + whole(perf.silenceProbability * 100.0) + "%", textRow.removeFromTop(16), juce::Justification::centredLeft);
        g.setColour(kText); g.setFont(juce::FontOptions(13.0f));
        g.drawText("Guitar " + juce::String(bpm, 1) + " BPM  |  drummer " + juce::String(processor_.getEffectiveDrummerBpm(), 1) + " BPM  |  " + movementName(processor_.getTempoMovement()) +
                   " (" + juce::String(processor_.getTempoVelocity(), 2) + " BPM/s)  |  lookahead " + juce::String(processor_.getLookaheadMs(), 1) + " ms",
                   textRow.removeFromTop(20), juce::Justification::centredLeft);
        inner.removeFromLeft(14);
        auto phaseArea = inner;
        g.setColour(kDim); g.drawText("Phase error (cycles)", phaseArea.removeFromTop(18), juce::Justification::centredLeft);
        auto gauge = phaseArea.removeFromTop(22);
        g.setColour(kPanelRaised); g.fillRoundedRectangle(gauge.toFloat(), 3.0f);
        const double error = juce::jlimit(-0.5, 0.5, processor_.getPhaseErrorCycles());
        const float mid = gauge.getCentreX();
        g.setColour(processor_.isHardResyncRecommended() ? kAccent : kGood);
        g.fillRect(juce::Rectangle<float>(error >= 0 ? mid : mid + static_cast<float>(error) * gauge.getWidth(), static_cast<float>(gauge.getY() + 3),
                                          std::abs(static_cast<float>(error)) * gauge.getWidth(), static_cast<float>(gauge.getHeight() - 6)));
        g.setColour(kText); g.drawLine(mid, static_cast<float>(gauge.getY()), mid, static_cast<float>(gauge.getBottom()), 1.0f);
        g.drawText(juce::String(processor_.getPhaseErrorCycles(), 3) + (processor_.isHardResyncRecommended() ? "  RESYNC NEEDED" : ""), phaseArea.removeFromTop(20), juce::Justification::centredLeft);

        // ---- confidence history ----
        area.removeFromTop(8);
        auto levelPanel = area.removeFromBottom(76);
        area.removeFromBottom(8);
        auto confArea = area;
        drawPanel(g, confArea, "Confidence history (30 s)   tempo / beat / downbeat");
        auto cplot = confArea.reduced(12, 6); cplot.removeFromTop(22);
        g.setColour(kBackground); g.fillRoundedRectangle(cplot.toFloat(), 4.0f);
        const juce::Colour colours[3] { kAccent, kAccentAlt, kWarn };
        for (std::size_t line = 0; line < 3; ++line) {
            juce::Path path;
            for (std::size_t i = 0; i < HistoryLength; ++i) {
                const auto index = (historyHead_ + i) % HistoryLength;
                const float x = cplot.getX() + cplot.getWidth() * static_cast<float>(i) / static_cast<float>(HistoryLength - 1);
                const float y = cplot.getBottom() - history_[index][line] * (cplot.getHeight() - 4.0f) - 2.0f;
                if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            }
            g.setColour(colours[line]); g.strokePath(path, juce::PathStrokeType(1.6f));
        }
        // ---- input level + noise gate ----
        drawPanel(g, levelPanel, "Input level and noise gate");
        auto levelArea = levelPanel.withTrimmedTop(22);
        auto meterRow = levelArea.reduced(12, 0).removeFromTop(24);
        g.setColour(kDim); g.setFont(juce::FontOptions(12.0f));
        g.drawText("Input level", meterRow.removeFromLeft(80), juce::Justification::centredLeft);
        auto bar = meterRow.reduced(0, 6);
        g.setColour(kPanelRaised); g.fillRoundedRectangle(bar.toFloat(), 3.0f);
        const auto db = [](float v) { return v > 1.0e-5f ? 20.0f * std::log10(v) : -100.0f; };
        const auto norm = [](float d) { return juce::jlimit(0.0f, 1.0f, (d + 80.0f) / 80.0f); };
        g.setColour(processor_.isGateOpen() ? kGood : kDim);
        g.fillRoundedRectangle(bar.toFloat().withWidth(bar.getWidth() * norm(db(processor_.getInputPeak()))), 3.0f);
        const float gate = static_cast<float>(*processor_.getParameters().getRawParameterValue(robodrummer::ParamIDs::gateThreshold));
        if (gate > -80.0f) {
            g.setColour(kWarn);
            const float x = bar.getX() + bar.getWidth() * norm(gate);
            g.drawLine(x, static_cast<float>(bar.getY() - 2), x, static_cast<float>(bar.getBottom() + 2), 2.0f);
        }
        g.setColour(kDim);
        g.drawText(whole(db(processor_.getInputPeak())) + " dB  gate " + (processor_.isGateOpen() ? "open" : "closed"),
                   levelArea.reduced(12, 0).removeFromBottom(18), juce::Justification::centredLeft);

        // ---- state transition log ----
        drawPanel(g, logArea, "State transitions");
        auto lines = logArea.reduced(12, 6); lines.removeFromTop(22);
        g.setFont(juce::FontOptions(12.0f));
        for (auto it = log_.rbegin(); it != log_.rend() && lines.getHeight() >= 16; ++it) {
            g.setColour(kDim);
            g.drawText(juce::String(it->time, 1) + "s", lines.withHeight(16).removeFromLeft(48), juce::Justification::centredLeft);
            g.setColour(kText);
            g.drawText(it->text, lines.withHeight(16).withTrimmedLeft(50), juce::Justification::centredLeft);
            lines.removeFromTop(17);
        }
    }

    void refresh() override {
        // pull onset marks and trim old ones
        std::array<robodrummer::AnalysisOnsetMark, 64> marks;
        for (;;) {
            const auto n = processor_.popOnsetMarks(marks.data(), marks.size());
            if (n == 0) break;
            for (std::size_t i = 0; i < n; ++i) onsets_.push_back(marks[i]);
        }
        const double sr = std::max(1.0, processor_.getSampleRateForDisplay());
        const long long nowSample = processor_.getAnalysisSampleCursor();
        while (!onsets_.empty() && static_cast<double>(nowSample - onsets_.front().sample) / sr > 9.0) onsets_.pop_front();

        historyHead_ = (historyHead_ + 1) % HistoryLength;
        history_[(historyHead_ + HistoryLength - 1) % HistoryLength] = { processor_.getGuitarTempoConfidence(), processor_.getGuitarBeatConfidence(), processor_.getGuitarDownbeatConfidence() };

        // state transitions
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0 - startSeconds_;
        const auto note = [&](const juce::String& text) { log_.push_back({ now, text }); if (log_.size() > 40) log_.pop_front(); };
        const bool locked = processor_.isGuitarTrackerLocked();
        if (locked != lastLocked_) { note(locked ? "Tracker locked" : "Tracker lost lock"); lastLocked_ = locked; }
        const bool joined = processor_.hasAdaptiveJoined();
        if (joined != lastJoined_) { note(joined ? "Drummer joined" : "Drummer waiting"); lastJoined_ = joined; }
        const auto coordination = processor_.getJamCoordinationState();
        if (coordination != lastCoordination_) { note(coordinationName(coordination)); lastCoordination_ = coordination; }
        const auto phrase = processor_.getPhraseState();
        if (phrase != lastPhrase_) { note("Phrase: " + phraseName(phrase)); lastPhrase_ = phrase; }
        const bool resync = processor_.isHardResyncRecommended();
        if (resync != lastResync_) { if (resync) note("Phase jump - resync pending"); lastResync_ = resync; }
        const bool waiting = processor_.isWaitingForGuitarResume();
        if (waiting != lastWaiting_) { note(waiting ? "Waiting for guitar" : "Guitar resumed"); lastWaiting_ = waiting; }
        const bool stopped = processor_.isDrummerStopped();
        if (stopped != lastStopped_) { note(stopped ? "Drummer stopped" : "Drummer running"); lastStopped_ = stopped; }
        repaint();
    }

private:
    static constexpr std::size_t HistoryLength = 360;
    struct LogEntry { double time; juce::String text; };

    RoboDrummerAudioProcessor& processor_;
    std::deque<robodrummer::AnalysisOnsetMark> onsets_;
    std::array<std::array<float, 3>, HistoryLength> history_;
    std::size_t historyHead_{0};
    std::deque<LogEntry> log_;
    double startSeconds_{juce::Time::getMillisecondCounterHiRes() / 1000.0};
    bool lastLocked_{false}, lastJoined_{false}, lastResync_{false}, lastWaiting_{false}, lastStopped_{false};
    robodrummer::JamCoordinationState lastCoordination_{robodrummer::JamCoordinationState::EstablishingGroove};
    robodrummer::PhraseState lastPhrase_{robodrummer::PhraseState::Stable};
};

// ====================================================================================================================
// MIDI drum output map (editable; General MIDI by default)
// ====================================================================================================================
class DrumMapEditor final : public juce::Component {
public:
    explicit DrumMapEditor(RoboDrummerAudioProcessor& p) : processor_(p) {
        static const char* names[16] { "Kick", "Snare", "Closed hat", "Open hat", "Ride", "Crash", "High tom", "Mid tom", "Floor tom",
                                       "Rimshot", "Side stick", "China", "Splash", "Cowbell", "Tambourine", "Clap" };
        for (int i = 0; i < 16; ++i) {
            rows_[static_cast<std::size_t>(i)].label.setText(names[i], juce::dontSendNotification);
            for (int note = 0; note < 128; ++note)
                rows_[static_cast<std::size_t>(i)].combo.addItem(juce::String(note) + "  " + juce::MidiMessage::getMidiNoteName(note, true, true, 3), note + 1);
            addAndMakeVisible(rows_[static_cast<std::size_t>(i)].label);
            addAndMakeVisible(rows_[static_cast<std::size_t>(i)].combo);
            rows_[static_cast<std::size_t>(i)].combo.onChange = [this, i] {
                processor_.setMidiDrumNote(static_cast<robodrummer::DrumInstrument>(i), rows_[static_cast<std::size_t>(i)].combo.getSelectedId() - 1);
            };
        }
        reset_.setTooltip("Restore the General MIDI percussion map.");
        reset_.onClick = [this] { processor_.resetMidiDrumMap(); load(); };
        addAndMakeVisible(reset_);
        load();
        setSize(330, 16 * 28 + 52);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(10);
        for (auto& row : rows_) {
            auto r = area.removeFromTop(26); area.removeFromTop(2);
            row.label.setBounds(r.removeFromLeft(110)); row.combo.setBounds(r);
        }
        reset_.setBounds(area.removeFromTop(30).reduced(0, 2));
    }
private:
    void load() {
        for (int i = 0; i < 16; ++i)
            rows_[static_cast<std::size_t>(i)].combo.setSelectedId(processor_.getMidiDrumNote(static_cast<robodrummer::DrumInstrument>(i)) + 1, juce::dontSendNotification);
    }
    struct Row { juce::Label label; juce::ComboBox combo; };
    RoboDrummerAudioProcessor& processor_;
    std::array<Row, 16> rows_;
    juce::TextButton reset_{"RESET TO GENERAL MIDI"};
};

// ====================================================================================================================
// SETTINGS
// ====================================================================================================================
class SettingsPanel final : public PanelBase {
public:
    SettingsPanel(RoboDrummerAudioProcessor& p, std::function<void(bool)> tooltipChanged, bool tooltipsOn, std::function<void()> showHelp)
        : processor_(p), apvts_(p.getParameters()),
          mode_(apvts_, robodrummer::ParamIDs::inputMode, "Guitar input", "Which input channel(s) the analysis listens to. The audible guitar is never altered."),
          gain_(apvts_, robodrummer::ParamIDs::inputGain, "Input gain", "Gain applied to the analysis signal only."),
          autoGain_(apvts_, robodrummer::ParamIDs::autoGain, "Automatic gain normalisation", "Slowly levels a quiet or hot guitar for analysis."),
          gate_(apvts_, robodrummer::ParamIDs::gateThreshold, "Noise gate", "Analysis noise gate threshold. Off at the far left."),
          dynamic_(apvts_, robodrummer::ParamIDs::dynamicFollow, "Dynamic follow", "How strongly guitar dynamics steer the drummer's intensity."),
          strength_(apvts_, robodrummer::ParamIDs::followStrength, "Follow strength", "Scales how much of the guitar's timing authority is applied (100% = all of it)."),
          inertia_(apvts_, robodrummer::ParamIDs::tempoInertia, "Tempo inertia", "How heavy the tempo feels. High values resist change; low values move quickly."),
          correction_(apvts_, robodrummer::ParamIDs::beatCorrection, "Beat correction speed", "How quickly small phase errors against the guitar's beat are nudged away."),
          sensitivity_(apvts_, robodrummer::ParamIDs::confidenceSensitivity, "Confidence sensitivity", "Low = only follow when the tracker is very sure; high = follow on weaker evidence."),
          midiChannel_(apvts_, robodrummer::ParamIDs::midiOutChannel, "Output channel", "MIDI channel for generated drum notes (default 10, General MIDI drums)."),
          tooltips_("Show tooltips"), showHelp_(std::move(showHelp)) {
        for (auto* c : std::initializer_list<juce::Component*>{ &mode_, &gain_, &autoGain_, &gate_, &dynamic_, &strength_, &inertia_, &correction_, &sensitivity_, &midiChannel_ }) addAndMakeVisible(*c);
        drumMap_.setTooltip("Edit which MIDI note each drum sends (General MIDI by default).");
        drumMap_.onClick = [this] {
            juce::CallOutBox::launchAsynchronously(std::make_unique<DrumMapEditor>(processor_), drumMap_.getScreenBounds(), nullptr);
        };
        addAndMakeVisible(drumMap_);
        addAndMakeVisible(meter_);
        meter_.setColour(kGood);
        calibrate_.setTooltip("Play normally for about 8 seconds. RoboDrummer measures your noise floor, levels and attack density and suggests gate and gain settings.");
        calibrate_.onClick = [this] { if (processor_.getCalibration().state == 1) processor_.cancelCalibration(); else processor_.startCalibration(); };
        apply_.setTooltip("Apply the suggested gate threshold and input gain.");
        apply_.onClick = [this] { processor_.applyCalibrationSuggestions(); };
        addAndMakeVisible(calibrate_); addAndMakeVisible(apply_); addAndMakeVisible(progress_);
        progress_.setColour(kAccent);
        for (auto* l : { &gateState_, &calResult_, &about_ }) {
            l->setFont(juce::FontOptions(13.0f)); l->setColour(juce::Label::textColourId, kDim); l->setMinimumHorizontalScale(1.0f);
            l->setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(*l);
        }
        tooltips_.setToggleState(tooltipsOn, juce::dontSendNotification);
        tooltips_.setTooltip("Show or hide the hover help on every control.");
        tooltips_.onClick = [this, tooltipChanged] { tooltipChanged(tooltips_.getToggleState()); };
        helpButton_.onClick = [this] { showHelp_(); };
        addAndMakeVisible(tooltips_); addAndMakeVisible(helpButton_);
        about_.setText(juce::String("RoboDrummer ") + JucePlugin_VersionString + "  |  Circuit Drift Labs\nAdaptive virtual drummer for a live guitarist.\n"
                       "Formats: VST3, CLAP, Standalone (Windows, macOS, Linux). No network access, telemetry, licensing or update checks.\n"
                       "Audio thread: no allocation, no locks, no disk. Kits load on a background thread.", juce::dontSendNotification);
        website_.setButtonText("Circuit Drift Labs website");
        website_.setURL(juce::URL("https://djshellshoxxx.github.io/circuitdriftlabs/"));
        website_.setColour(juce::HyperlinkButton::textColourId, kAccentAlt);
        addAndMakeVisible(website_);
        refresh();
    }
    void paint(juce::Graphics& g) override {
        drawPanel(g, inputArea_, "Guitar input & analysis");
        drawPanel(g, calibrationArea_, "Calibration");
        drawPanel(g, followArea_, "Following (advanced)");
        drawPanel(g, midiOutArea_, "MIDI drum output");
        drawPanel(g, interfaceArea_, "Interface & help");
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto left = area.removeFromLeft(area.getWidth() * 55 / 100);
        area.removeFromLeft(8);
        inputArea_ = left.removeFromTop(left.getHeight() * 62 / 100);
        left.removeFromTop(8);
        interfaceArea_ = left;
        calibrationArea_ = area.removeFromTop(area.getHeight() * 40 / 100);
        area.removeFromTop(8);
        midiOutArea_ = area.removeFromBottom(100);
        area.removeFromBottom(8);
        followArea_ = area;
        auto i = inputArea_.reduced(12, 6); i.removeFromTop(22);
        stack(i, 28, 4, { &mode_, &gain_, &autoGain_, &gate_, &dynamic_ });
        meter_.setBounds(i.removeFromTop(14).reduced(0, 2)); i.removeFromTop(4);
        gateState_.setBounds(i.removeFromTop(20));
        auto c = calibrationArea_.reduced(12, 6); c.removeFromTop(22);
        rowOfButtons(c.removeFromTop(34), 8, { &calibrate_, &apply_ }); c.removeFromTop(8);
        progress_.setBounds(c.removeFromTop(14)); c.removeFromTop(8);
        calResult_.setBounds(c);
        auto fa = followArea_.reduced(12, 6); fa.removeFromTop(22);
        stack(fa, 28, 3, { &strength_, &inertia_, &correction_, &sensitivity_ });
        auto mo = midiOutArea_.reduced(12, 6); mo.removeFromTop(22);
        stack(mo, 28, 3, { &midiChannel_ });
        drumMap_.setBounds(mo.removeFromTop(28));
        auto f = interfaceArea_.reduced(12, 6); f.removeFromTop(22);
        auto row = f.removeFromTop(30);
        tooltips_.setBounds(row.removeFromLeft(160)); helpButton_.setBounds(row.removeFromLeft(120).reduced(0, 2)); f.removeFromTop(6);
        website_.setBounds(f.removeFromBottom(24).withWidth(220));
        about_.setBounds(f);
    }
    void refresh() override {
        const auto toNorm = [](float peak) { const float db = peak > 1.0e-5f ? 20.0f * std::log10(peak) : -100.0f; return juce::jlimit(0.0f, 1.0f, (db + 80.0f) / 80.0f); };
        meter_.setValue(toNorm(processor_.getInputPeak()));
        gateState_.setText(juce::String("Analysis input: ") + whole(processor_.getInputPeak() > 1.0e-5f ? 20.0f * std::log10(processor_.getInputPeak()) : -100.0f) +
                           " dB peak  |  gate " + (processor_.isGateOpen() ? "open" : "closed"), juce::dontSendNotification);
        const auto cal = processor_.getCalibration();
        calibrate_.setButtonText(cal.state == 1 ? "CANCEL" : "CALIBRATE (8 s)");
        apply_.setEnabled(cal.state == 2);
        progress_.setValue(cal.state == 1 ? cal.progress : (cal.state == 2 ? 1.0f : 0.0f));
        if (cal.state == 1) calResult_.setText("Listening... play normally (strum, riff, include quiet parts).", juce::dontSendNotification);
        else if (cal.state == 2) {
            const auto& r = cal.result;
            calResult_.setText("Noise floor " + juce::String(r.noiseFloorDb, 1) + " dB\nTypical level " + juce::String(r.typicalRmsDb, 1) + " dB, loud " + juce::String(r.loudRmsDb, 1) +
                               " dB, peak " + juce::String(r.peakDb, 1) + " dB\nDynamic range " + juce::String(r.dynamicRangeDb, 1) + " dB\nAttack density " +
                               juce::String(r.onsetsPerSecond, 1) + " per second\n\nSuggested noise gate " + juce::String(r.suggestedGateDb, 1) + " dB, input gain " +
                               juce::String(r.suggestedGainDb, 1) + " dB", juce::dontSendNotification);
        } else calResult_.setText("Not calibrated. Calibration is optional: it only seeds the gate and gain, and everything stays adjustable.", juce::dontSendNotification);
    }
private:
    RoboDrummerAudioProcessor& processor_;
    APVTS& apvts_;
    ParamCombo mode_;
    ParamSlider gain_;
    ParamToggle autoGain_;
    ParamSlider gate_, dynamic_, strength_, inertia_, correction_, sensitivity_, midiChannel_;
    juce::TextButton drumMap_{"EDIT DRUM MAP..."};
    BarMeter meter_, progress_;
    juce::TextButton calibrate_{"CALIBRATE (8 s)"}, apply_{"APPLY SUGGESTIONS"}, helpButton_{"HELP"};
    juce::ToggleButton tooltips_;
    juce::HyperlinkButton website_;
    juce::Label gateState_, calResult_, about_;
    std::function<void()> showHelp_;
    juce::Rectangle<int> inputArea_, calibrationArea_, interfaceArea_, followArea_, midiOutArea_;
};

} // namespace rdui
