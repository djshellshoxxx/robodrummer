#pragma once
#include "core/GrooveGenerator.h"
#include "core/JamState.h"
#include "core/Style.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace robodrummer {
class JamEngine {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; sampleCursor_ = 0; }
    void resetPhase() noexcept { sampleCursor_ = 0; }
    void setTempo(double bpm) noexcept { bpm_ = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0); }
    void setMeter(int n, int d) noexcept { numerator_ = std::max(1, n); denominator_ = (d == 1 || d == 2 || d == 4 || d == 8 || d == 16) ? d : 4; }
    void setIntensity(float value) noexcept { state_.intensity = std::clamp(value, 0.0f, 1.0f); }
    void apply(MidiCommand command) noexcept { applyMidiCommand(state_, command); }
    [[nodiscard]] const JamState& state() const noexcept { return state_; }
    [[nodiscard]] std::size_t processBlock(int numSamples, DrumEvent* out, std::size_t capacity) noexcept {
        if (numSamples <= 0 || out == nullptr || capacity == 0 || state_.stopped) { if (numSamples > 0) sampleCursor_ += numSamples; return 0; }
        const double effectiveBpm = bpm_ * state_.timeScale;
        const double spq = sampleRate_ * 60.0 / effectiveBpm;
        const double spb = spq * (4.0 / static_cast<double>(denominator_));
        const long long barSamples = std::max<long long>(1, static_cast<long long>(std::llround(spb * numerator_)));
        const long long blockStart = sampleCursor_;
        const long long blockEnd = blockStart + numSamples;
        const long long firstBar = blockStart / barSamples;
        const long long lastBar = (blockEnd - 1) / barSamples;
        std::size_t count = 0;
        for (long long bar = firstBar; bar <= lastBar && count < capacity; ++bar) {
            std::array<DrumEvent, 64> events{};
            GrooveContext ctx;
            ctx.sampleRate = sampleRate_;
            ctx.bpm = effectiveBpm;
            ctx.numerator = numerator_;
            ctx.denominator = denominator_;
            ctx.intensity = state_.intensity;
            ctx.sectionStart = (bar == 0);
            ctx.seed = static_cast<std::uint32_t>(0x5244424Du + static_cast<std::uint32_t>(bar));
            const auto eventCount = generator_.generateBar(style_, ctx, events);
            const long long barStart = bar * barSamples;
            for (std::size_t i = 0; i < eventCount && count < capacity; ++i) {
                const long long absolute = barStart + events[i].sampleOffset;
                if (absolute >= blockStart && absolute < blockEnd) {
                    auto e = events[i];
                    e.sampleOffset = static_cast<int>(absolute - blockStart);
                    out[count++] = e;
                }
            }
        }
        if (state_.crashRequested && count < capacity) { out[count++] = {DrumInstrument::Crash, 0, 1.0f, 0}; state_.crashRequested = false; }
        sampleCursor_ = blockEnd;
        std::sort(out, out + count, [](const DrumEvent& a, const DrumEvent& b){ return a.sampleOffset < b.sampleOffset; });
        return count;
    }
private:
    double sampleRate_{48000.0};
    double bpm_{120.0};
    int numerator_{4};
    int denominator_{4};
    long long sampleCursor_{0};
    Style style_{Style::basicRock()};
    GrooveGenerator generator_{};
    JamState state_{};
};
}
