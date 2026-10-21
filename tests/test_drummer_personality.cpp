#include "core/DrummerPersonality.h"
#include "core/SectionSequencer.h"
#include <cassert>
int main() {
    using namespace robodrummer;
    DrummerControls base;
    const auto balanced = applyPersonality(base, DrummerPersonality::Balanced);
    assert(balanced.complexity == base.complexity && balanced.feelMs == base.feelMs);
    assert(applyPersonality(base, DrummerPersonality::LaidBack).feelMs > 0.0f);
    assert(applyPersonality(base, DrummerPersonality::Pushing).feelMs < 0.0f);
    assert(applyPersonality(base, DrummerPersonality::Busy).complexity > base.complexity);
    assert(applyPersonality(base, DrummerPersonality::Busy).fillFrequency > base.fillFrequency);
    assert(applyPersonality(base, DrummerPersonality::Minimal).fillFrequency < base.fillFrequency);
    assert(applyPersonality(base, DrummerPersonality::Aggressive).crashTendency > 1.0f);
    assert(applyPersonality(base, DrummerPersonality::Tight).humanize < base.humanize);
    // Offsets never push controls out of range.
    DrummerControls extreme; extreme.complexity = 0.95f; extreme.fillFrequency = 0.95f; extreme.humanize = 0.0f; extreme.crashTendency = 1.9f;
    const auto clamped = applyPersonality(extreme, DrummerPersonality::Busy);
    assert(clamped.complexity <= 1.0f && clamped.fillFrequency <= 1.0f && clamped.humanize >= 0.0f);
    assert(applyPersonality(extreme, DrummerPersonality::Aggressive).crashTendency <= 2.0f);
    // Sections carry the SPEC 25 per-section fields with inherit defaults.
    SectionDefinition section;
    assert(section.leadership < 0.0f && section.followStrength < 0.0f && section.fillFrequency < 0.0f);
    assert(section.timeFeel == SectionTimeFeel::Normal && section.transition == SectionTransition::Crash);
}
