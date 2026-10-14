// SPEC 6/7/18: spectral-flux onsets, band energies, density, regularity, accents, silence probability.
#include "analysis/OnsetDetector.h"
#include "analysis/PerformanceAnalyzer.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    using namespace robodrummer;
    constexpr double sr = 48000.0;

    // Spectral flux: constant-level playing whose spectrum changes (a "chord change" from low to high content)
    // is detected even though the broadband level never moves.
    {
        AdaptiveOnsetDetector detector; detector.prepare(sr);
        std::vector<float> audio(static_cast<std::size_t>(sr * 6.0));
        for (std::size_t i = 0; i < audio.size(); ++i) {
            const double t = static_cast<double>(i) / sr;
            const bool low = static_cast<int>(t / 0.75) % 2 == 0;
            audio[i] = static_cast<float>(0.3 * std::sqrt(2.0) * std::sin(6.2831853 * (low ? 110.0 : 3300.0) * t));
        }
        std::array<OnsetEvent, 64> events{};
        std::size_t total = 0;
        for (std::size_t pos = 0; pos < audio.size(); pos += 512)
            total += detector.processBlock(audio.data() + pos, static_cast<int>(std::min<std::size_t>(512, audio.size() - pos)), events.data(), events.size());
        assert(total >= 5 && total <= 12);
    }
    // Onset probability is reported and bounded.
    {
        AdaptiveOnsetDetector detector; detector.prepare(sr);
        std::vector<float> audio(static_cast<std::size_t>(sr), 0.0f);
        for (int hit = 0; hit < 4; ++hit)
            for (int i = 0; i < 3000; ++i) audio[static_cast<std::size_t>(hit * 12000 + 2000 + i)] = static_cast<float>(0.8 * std::exp(-i / 500.0) * std::sin(i * 0.1));
        std::array<OnsetEvent, 16> events{};
        const auto n = detector.processBlock(audio.data(), static_cast<int>(audio.size()), events.data(), events.size());
        assert(n >= 3);
        for (std::size_t i = 0; i < n; ++i) assert(events[i].probability >= 0.5f - 1.0e-4f && events[i].probability <= 1.0f);
    }

    // Band energies separate a bass note from a bright pick attack.
    {
        PerformanceAnalyzer bass, bright;
        bass.prepare(sr); bright.prepare(sr);
        std::array<float, 480> a{}, b{};
        for (int block = 0; block < 60; ++block) {
            for (std::size_t i = 0; i < a.size(); ++i) {
                const double t = static_cast<double>(block * 480 + static_cast<int>(i)) / sr;
                a[i] = static_cast<float>(0.4 * std::sin(6.2831853 * 90.0 * t));
                b[i] = static_cast<float>(0.4 * std::sin(6.2831853 * 4200.0 * t));
            }
            bass.processBlock(a.data(), 480); bright.processBlock(b.data(), 480);
        }
        std::printf("bass L/M/H %.2f %.2f %.2f   bright %.2f %.2f %.2f\n", bass.state().bandLow, bass.state().bandMid, bass.state().bandHigh, bright.state().bandLow, bright.state().bandMid, bright.state().bandHigh);
        // Band values are sqrt-compressed, so compare with a generous margin.
        assert(bass.state().bandLow > bass.state().bandHigh * 3.0f);
        assert(bright.state().bandHigh > bright.state().bandLow * 2.0f);
    }

    // Density, regularity, accents and silence.
    {
        PerformanceAnalyzer metronomic, erratic;
        metronomic.prepare(sr); erratic.prepare(sr);
        std::array<float, 480> noise{};
        double t = 0.0;
        std::uint32_t rng = 5;
        double nextErratic = 0.0;
        for (int block = 0; block < 1000; ++block) {
            for (std::size_t i = 0; i < noise.size(); ++i) { rng = rng * 1664525u + 1013904223u; noise[i] = 0.05f * (static_cast<float>(rng >> 8) / 8388608.0f - 1.0f); }
            metronomic.processBlock(noise.data(), 480); erratic.processBlock(noise.data(), 480);
            t += 480.0 / sr;
            if (block % 25 == 0) metronomic.noteOnset(t, block % 100 == 0 ? 0.9f : 0.5f);
            if (t >= nextErratic) {
                erratic.noteOnset(t, 0.5f);
                rng = rng * 1664525u + 1013904223u;
                nextErratic = t + 0.12 + 0.9 * (static_cast<double>(rng >> 8) / 16777216.0);
            }
        }
        assert(metronomic.state().regularity > 0.9f);
        assert(erratic.state().regularity < 0.5f);
        assert(metronomic.state().transientDensity > 0.0f && metronomic.state().transientDensity < 0.6f);
        // The last metronomic onset was a plain beat, an accent now must score higher.
        PerformanceAnalyzer p = metronomic;
        p.noteOnset(t + 0.01, 0.5f); const float plain = p.state().accentProbability; (void) plain;
        PerformanceAnalyzer plainA = metronomic, accentA = metronomic;
        plainA.noteOnset(t + 0.1, 0.5f); accentA.noteOnset(t + 0.1, 1.0f);
        plainA.processBlock(noise.data(), 480); accentA.processBlock(noise.data(), 480);
        assert(accentA.state().accentProbability > plainA.state().accentProbability + 0.2f);

        PerformanceAnalyzer quietAnalyzer, playing;
        quietAnalyzer.prepare(sr); playing.prepare(sr);
        std::array<float, 480> silence{}, loud{};
        for (std::size_t i = 0; i < loud.size(); ++i) loud[i] = 0.3f * std::sin(static_cast<float>(i) * 0.2f);
        for (int i = 0; i < 60; ++i) { quietAnalyzer.processBlock(silence.data(), 480); playing.processBlock(loud.data(), 480); }
        assert(quietAnalyzer.state().silenceProbability > 0.9f);
        assert(playing.state().silenceProbability < 0.1f);
    }

    // The intensity model reacts to more than loudness: denser, brighter playing at equal level reads as more intense.
    {
        PerformanceAnalyzer sparse, dense;
        sparse.prepare(sr); dense.prepare(sr);
        std::array<float, 480> sine{};
        for (std::size_t i = 0; i < sine.size(); ++i) sine[i] = 0.15f * std::sin(static_cast<float>(i) * 0.05f);
        double t = 0.0;
        for (int block = 0; block < 600; ++block) {
            sparse.processBlock(sine.data(), 480); dense.processBlock(sine.data(), 480);
            t += 480.0 / sr;
            if (block % 100 == 0) sparse.noteOnset(t, 0.5f);
            if (block % 6 == 0) dense.noteOnset(t, 0.8f);
        }
        assert(dense.state().intensity > sparse.state().intensity + 0.05f);
    }
}
