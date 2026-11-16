#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace parameter_text_regressions {

// Exercise the actual processor parameter adapters, including every custom
// formatter/parser. Quantized display text may round a value; its spelling
// must nevertheless survive the host's value -> text -> value -> text cycle.
template <class Check>
void roundTrips(const juce::AudioProcessor& processor, Check check) {
    for (const auto* parameter : processor.getParameters()) {
        const auto checkValue = [&](float normalized) {
            const auto text = parameter->getText(normalized, 128);
            check(text.isNotEmpty(), parameter->getName(128) + ": empty display text");
            if (text.isEmpty()) return;
            const float parsed = parameter->getValueForText(text);
            const bool valid = std::isfinite(parsed) && parsed >= 0.0f && parsed <= 1.0f;
            check(valid, parameter->getName(128) + ": invalid parsed value for '" + text + "'");
            if (!valid) return;
            const auto repeated = parameter->getText(parsed, 128);
            check(repeated == text, parameter->getName(128) + ": '" + text + "' -> "
                + juce::String(parsed, 8) + " -> '" + repeated + "'");
        };

        checkValue(parameter->getDefaultValue());
        for (int sample = 0; sample <= 128; ++sample)
            checkValue(static_cast<float>(sample) / 128.0f);
        const int steps = parameter->getNumSteps();
        if (steps > 1 && steps <= 4096)
            for (int step = 0; step < steps; ++step)
                checkValue(static_cast<float>(step) / static_cast<float>(steps - 1));
    }
}

} // namespace parameter_text_regressions
