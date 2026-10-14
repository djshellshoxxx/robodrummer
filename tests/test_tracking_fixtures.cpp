// Deterministic synthetic fixtures for the guitar tempo/beat tracker (SPEC 36): steady, ramp, missing events,
// syncopation, silence, half/double ambiguity, sudden change and noisy transients.
#include "analysis/RhythmAnalyzer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
using namespace robodrummer;
constexpr double kSr = 48000.0;

struct Hit { double seconds; float amplitude; };

class Renderer {
public:
    explicit Renderer(double noiseLevel = 0.0, std::uint32_t seed = 1) : noise_(noiseLevel), rng_(seed) {}
    // Renders [startSample, startSample+n) from the hit list.
    void render(const std::vector<Hit>& hits, long long startSample, int n, float* out) {
        for (int i = 0; i < n; ++i) {
            double v = 0.0;
            const double t = static_cast<double>(startSample + i) / kSr;
            for (auto it = std::lower_bound(hits.begin(), hits.end(), t - 0.2, [](const Hit& h, double x) { return h.seconds < x; });
                 it != hits.end() && it->seconds <= t; ++it) {
                const double dt = t - it->seconds;
                v += it->amplitude * std::exp(-dt / 0.05) * std::sin(6.2831853 * 196.0 * dt) + it->amplitude * 0.3 * std::exp(-dt / 0.004) * std::sin(6.2831853 * 1800.0 * dt);
            }
            if (noise_ > 0.0) {
                rng_ = rng_ * 1664525u + 1013904223u;
                v += noise_ * (static_cast<double>(rng_ >> 8) / 8388608.0 - 1.0);
            }
            out[i] = static_cast<float>(v);
        }
    }
private:
    double noise_;
    std::uint32_t rng_;
};

struct Run {
    LiveRhythmAnalyzer analyzer;
    Renderer renderer;
    long long cursor{0};
    explicit Run(double noise = 0.0) : renderer(noise) { analyzer.prepare(kSr); }
    void advance(const std::vector<Hit>& hits, double seconds) {
        std::vector<float> block(256);
        const long long end = cursor + static_cast<long long>(seconds * kSr);
        while (cursor < end) {
            renderer.render(hits, cursor, 256, block.data());
            analyzer.processBlock(block.data(), 256);
            cursor += 256;
        }
    }
    RhythmState state() const { return analyzer.state(); }
};

std::vector<Hit> steady(double bpm, double seconds, double start = 0.0, double accent = 0.8) {
    std::vector<Hit> hits;
    const double period = 60.0 / bpm;
    int beat = 0;
    for (double t = start; t < seconds; t += period, ++beat) hits.push_back({ t, static_cast<float>(beat % 4 == 0 ? accent : accent * 0.6) });
    return hits;
}

bool hasHypothesisNear(const LiveRhythmAnalyzer& a, double bpm, double tolerance) {
    for (const auto& h : a.tempoTracker().topHypotheses())
        if (h.confidence > 0.05f && std::abs(h.bpm - bpm) <= tolerance) return true;
    return false;
}
} // namespace

int main() {
    // 1. steady metronomic input at several tempi
    for (const double bpm : { 70.0, 100.0, 120.0, 150.0, 190.0 }) {
        Run run;
        run.advance(steady(bpm, 14.0), 14.0);
        const auto s = run.state();
        std::printf("steady %.0f -> %.2f conf %.2f\n", bpm, s.tempoBpm, s.tempoConfidence);
        assert(std::abs(s.tempoBpm - bpm) < 2.0);
        assert(s.locked && s.tempoConfidence > 0.7f);
        assert(s.movement == TempoMovement::Steady || s.movement == TempoMovement::Jitter);
    }

    // 2. gradual acceleration 100 -> 124 BPM over 20 s is followed and classified
    {
        std::vector<Hit> hits;
        double t = 0.0, beat = 0.0;
        while (t < 34.0) {
            hits.push_back({ t, beat - 4 * std::floor(beat / 4) < 0.5 ? 0.8f : 0.5f });
            const double bpm = 100.0 + 24.0 * std::clamp((t - 6.0) / 20.0, 0.0, 1.0);
            t += 60.0 / bpm; beat += 1.0;
        }
        Run run;
        run.advance(hits, 6.0);
        bool sawAccelerating = false;
        for (int i = 0; i < 28; ++i) {
            run.advance(hits, 1.0);
            sawAccelerating |= run.state().movement == TempoMovement::Accelerating;
        }
        const auto s = run.state();
        std::printf("ramp -> %.2f (target 124) movement %d velocity %.2f\n", s.tempoBpm, static_cast<int>(s.movement), s.tempoVelocity);
        assert(std::abs(s.tempoBpm - 124.0) < 3.0);
        assert(sawAccelerating);
        assert(s.locked);
    }

    // 3. missing events: every fifth beat is dropped, tempo is unaffected
    {
        auto hits = steady(110.0, 16.0);
        std::vector<Hit> thinned;
        for (std::size_t i = 0; i < hits.size(); ++i) if (i % 5 != 4) thinned.push_back(hits[i]);
        Run run; run.advance(thinned, 16.0);
        assert(std::abs(run.state().tempoBpm - 110.0) < 2.5 && run.state().locked);
    }

    // 4. extra / syncopated events (eighths and off-beat accents) do not change the quarter-note pulse
    {
        std::vector<Hit> hits;
        const double period = 60.0 / 100.0;
        for (int beat = 0; beat < 40; ++beat) {
            hits.push_back({ beat * period, beat % 4 == 0 ? 0.9f : 0.55f });
            hits.push_back({ beat * period + period * 0.5, beat % 2 ? 0.7f : 0.3f });          // off-beat eighths
            if (beat % 3 == 0) hits.push_back({ beat * period + period * 0.75, 0.25f });      // a stray sixteenth
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.seconds < b.seconds; });
        Run run; run.advance(hits, 24.0);
        const auto s = run.state();
        std::printf("syncopated -> %.2f\n", s.tempoBpm);
        assert(s.locked);
        assert(std::abs(s.tempoBpm - 100.0) < 3.0 || std::abs(s.tempoBpm - 200.0) < 4.0); // pulse or its double
        assert(hasHypothesisNear(run.analyzer, 100.0, 3.0));
    }

    // 5. silence releases the lock and playing again re-acquires without a restart
    {
        auto hits = steady(120.0, 10.0);
        auto resumed = steady(120.0, 26.0, 16.0);
        hits.insert(hits.end(), resumed.begin(), resumed.end());
        Run run; run.advance(hits, 10.0);
        assert(run.state().locked);
        run.advance(hits, 6.0);
        assert(!run.state().locked);
        run.advance(hits, 9.0);
        const auto s = run.state();
        assert(s.locked && std::abs(s.tempoBpm - 120.0) < 3.0);
    }

    // 6. half/double ambiguity: an even eighth-note stream keeps the true pulse among the hypotheses
    {
        std::vector<Hit> hits;
        for (double t = 0.0; t < 20.0; t += 0.25) hits.push_back({ t, 0.6f });
        Run run; run.advance(hits, 20.0);
        assert(run.state().locked);
        assert(hasHypothesisNear(run.analyzer, 120.0, 3.0));
        assert(hasHypothesisNear(run.analyzer, 240.0, 4.0) || hasHypothesisNear(run.analyzer, 60.0, 3.0));
        const auto rich = run.analyzer.richHypotheses();
        for (const auto& h : rich) std::printf("hyp %.1f conf %.2f support %d cons %.2f age %.1f rel %d\n", h.bpm, h.confidence, h.supportingEvents, h.consistency, h.ageSeconds, static_cast<int>(h.relationToBest));
        assert(rich[0].supportingEvents >= 8 && rich[0].consistency > 0.4f && rich[0].ageSeconds > 2.0);
        assert(rich[0].relationToBest == HypothesisRelation::Primary);
        bool sawRelation = false;
        for (std::size_t i = 1; i < rich.size(); ++i) sawRelation |= rich[i].relationToBest == HypothesisRelation::HalfTime || rich[i].relationToBest == HypothesisRelation::DoubleTime;
        assert(sawRelation);
    }

    // 7. sudden tempo change 100 -> 140: classified as abrupt and re-acquired quickly
    {
        auto hits = steady(100.0, 12.0);
        auto fast = steady(140.0, 40.0, 12.0);
        hits.insert(hits.end(), fast.begin(), fast.end());
        Run run; run.advance(hits, 12.0);
        assert(std::abs(run.state().tempoBpm - 100.0) < 2.0);
        bool sawAbrupt = false;
        double settled = -1.0;
        for (int i = 0; i < 160; ++i) {
            run.advance(hits, 0.1);
            sawAbrupt |= run.state().movement == TempoMovement::AbruptShift;
            if (settled < 0.0 && std::abs(run.state().tempoBpm - 140.0) < 3.0) settled = (i + 1) * 0.1;
        }
        std::printf("sudden change settled after %.1f s (abrupt seen: %d)\n", settled, sawAbrupt);
        assert(sawAbrupt);
        assert(settled > 0.0 && settled < 9.0);
    }

    // 8. noisy guitar-like transients with +/-12 ms timing jitter and a noise floor
    {
        std::vector<Hit> hits;
        std::uint32_t rng = 99;
        const double period = 60.0 / 96.0;
        for (int beat = 0; beat < 40; ++beat) {
            rng = rng * 1664525u + 1013904223u;
            const double jitter = ((static_cast<double>(rng >> 8) / 8388608.0) - 1.0) * 0.012;
            hits.push_back({ beat * period + jitter, beat % 4 == 0 ? 0.8f : 0.5f });
        }
        Run run(0.01); run.advance(hits, 24.0);
        const auto s = run.state();
        std::printf("noisy -> %.2f conf %.2f\n", s.tempoBpm, s.tempoConfidence);
        assert(std::abs(s.tempoBpm - 96.0) < 3.0 && s.locked);
    }

    // 9. a pure noise floor never produces a lock
    {
        Run run(0.02); run.advance({}, 20.0);
        assert(!run.state().locked && run.state().tempoConfidence < 0.3f);
    }
    std::printf("tracking fixtures ok\n");
}
