#pragma once
#include "plugin/PluginEditor.h"
#include "plugin/PresetManager.h"
#include <cmath>

namespace preset_regressions {
inline void setPlain(RoboDrummerAudioProcessor& processor, const char* id, float value) {
    if (auto* parameter = processor.getParameters().getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
inline float value(RoboDrummerAudioProcessor& processor, const char* id) {
    return processor.getParameters().getRawParameterValue(id)->load();
}
inline void parameter(juce::XmlElement& xml, const char* id, const juce::String& number) {
    auto* node = xml.createNewChildElement("Param");
    node->setAttribute("id", id); node->setAttribute("value", number);
}
inline juce::Component* find(juce::Component& parent, const juce::String& id) {
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = find(*child, id)) return found;
    return nullptr;
}

// Runs with the real processor and file loader; all test files use caller-owned scratch storage.
template <class Check>
void validation(const juce::File& scratch, Check check) {
    using robodrummer::PresetManager; using robodrummer::PresetType;
    scratch.createDirectory();
    const auto file = scratch.getChildFile("validation.rdpreset");
    RoboDrummerAudioProcessor processor;
    auto load = [&](juce::XmlElement& xml, PresetType type = PresetType::Kit) {
        check(xml.writeTo(file), "write preset fixture");
        return PresetManager::loadUserFile(processor, type, file);
    };
    juce::XmlElement original("RoboDrummerPreset");
    original.setAttribute("type", "Kit"); original.setAttribute("version", 1);
    parameter(original, "roomSize", "0.25");
    check(load(original), "current v1 Kit preset loads");
    check(std::abs(value(processor, "roomSize") - 0.25f) < 0.001f, "current preset applies allowed field");
    auto legacy = original;
    legacy.removeAttribute("type"); legacy.removeAttribute("version");
    setPlain(processor, "roomSize", 0.6f);
    check(load(legacy), "legacy missing type/version infer requested type and version one");
    check(std::abs(value(processor, "roomSize") - 0.25f) < 0.001f, "legacy overlay applies unchanged");

    auto injected = original;
    parameter(injected, "jamStyle", "15"); parameter(injected, "practiceEnabled", "1"); parameter(injected, "pracLoopOn", "1");
    parameter(injected, "futureParameter", "future enum text");
    setPlain(processor, "jamStyle", 0); setPlain(processor, "practiceEnabled", 0); setPlain(processor, "pracLoopOn", 0);
    check(load(injected), "out-of-slice and future fields are ignored for compatibility");
    check(value(processor, "jamStyle") == 0 && value(processor, "practiceEnabled") == 0 && value(processor, "pracLoopOn") == 0,
          "Kit load cannot change groove or start practice");
    auto jam = original;
    jam.setAttribute("type", "Jam");
    parameter(jam, "practiceEnabled", "1"); parameter(jam, "pracLoopOn", "1");
    check(load(jam, PresetType::Jam), "Jam preset accepts saved fields");
    check(value(processor, "practiceEnabled") == 0 && value(processor, "pracLoopOn") == 0, "Jam load never starts a session");

    struct SliceCase { PresetType type; const char* allowed; const char* excluded; };
    for (const auto slice : {
            SliceCase{PresetType::Jam, "bpm", "practiceEnabled"},
            SliceCase{PresetType::Drummer, "humanize", "roomSize"},
            SliceCase{PresetType::Style, "jamStyle", "humanize"},
            SliceCase{PresetType::Kit, "roomSize", "jamStyle"},
            SliceCase{PresetType::Song, "arrangementEnabled", "bpm"},
            SliceCase{PresetType::Analysis, "inputGain", "jamStyle"}}) {
        auto* allowed = processor.getParameters().getParameter(slice.allowed);
        auto* excluded = processor.getParameters().getParameter(slice.excluded);
        allowed->setValueNotifyingHost(0.1f); excluded->setValueNotifyingHost(0.1f);
        const auto beforeAllowed = allowed->getValue(), beforeExcluded = excluded->getValue();
        juce::XmlElement xml("RoboDrummerPreset"); xml.setAttribute("type", PresetManager::typeName(slice.type)); xml.setAttribute("version", 1);
        parameter(xml, slice.allowed, juce::String(allowed->convertFrom0to1(0.8f), 8));
        parameter(xml, slice.excluded, juce::String(excluded->convertFrom0to1(0.8f), 8));
        check(load(xml, slice.type), "each preset type loads a valid slice");
        check(std::abs(allowed->getValue() - beforeAllowed) > 0.1f && excluded->getValue() == beforeExcluded,
              "each preset type changes only its own allowed parameters");
    }

    auto unchanged = [&](juce::XmlElement& xml, const char* label, PresetType type = PresetType::Kit) {
        juce::MemoryBlock before, after; processor.getStateInformation(before);
        check(!load(xml, type), label);
        processor.getStateInformation(after);
        check(before == after, "rejected preset preserves complete serialized state");
    };
    juce::XmlElement wrongRoot("OtherPreset"); parameter(wrongRoot, "roomSize", "0.9"); unchanged(wrongRoot, "wrong root rejected");
    auto wrongType = original; wrongType.setAttribute("type", "Drummer"); unchanged(wrongType, "explicit mismatched type rejected");
    for (const char* version : { "0", "2", "1junk", "" }) {
        auto wrongVersion = original; wrongVersion.setAttribute("version", version); unchanged(wrongVersion, "unsupported version rejected");
    }
    for (const char* number : { "garbage", "", "0.5tail", "1..2", "1e", "--1" }) {
        auto malformed = original; parameter(malformed, "kitTune", number); unchanged(malformed, "malformed value rejected transactionally");
    }
    auto missingValue = original; missingValue.createNewChildElement("Param")->setAttribute("id", "kitTune");
    unchanged(missingValue, "missing numeric value rejected");
    auto duplicate = original; parameter(duplicate, "roomSize", "0.8"); unchanged(duplicate, "ambiguous duplicate field rejected");

    // Retain previously accepted sanitization: nonfinite values are ignored and finite values clamp.
    for (const char* number : { "nan", "-nan", "inf", "-inf", "infinity", "1e99" }) {
        auto numeric = original;
        numeric.getChildByName("Param")->setAttribute("value", number);
        setPlain(processor, "roomSize", 0.6f);
        check(load(numeric), "historically accepted nonfinite overlay remains loadable");
        check(std::abs(value(processor, "roomSize") - 0.6f) < 0.001f, "nonfinite preset value is ignored");
    }
    for (const char* number : { "-100", "100" }) {
        auto numeric = original; numeric.getChildByName("Param")->setAttribute("value", number);
        check(load(numeric), "finite out-of-range value remains loadable");
        check(value(processor, "roomSize") == (number[0] == '-' ? 0.0f : 1.0f), "finite values retain range clamp");
    }
    auto arrangement = jam;
    parameter(arrangement, "bpm", "150");
    arrangement.createNewChildElement("Arrangement")->setAttribute("arr0Bars", "broken");
    unchanged(arrangement, "malformed arrangement rejects before any parameter change", PresetType::Jam);
    check(!PresetManager::loadUserFile(processor, PresetType::Kit, scratch.getChildFile("missing.rdpreset")), "missing file rejected");
    check(!PresetManager::loadUserFile(processor, static_cast<PresetType>(99), file), "invalid preset type rejected");
    check(!PresetManager::load(processor, PresetType::Kit, {"missing factory", true}), "missing factory rejected");
}

inline void measure(RoboDrummerAudioProcessor& processor, bool start = true, bool finalize = true) {
    if (start) processor.startAutoSetup();
    juce::AudioBuffer<float> buffer(std::max(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels()), 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 1250 && processor.getAutoSetupState() == robodrummer::AutoSetupState::Listening; ++block) {
        for (int sample = 0; sample < 512; ++sample) {
            const double position = std::fmod(static_cast<double>(block * 512 + sample), 14400.0);
            const auto input = static_cast<float>(0.4 * std::exp(-position / 9000.0) * std::sin(6.2831853 * 196.0 * position / 48000.0));
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) buffer.setSample(channel, sample, channel < 2 ? input : 0.0f);
        }
        midi.clear(); processor.processBlock(buffer, midi);
    }
    if (finalize) processor.finalizeAutoSetupIfReady();
}

template <class Check>
void lifecycle(const juce::File& scratch, Check check) {
    using robodrummer::AutoSetupState; using robodrummer::PresetManager; using robodrummer::PresetType;
    scratch.createDirectory();
    const auto file = scratch.getChildFile("lifecycle.rdpreset");
    juce::XmlElement valid("RoboDrummerPreset"); valid.setAttribute("type", "Kit"); valid.setAttribute("version", 1);
    parameter(valid, "roomSize", "0.25"); check(valid.writeTo(file), "write lifecycle fixture");
    RoboDrummerAudioProcessor processor; processor.prepareToPlay(48000.0, 512);
    measure(processor);
    check(processor.getAutoSetupState() == AutoSetupState::Done, "measurement completes");
    const auto original = processor.getAutoSetupCurrentValues();
    const auto proposal = processor.getAutoSetupResult().proposal;
    auto selected = proposal;
    selected.autoGain.apply = selected.gateThresholdDb.apply = selected.followStrength.apply = selected.tempoInertia.apply = false;
    selected.confidenceSensitivity.apply = selected.timingMode.apply = selected.leadership.apply = selected.bpm.apply = selected.jamStyle.apply = selected.intensity.apply = false;
    check(selected.inputGainDb.apply && processor.applyAutoSetup(selected) && processor.canUndoAutoSetup(), "fresh per-field Apply produces Undo");
    check(std::abs(value(processor, "bpm") - original.bpm) < 0.01f, "unselected fields remain unchanged");
    const auto generation = processor.getAutoSetupGeneration();
    juce::MemoryBlock before, after; processor.getStateInformation(before);
    juce::XmlElement invalid(valid); invalid.setAttribute("version", 9); check(invalid.writeTo(file), "write rejected lifecycle fixture");
    check(!PresetManager::loadUserFile(processor, PresetType::Kit, file), "invalid load rejected with pending Undo");
    check(processor.getAutoSetupGeneration() == generation && processor.canUndoAutoSetup(), "rejected load preserves generation/proposal/Undo");
    processor.getStateInformation(after); check(before == after, "rejected load preserves all live state");
    check(!PresetManager::load(processor, PresetType::Kit, {"missing factory", true}) && processor.canUndoAutoSetup(), "missing factory preserves Undo");
    const char garbage[] = "invalid state";
    processor.setStateInformation(garbage, static_cast<int>(sizeof(garbage)));
    check(processor.getAutoSetupGeneration() == generation && processor.canUndoAutoSetup(), "rejected host state preserves proposal and Undo");
    setPlain(processor, "intensity", 0.9f);
    check(processor.undoAutoSetup() && !processor.canUndoAutoSetup(), "fresh Undo is consumed once");
    check(std::abs(value(processor, "inputGain") - original.inputGainDb) < 0.001f && std::abs(value(processor, "intensity") - 0.9f) < 0.001f,
          "Undo restores selected field exactly and preserves later unselected edit");

    check(valid.writeTo(file), "restore valid lifecycle fixture");
    for (bool consumeBlock : {false, true}) {
        processor.startAutoSetup();
        if (consumeBlock) {
            juce::AudioBuffer<float> buffer(2, 512); buffer.clear(); juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
        }
        const auto run = processor.getAutoSetupGeneration();
        check(PresetManager::loadUserFile(processor, PresetType::Kit, file), "valid user load succeeds");
        check(processor.getAutoSetupGeneration() != run && processor.getAutoSetupState() == AutoSetupState::Cancelled,
              "successful user load invalidates queued/listening measurement immediately");
        check(!processor.applyAutoSetup(proposal) && !processor.canUndoAutoSetup(), "stale producer proposal/Undo unavailable");
    }
    measure(processor, true, false);
    check(processor.getAutoSetupState() == AutoSetupState::Analyzing, "measurement awaits message-thread finalization");
    check(PresetManager::load(processor, PresetType::Kit, {"Studio Kit", true}), "valid factory load succeeds");
    processor.finalizeAutoSetupIfReady();
    check(processor.getAutoSetupState() == AutoSetupState::Cancelled && !processor.applyAutoSetup(), "factory load invalidates queued finalization");

    auto previousSettings = rdui::EditorSettings::fileOverride();
    rdui::EditorSettings::fileOverride() = scratch.getChildFile("settings.xml"); rdui::EditorSettings::setBool("tourSeen", true);
    std::function<void()> afterEditorDestruction;
    {
        auto editor = std::unique_ptr<RoboDrummerAudioProcessorEditor>(static_cast<RoboDrummerAudioProcessorEditor*>(processor.createEditor()));
        auto* card = dynamic_cast<rdui::AutoSetupCard*>(find(*editor, "autoSetupCard"));
        check(card != nullptr, "real editor exposes Learn card");
        if (card != nullptr) {
            card->begin(); measure(processor, false); card->refresh();
            check(card->mode() == rdui::AutoSetupCard::Mode::Review, "real card enters review");
            auto* apply = dynamic_cast<juce::TextButton*>(find(*card, "autoSetupApply"));
            check(apply != nullptr, "review has Apply action");
            if (apply != nullptr) {
                auto stale = apply->onClick;
                const auto staleProposal = processor.getAutoSetupResult().proposal;
                check(PresetManager::load(processor, PresetType::Jam, {"Metal Assault", true}), "load new preset during review");
                processor.getStateInformation(before); stale(); processor.getStateInformation(after);
                check(before == after && value(processor, "bpm") == 160.0f, "retained stale Apply changes no parameter");
                check(card->mode() == rdui::AutoSetupCard::Mode::Hidden, "invalidated review closes");
                card->begin(); measure(processor, false); card->refresh();
                check(card->mode() == rdui::AutoSetupCard::Mode::Review, "fresh review after invalidation works");
                processor.getStateInformation(before); stale(); processor.getStateInformation(after);
                check(before == after && card->mode() == rdui::AutoSetupCard::Mode::Review, "old callback cannot apply or dismiss a later review");
                check(!processor.applyAutoSetup(staleProposal), "old proposal is rejected even after a newer result completes");
                auto stateStale = apply->onClick;
                processor.getStateInformation(before); processor.setStateInformation(before.getData(), static_cast<int>(before.getSize()));
                processor.getStateInformation(before); stateStale(); processor.getStateInformation(after);
                check(before == after && card->mode() == rdui::AutoSetupCard::Mode::Hidden, "state restore invalidates retained review callback");
                card->begin(); measure(processor, false); card->refresh();
                processor.getStateInformation(before);
                auto freshApply = apply->onClick;
                afterEditorDestruction = freshApply;
                freshApply();
                check(card->mode() == rdui::AutoSetupCard::Mode::Applied && processor.canUndoAutoSetup(), "fresh review Apply remains available");
                juce::MemoryBlock applied; processor.getStateInformation(applied);
                freshApply(); processor.getStateInformation(after);
                check(applied == after && processor.canUndoAutoSetup(), "repeated retained Apply cannot replace the original Undo");
                check(processor.undoAutoSetup(), "fresh review Undo works");
                processor.getStateInformation(after);
                check(before == after, "review Undo restores exact serialized parameter values");
                card->dismiss();
                card->begin();
                processor.invalidateAutoSetup(); processor.startAutoSetup();
                card->dismiss();
                check(processor.getAutoSetupState() == AutoSetupState::Listening, "old card dismissal cannot cancel a replacement run");
                processor.cancelAutoSetup();
            }
        }
    }
    processor.getStateInformation(before);
    if (afterEditorDestruction) afterEditorDestruction();
    processor.getStateInformation(after);
    check(before == after, "retained Apply is harmless after editor destruction");
    rdui::EditorSettings::fileOverride() = previousSettings;
    processor.releaseResources();
}
} // namespace preset_regressions
