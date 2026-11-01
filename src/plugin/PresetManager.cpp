// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "PresetManager.h"
#include "PluginProcessor.h"
#include <cmath>

namespace robodrummer {
namespace {
namespace ids = ParamIDs;

struct FactoryValue { const char* id; float value; };
struct FactoryPreset {
    PresetType type;
    const char* name;
    std::vector<FactoryValue> values;
    int song{0}; // 1 = verse/chorus rock, 2 = twelve-bar blues form
};

// Choice indices: jamStyle follows kJamStyleNames, drummer follows kDrummerPersonalityNames.
const std::vector<FactoryPreset>& factoryPresets() {
    static const std::vector<FactoryPreset> presets {
        // ---- Jam presets (whole-setup starting points) ----
        { PresetType::Jam, "Rock - Guitar Leads", { {ids::jamStyle, 0}, {ids::leadershipMode, 2}, {ids::leadership, 0.75f}, {ids::followRange, 25}, {ids::followResponse, 2}, {ids::drummer, 0} } },
        { PresetType::Jam, "Blues Shuffle - Hybrid", { {ids::jamStyle, 5}, {ids::leadershipMode, 1}, {ids::leadership, 0.55f}, {ids::followRange, 15}, {ids::followResponse, 1}, {ids::drummer, 2}, {ids::humanize, 0.65f} } },
        { PresetType::Jam, "Funk Pocket - Drummer Leads", { {ids::jamStyle, 2}, {ids::leadershipMode, 0}, {ids::drummer, 1}, {ids::complexity, 0.65f}, {ids::humanize, 0.35f} } },
        { PresetType::Jam, "Metal Assault", { {ids::jamStyle, 4}, {ids::leadershipMode, 0}, {ids::drummer, 4}, {ids::intensity, 0.8f}, {ids::bpm, 160}, {ids::crashTendency, 1.4f} } },
        { PresetType::Jam, "Punk Blitz", { {ids::jamStyle, 3}, {ids::leadershipMode, 1}, {ids::drummer, 3}, {ids::bpm, 175}, {ids::intensity, 0.8f}, {ids::fillFrequency, 0.7f} } },
        { PresetType::Jam, "Reggae Roots", { {ids::jamStyle, 15}, {ids::leadershipMode, 0}, {ids::bpm, 76}, {ids::drummer, 2}, {ids::intensity, 0.4f} } },
        { PresetType::Jam, "Disco Floor", { {ids::jamStyle, 16}, {ids::leadershipMode, 0}, {ids::bpm, 120}, {ids::drummer, 1}, {ids::humanize, 0.2f} } },
        { PresetType::Jam, "Jazz Brushes Feel", { {ids::jamStyle, 21}, {ids::leadershipMode, 1}, {ids::bpm, 140}, {ids::drummer, 2}, {ids::intensity, 0.35f}, {ids::humanize, 0.7f} } },
        { PresetType::Jam, "Half-Time Heavy", { {ids::jamStyle, 19}, {ids::leadershipMode, 0}, {ids::bpm, 140}, {ids::drummer, 4}, {ids::intensity, 0.7f} } },
        { PresetType::Jam, "Indie Jam - Full Follow", { {ids::jamStyle, 12}, {ids::leadershipMode, 2}, {ids::leadership, 1.0f}, {ids::followRange, 40}, {ids::followResponse, 3}, {ids::drummer, 0} } },
        // ---- Drummer presets ----
        { PresetType::Drummer, "Tight Studio", { {ids::drummer, 1}, {ids::humanize, 0.25f}, {ids::feel, 0}, {ids::complexity, 0.5f}, {ids::fillFrequency, 0.45f}, {ids::fillSurprise, 0.05f} } },
        { PresetType::Drummer, "Loose Garage", { {ids::drummer, 0}, {ids::humanize, 0.85f}, {ids::feel, 3}, {ids::complexity, 0.55f}, {ids::fillFrequency, 0.6f}, {ids::fillSurprise, 0.3f} } },
        { PresetType::Drummer, "Laid-back Groover", { {ids::drummer, 2}, {ids::humanize, 0.6f}, {ids::feel, 8}, {ids::complexity, 0.4f}, {ids::fillFrequency, 0.35f} } },
        { PresetType::Drummer, "Busy Showoff", { {ids::drummer, 5}, {ids::humanize, 0.5f}, {ids::complexity, 0.85f}, {ids::fillFrequency, 0.85f}, {ids::fillLength, 2}, {ids::fillComplexity, 0.85f}, {ids::fillSurprise, 0.5f} } },
        { PresetType::Drummer, "Minimal Support", { {ids::drummer, 6}, {ids::humanize, 0.4f}, {ids::complexity, 0.2f}, {ids::fillFrequency, 0.15f}, {ids::crashTendency, 0.4f} } },
        // ---- Style presets ----
        { PresetType::Style, "Classic Rock", { {ids::jamStyle, 6} } },
        { PresetType::Style, "Hard Rock", { {ids::jamStyle, 7} } },
        { PresetType::Style, "Grunge", { {ids::jamStyle, 9} } },
        { PresetType::Style, "Soul", { {ids::jamStyle, 10} } },
        { PresetType::Style, "Pop", { {ids::jamStyle, 11} } },
        { PresetType::Style, "Country", { {ids::jamStyle, 14} } },
        { PresetType::Style, "Electronic Rock", { {ids::jamStyle, 17} } },
        { PresetType::Style, "Breakbeat", { {ids::jamStyle, 18} } },
        // ---- Kit presets ----
        { PresetType::Kit, "Studio Kit", { {ids::kit, 0}, {ids::roomSize, 0.4f}, {ids::mixGain[6], -14} } },
        { PresetType::Kit, "Tight Kit", { {ids::kit, 1}, {ids::roomSize, 0.25f}, {ids::mixGain[6], -20} } },
        { PresetType::Kit, "Big Room Kit", { {ids::kit, 2}, {ids::roomSize, 0.85f}, {ids::mixGain[6], -6} } },
        { PresetType::Kit, "Lo-Fi Kit", { {ids::kit, 3}, {ids::roomSize, 0.3f}, {ids::mixGain[6], -16} } },
        // ---- Analysis presets ----
        { PresetType::Analysis, "Clean DI Guitar", { {ids::inputMode, 0}, {ids::inputGain, 0}, {ids::autoGain, 0}, {ids::gateThreshold, -90}, {ids::followResponse, 2} } },
        { PresetType::Analysis, "Noisy Amp (gated)", { {ids::inputMode, 0}, {ids::autoGain, 0}, {ids::gateThreshold, -55}, {ids::followResponse, 1} } },
        { PresetType::Analysis, "Quiet Guitar (auto gain)", { {ids::inputMode, 0}, {ids::autoGain, 1}, {ids::gateThreshold, -70}, {ids::followResponse, 2} } },
        { PresetType::Analysis, "Rock-solid Tracking", { {ids::followResponse, 0}, {ids::followRange, 10}, {ids::dynamicFollow, 0.4f} } },
        { PresetType::Analysis, "Push-and-Pull Tracking", { {ids::followResponse, 4}, {ids::followRange, 40}, {ids::dynamicFollow, 0.8f} } },
        // ---- Song presets ----
        { PresetType::Song, "Verse / Chorus Rock", { {ids::arrangementEnabled, 1} }, 1 },
        { PresetType::Song, "Twelve-Bar Blues Form", { {ids::arrangementEnabled, 1} }, 2 },
    };
    return presets;
}

ArrangementSlot slotOf(JamStyle style, int bars, float intensity, bool autoAdvance, SectionTransition transition,
                       DrummerPersonality personality = DrummerPersonality::Balanced, SectionTimeFeel feel = SectionTimeFeel::Normal) {
    ArrangementSlot slot;
    slot.section.style = style; slot.section.bars = bars; slot.section.intensityTarget = intensity;
    slot.section.autoAdvance = autoAdvance; slot.section.transition = transition;
    slot.section.personality = personality; slot.section.timeFeel = feel;
    return slot;
}

void applySong(RoboDrummerAudioProcessor& p, int song) {
    const char* names1[] = { "Intro", "Verse", "Pre-chorus", "Chorus", "Breakdown", "Outro" };
    const char* names2[] = { "Head", "Chorus 2", "Solo", "Turnaround", "Tag", "End" };
    if (song == 1) {
        const ArrangementSlot slots[6] = {
            slotOf(JamStyle::Rock, 4, 0.40f, true, SectionTransition::None, DrummerPersonality::Minimal),
            slotOf(JamStyle::Rock, 8, 0.50f, true, SectionTransition::Crash),
            slotOf(JamStyle::Rock, 4, 0.65f, true, SectionTransition::Fill),
            slotOf(JamStyle::HardRock, 8, 0.85f, true, SectionTransition::Fill, DrummerPersonality::Aggressive),
            slotOf(JamStyle::HalfTime, 4, 0.55f, true, SectionTransition::Break),
            slotOf(JamStyle::Rock, 8, 0.60f, false, SectionTransition::Crash, DrummerPersonality::Balanced),
        };
        for (int i = 0; i < 6; ++i) { p.setArrangementSlot(i, slots[i]); p.setArrangementName(i, names1[i]); }
    } else {
        const ArrangementSlot slots[6] = {
            slotOf(JamStyle::Blues, 12, 0.45f, true, SectionTransition::None, DrummerPersonality::LaidBack),
            slotOf(JamStyle::Shuffle, 12, 0.55f, true, SectionTransition::Fill, DrummerPersonality::LaidBack),
            slotOf(JamStyle::Shuffle, 12, 0.75f, true, SectionTransition::Crash, DrummerPersonality::Pushing),
            slotOf(JamStyle::Blues, 4, 0.60f, true, SectionTransition::Fill),
            slotOf(JamStyle::Blues, 8, 0.45f, true, SectionTransition::Crash, DrummerPersonality::Minimal),
            slotOf(JamStyle::Blues, 4, 0.35f, false, SectionTransition::Crash, DrummerPersonality::Minimal),
        };
        for (int i = 0; i < 6; ++i) { p.setArrangementSlot(i, slots[i]); p.setArrangementName(i, names2[i]); }
    }
}

juce::StringArray idsFor(PresetType type, RoboDrummerAudioProcessor& processor) {
    juce::StringArray result;
    const auto add = [&](std::initializer_list<const char*> list) { for (auto* id : list) result.add(id); };
    switch (type) {
        case PresetType::Style: add({ ids::jamStyle }); break;
        case PresetType::Drummer:
            add({ ids::drummer, ids::humanize, ids::feel, ids::complexity, ids::fillFrequency, ids::fillLength,
                  ids::fillComplexity, ids::fillSurprise, ids::crashTendency, ids::rlAmount, ids::rlSensitivity, ids::rlAccentCrash, ids::limbModelAmount });
            break;
        case PresetType::Kit:
            add({ ids::kit, ids::outputMode, ids::roomSize, ids::kitTune, ids::midiOutChannel,
                  ids::drumBusEnabled, ids::drumBusThreshold, ids::drumBusRatio, ids::drumBusKnee, ids::drumBusAttack, ids::drumBusRelease,
                  ids::drumBusMakeup, ids::drumBusDetect, ids::drumBusScHpf, ids::drumBusMix, ids::drumBusDrive,
                  ids::transientShaperEnabled, ids::transientShaperAttack, ids::transientShaperSustain, ids::transientShaperMix,
                  ids::transientShaperOutput });
            for (std::size_t i = 0; i < 8; ++i) { result.add(ids::mixGain[i]); result.add(ids::mixMute[i]); }
            for (std::size_t i = 0; i < 7; ++i) result.add(ids::mixPan[i]);
            for (std::size_t i = 0; i < 6; ++i) result.add(ids::mixSolo[i]);
            break;
        case PresetType::Song: add({ ids::arrangementEnabled }); break;
        case PresetType::Analysis:
            add({ ids::inputMode, ids::inputGain, ids::autoGain, ids::gateThreshold, ids::followResponse, ids::followRange,
                  ids::dynamicFollow, ids::silenceMode, ids::silenceStopBars, ids::followStrength, ids::tempoInertia, ids::autoMeterEnabled, ids::autoMeterThreshold,
                  ids::beatCorrection, ids::confidenceSensitivity, ids::guitarMonitor });
            break;
        case PresetType::Jam:
            for (auto* parameter : processor.getParameters().processor.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                    if (withId->paramID != ids::practiceEnabled && withId->paramID != ids::pracLoopOn) // a session never starts from a preset
                        result.add(withId->paramID);
            break;
    }
    return result;
}

// Preset files are plain XML users can edit or share: non-finite values are ignored (the parameter keeps its value)
// and out-of-range ones are clamped, so a damaged file can never put NaN/Inf on the audio thread.
bool setPlain(RoboDrummerAudioProcessor& processor, const juce::String& id, float plain, std::uint64_t generation) {
    if (processor.getAutoSetupGeneration() != generation) return false;
    if (!std::isfinite(plain)) return true;
    if (auto* parameter = processor.getParameters().getParameter(id)) {
        parameter->beginChangeGesture();
        if (processor.getAutoSetupGeneration() == generation)
            parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, parameter->convertTo0to1(plain)));
        parameter->endChangeGesture();
    }
    return processor.getAutoSetupGeneration() == generation;
}

bool includesArrangement(PresetType type) noexcept { return type == PresetType::Jam || type == PresetType::Song; }
bool includesKitPath(PresetType type) noexcept { return type == PresetType::Jam || type == PresetType::Kit; }

// Match a complete decimal token before JUCE's locale-independent conversion.
// Its permissive getDoubleValue() alone accepts prefixes such as "0.5tail".
bool numericToken(const juce::String& input) {
    const auto text = input.trim();
    if (text.isEmpty()) return false;
    auto p = text.getCharPointer();
    if (*p == '+' || *p == '-') ++p;
    const juce::String unsignedText(p);
    if (unsignedText.equalsIgnoreCase("nan") || unsignedText.equalsIgnoreCase("inf") || unsignedText.equalsIgnoreCase("infinity")) return true;
    bool digits = false;
    while (*p >= '0' && *p <= '9') { digits = true; ++p; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') { digits = true; ++p; }
    }
    if (!digits) return false;
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        bool exponent = false;
        while (*p >= '0' && *p <= '9') { exponent = true; ++p; }
        if (!exponent) return false;
    }
    return p.isEmpty();
}

bool validArrangement(const juce::XmlElement& xml, juce::ValueTree& result) {
    result = juce::ValueTree::fromXml(xml);
    if (!result.isValid()) return false;
    constexpr const char* numericFields[] { "Style", "Bars", "Intensity", "Feel", "Transition", "Personality", "FillFreq", "Leadership", "Follow" };
    for (int slot = 0; slot < RoboDrummerAudioProcessor::ArrangementSlotCount; ++slot) {
        const auto prefix = juce::String("arr") + juce::String(slot);
        for (const auto* field : numericFields) {
            const juce::Identifier id(prefix + field);
            if (!result.hasProperty(id)) continue;
            const auto text = result.getProperty(id).toString();
            if (!numericToken(text)) return false;
            // Retain historical NaN/Inf tolerance without passing them through integer casts.
            if (!std::isfinite(static_cast<float>(text.getDoubleValue()))) result.removeProperty(id, nullptr);
        }
        for (const auto* field : { "Enabled", "Auto" }) {
            const juce::Identifier id(prefix + field);
            if (!result.hasProperty(id)) continue;
            const auto text = result.getProperty(id).toString().trim();
            if (text != "0" && text != "1" && !text.equalsIgnoreCase("true") && !text.equalsIgnoreCase("false")) return false;
        }
    }
    return true;
}
} // namespace

const char* PresetManager::typeName(PresetType type) noexcept {
    switch (type) {
        case PresetType::Jam: return "Jam";
        case PresetType::Drummer: return "Drummer";
        case PresetType::Style: return "Style";
        case PresetType::Kit: return "Kit";
        case PresetType::Song: return "Song";
        case PresetType::Analysis: return "Analysis";
    }
    return "Jam";
}

juce::File PresetManager::userDirectory(PresetType type) {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Circuit Drift Labs").getChildFile("RoboDrummer").getChildFile("Presets").getChildFile(typeName(type));
}

juce::String PresetManager::sanitiseName(const juce::String& name) {
    return juce::File::createLegalFileName(name.trim()).substring(0, 64);
}

std::vector<PresetEntry> PresetManager::list(PresetType type) {
    std::vector<PresetEntry> entries;
    for (const auto& preset : factoryPresets())
        if (preset.type == type) entries.push_back({ preset.name, true });
    auto files = userDirectory(type).findChildFiles(juce::File::findFiles, false, "*.rdpreset");
    files.sort();
    for (const auto& file : files) entries.push_back({ file.getFileNameWithoutExtension(), false });
    return entries;
}

bool PresetManager::load(RoboDrummerAudioProcessor& processor, PresetType type, const PresetEntry& entry) {
    if (entry.factory) {
        for (const auto& preset : factoryPresets()) {
            if (preset.type != type || entry.name != preset.name) continue;
            const auto transaction = processor.lockNonRealtimeStateChange();
            processor.invalidateAutoSetup();
            const auto generation = processor.getAutoSetupGeneration();
            for (const auto& value : preset.values)
                if (!setPlain(processor, value.id, value.value, generation)) return false;
            if (preset.song != 0) applySong(processor, preset.song);
            return true;
        }
        return false;
    }

    const auto file = userDirectory(type).getChildFile(sanitiseName(entry.name) + ".rdpreset");
    return loadUserFile(processor, type, file);
}

bool PresetManager::loadUserFile(RoboDrummerAudioProcessor& processor, PresetType type, const juce::File& file) {
    const auto index = static_cast<int>(type);
    if (index < 0 || index >= kPresetTypeCount) return false;
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("RoboDrummerPreset")) return false;
    // Every version emitted by RoboDrummer writes type/version=1. Older hand-edited
    // overlays omitted these attributes, so missing values remain compatible.
    if (xml->hasAttribute("type") && xml->getStringAttribute("type") != typeName(type)) return false;
    if (xml->hasAttribute("version") && xml->getStringAttribute("version").trim() != "1") return false;
    const auto allowed = idsFor(type, processor);
    juce::StringArray seen;
    std::vector<std::pair<juce::String, float>> values;
    for (auto* node : xml->getChildWithTagNameIterator("Param")) {
        if (!node->hasAttribute("id") || !node->hasAttribute("value")) return false;
        const auto id = node->getStringAttribute("id");
        if (!allowed.contains(id)) continue; // forward-compatible unknown fields and other preset slices
        if (seen.contains(id) || !numericToken(node->getStringAttribute("value"))) return false;
        seen.add(id);
        const auto value = static_cast<float>(node->getDoubleAttribute("value"));
        if (std::isfinite(value)) values.emplace_back(id, value); // historical nonfinite-ignore policy
    }
    juce::ValueTree arrangement;
    if (includesArrangement(type)) {
        int count = 0;
        for (auto* node : xml->getChildWithTagNameIterator("Arrangement")) {
            if (++count > 1 || !validArrangement(*node, arrangement)) return false;
        }
    }
    // Commit only after the complete file has validated. A failed load must
    // preserve parameters, arrangement, the pending proposal and its Undo.
    const auto transaction = processor.lockNonRealtimeStateChange();
    processor.invalidateAutoSetup();
    const auto generation = processor.getAutoSetupGeneration();
    for (const auto& [id, value] : values)
        if (!setPlain(processor, id, value, generation)) return false;
    if (arrangement.isValid()) processor.applyArrangementState(arrangement);
    if (includesKitPath(type) && xml->hasAttribute("userKitPath")) {
        processor.setUserKitFolder(juce::File(xml->getStringAttribute("userKitPath")));
        processor.requestKitReload();
    }
    return true;
}

bool PresetManager::saveUser(RoboDrummerAudioProcessor& processor, PresetType type, const juce::String& name) {
    const auto safe = sanitiseName(name);
    if (safe.isEmpty()) return false;
    auto directory = userDirectory(type);
    if (!directory.createDirectory().wasOk()) return false;

    juce::XmlElement root("RoboDrummerPreset");
    root.setAttribute("type", typeName(type));
    root.setAttribute("name", name.trim());
    root.setAttribute("version", 1);
    for (const auto& id : idsFor(type, processor)) {
        if (auto* parameter = processor.getParameters().getParameter(id)) {
            auto* node = root.createNewChildElement("Param");
            node->setAttribute("id", id);
            node->setAttribute("value", static_cast<double>(parameter->convertFrom0to1(parameter->getValue())));
        }
    }
    if (includesArrangement(type))
        if (auto xml = processor.createArrangementState().createXml()) root.addChildElement(xml.release());
    if (includesKitPath(type))
        root.setAttribute("userKitPath", processor.getUserKitFolder().getFullPathName());
    return root.writeTo(directory.getChildFile(safe + ".rdpreset"));
}

bool PresetManager::deleteUser(PresetType type, const juce::String& name) {
    return userDirectory(type).getChildFile(sanitiseName(name) + ".rdpreset").deleteFile();
}

int PresetManager::factoryProgramCount() {
    int count = 0;
    for (const auto& preset : factoryPresets()) count += preset.type == PresetType::Jam ? 1 : 0;
    return count;
}

juce::String PresetManager::factoryProgramName(int index) {
    int i = 0;
    for (const auto& preset : factoryPresets())
        if (preset.type == PresetType::Jam && i++ == index) return preset.name;
    return {};
}

bool PresetManager::loadFactoryProgram(RoboDrummerAudioProcessor& processor, int index) {
    return load(processor, PresetType::Jam, { factoryProgramName(index), true });
}

} // namespace robodrummer
