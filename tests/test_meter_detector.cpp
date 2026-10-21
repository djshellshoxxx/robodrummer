#include "analysis/MeterDetector.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace robodrummer;

namespace {

struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xFFFFFF) / 16777216.0f; }
    float tri() { return next() + next() - 1.0f; }  // -1..1 triangular
};

struct Onset { double pos; float strength; };

enum class Kind { FourFour, ThreeFour, FiveFour, SixEightTernary, SixEightSixPulse, SevenEight, Noise, Flat };

// Guitar-like stream: accented strum on bar starts / group starts, weaker strums elsewhere, off-beat subdivisions.
std::vector<Onset> synth(Kind kind, std::uint32_t seed, int bars, float jitter = 0.03f, float miss = 0.10f) {
    Rng rng(seed);
    std::vector<float> accent;
    bool ternarySub = false;
    switch (kind) {
        case Kind::FourFour: accent = {1.0f, 0.45f, 0.75f, 0.45f}; break;
        case Kind::ThreeFour: accent = {1.0f, 0.45f, 0.45f}; break;
        case Kind::FiveFour: accent = {1.0f, 0.4f, 0.4f, 0.75f, 0.4f}; break;
        case Kind::SixEightTernary: accent = {1.0f, 0.7f}; ternarySub = true; break;
        case Kind::SixEightSixPulse: accent = {1.0f, 0.3f, 0.3f, 0.65f, 0.3f, 0.3f}; break;
        case Kind::SevenEight: {
            const int g = static_cast<int>(rng.next() * 3.0f) % 3;
            if (g == 0) accent = {1.0f, 0.35f, 0.65f, 0.35f, 0.65f, 0.35f, 0.35f};       // 2+2+3
            else if (g == 1) accent = {1.0f, 0.35f, 0.35f, 0.65f, 0.35f, 0.65f, 0.35f};  // 3+2+2
            else accent = {1.0f, 0.35f, 0.65f, 0.35f, 0.35f, 0.65f, 0.35f};              // 2+3+2
            break;
        }
        case Kind::Noise: accent = std::vector<float>(4, 0.5f); break;
        case Kind::Flat: accent = std::vector<float>(4, 0.6f); break;
    }
    const int L = static_cast<int>(accent.size());
    std::vector<Onset> out;
    for (int bar = 0; bar < bars; ++bar) {
        for (int p = 0; p < L; ++p) {
            const double base = static_cast<double>(bar * L + p);
            float a = accent[static_cast<std::size_t>(p)];
            if (kind == Kind::Noise) a = 0.15f + 0.85f * rng.next();
            else if (kind != Kind::Flat) a = std::clamp(a * (1.0f + 0.15f * rng.tri()), 0.05f, 1.0f);
            if (rng.next() >= miss) out.push_back({base + 0.02 * rng.tri(), a});
            // Subdivisions.
            if (kind == Kind::Noise || kind == Kind::Flat) continue;
            if (ternarySub) {
                for (const double ph : {1.0 / 3.0, 2.0 / 3.0})
                    if (rng.next() >= miss + 0.05f) out.push_back({base + ph + jitter * rng.tri(), 0.30f + 0.1f * rng.next()});
            } else if (rng.next() >= miss + 0.15f) {
                out.push_back({base + 0.5 + jitter * rng.tri(), 0.25f + 0.1f * rng.next()});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Onset& a, const Onset& b) { return a.pos < b.pos; });
    return out;
}

MeterDetector::Suggestion run(const std::vector<Onset>& stream, int* changes = nullptr, int* lockedAt = nullptr) {
    MeterDetector det;
    int lastId = 404;
    int nChanges = 0;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        det.observeOnset(stream[i].pos, stream[i].strength);
        const auto s = det.suggestion();
        const int id = s.numerator * 100 + s.denominator;
        if (id != lastId) { ++nChanges; lastId = id; if (lockedAt != nullptr) *lockedAt = static_cast<int>(i); }
    }
    if (changes != nullptr) *changes = nChanges - 1;  // first sample is the initial 4/4
    return det.suggestion();
}

int accuracy(Kind kind, int bars, int expectedNum, int expectedDen, const char* name) {
    int ok = 0;
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        const auto s = run(synth(kind, seed, bars));
        if (s.numerator == expectedNum && s.denominator == expectedDen) ++ok;
    }
    std::printf("%-18s %3d/100\n", name, ok); std::fflush(stdout);
    return ok;
}

void testAccuracy() {
    assert(accuracy(Kind::FourFour, 16, 4, 4, "4/4") >= 85);
    assert(accuracy(Kind::ThreeFour, 20, 3, 4, "3/4") >= 85);
    assert(accuracy(Kind::FiveFour, 20, 5, 4, "5/4") >= 85);
    assert(accuracy(Kind::SixEightTernary, 24, 6, 8, "6/8 (2 pulses)") >= 85);
    assert(accuracy(Kind::SixEightSixPulse, 20, 6, 8, "6/8 (6 pulses)") >= 85);
    assert(accuracy(Kind::SevenEight, 24, 7, 8, "7/8") >= 85);
}

void testHarshConditions() {
    // Sloppier player: 20% missing events, larger timing jitter. Still mostly right, and never a wild guess.
    const struct { Kind k; int n, d; } cases[] = {{Kind::ThreeFour, 3, 4}, {Kind::FiveFour, 5, 4}, {Kind::SixEightTernary, 6, 8}, {Kind::SevenEight, 7, 8}};
    for (const auto& c : cases) {
        int ok = 0;
        for (std::uint32_t seed = 1; seed <= 100; ++seed) {
            const auto s = run(synth(c.k, seed, 24, 0.05f, 0.20f));
            if (s.numerator == c.n && s.denominator == c.d) ++ok;
        }
        std::printf("harsh %d/%d %3d/100\n", c.n, c.d, ok);
        assert(ok >= 70);
    }
}

void testAmbiguousDefaultsToFourFour() {
    int noiseOk = 0, flatOk = 0;
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        const auto n = run(synth(Kind::Noise, seed, 20));
        if (n.numerator == 4 && n.denominator == 4) ++noiseOk;
        const auto f = run(synth(Kind::Flat, seed, 20));
        if (f.numerator == 4 && f.denominator == 4 && f.confidence < 0.3f) ++flatOk;
    }
    std::printf("noise->4/4 %d/100, flat->4/4 %d/100\n", noiseOk, flatOk);
    assert(noiseOk >= 95);
    assert(flatOk >= 99);

    // Beat 3 as strong as beat 1 (half-bar repeat) is still 4/4, never 2/4 or 6/8.
    int halfOk = 0;
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        Rng rng(seed);
        std::vector<Onset> st;
        for (int b = 0; b < 16; ++b)
            for (int p = 0; p < 4; ++p)
                st.push_back({static_cast<double>(b * 4 + p) + 0.02 * rng.tri(), (p % 2 == 0 ? 1.0f : 0.4f) * (1.0f + 0.1f * rng.tri())});
        const auto s = run(st);
        if (s.numerator == 4 && s.denominator == 4) ++halfOk;
    }
    assert(halfOk >= 95);

    // Silent / empty input.
    MeterDetector det;
    const auto s = det.suggestion();
    assert(s.numerator == 4 && s.denominator == 4 && s.confidence == 0.0f);
}

void testShuffleFeelStaysFourFour() {
    int ok = 0;
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        auto st = synth(Kind::FourFour, seed, 16);
        // Shift off-beats to triplet positions.
        for (auto& o : st) {
            const double ph = o.pos - std::floor(o.pos);
            if (ph > 0.4 && ph < 0.6) o.pos = std::floor(o.pos) + 0.667;
        }
        std::sort(st.begin(), st.end(), [](const Onset& a, const Onset& b) { return a.pos < b.pos; });
        const auto s = run(st);
        if (s.numerator == 4 && s.denominator == 4 && s.ternary) ++ok;
    }
    assert(ok >= 42);
}

void testNoFlappingAndConfidence() {
    for (const Kind k : {Kind::FourFour, Kind::ThreeFour, Kind::FiveFour, Kind::SixEightTernary, Kind::SevenEight}) {
        int bad = 0;
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            int changes = 0;
            const auto s = run(synth(k, seed, 60), &changes);
            if (changes > 1) ++bad;
            assert(s.confidence >= 0.0f && s.confidence <= 1.0f && std::isfinite(s.confidence));
        }
        assert(bad <= 2);  // <= 5% of long runs may change more than once
    }
    // Confident on clean material.
    const auto s = run(synth(Kind::ThreeFour, 7, 24, 0.01f, 0.0f));
    assert(s.numerator == 3 && s.denominator == 4);
    assert(s.confidence >= 0.75f);
}

void testNoEarlyLock() {
    // Not enough evidence yet: stay 4/4 with zero confidence.
    MeterDetector det;
    const auto st = synth(Kind::ThreeFour, 3, 3);
    for (const auto& o : st) det.observeOnset(o.pos, o.strength);
    const auto s = det.suggestion();
    assert(s.numerator == 4 && s.denominator == 4);
    assert(s.confidence < 0.2f);
}

void testMeterChangeAdapts() {
    auto a = synth(Kind::FourFour, 11, 16);
    auto b = synth(Kind::ThreeFour, 12, 40);
    const double shift = 64.0;
    for (auto& o : b) o.pos += shift;
    a.insert(a.end(), b.begin(), b.end());
    const auto s = run(a);
    assert(s.numerator == 3 && s.denominator == 4);
}

void testRobustness() {
    MeterDetector det;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    det.observeOnset(nan, 1.0f);
    det.observeOnset(inf, 1.0f);
    det.observeOnset(-inf, 1.0f);
    det.observeOnset(1.0, std::numeric_limits<float>::quiet_NaN());
    det.observeOnset(1.0, std::numeric_limits<float>::infinity());
    det.observeOnset(1.0, -1.0f);
    det.advanceTo(nan);
    det.advanceTo(inf);
    auto s = det.suggestion();
    assert(s.numerator == 4 && s.confidence == 0.0f);

    // Huge strengths clamp; huge jumps in position do not hang (bounded by a long silent gap).
    det.observeOnset(0.0, 1e9f);
    det.observeOnset(40.0, 1.0f);
    s = det.suggestion();
    assert(std::isfinite(s.confidence));
    // Backwards jump resets.
    det.observeOnset(1.0, 1.0f);
    assert(det.suggestion().confidence == 0.0f);
    assert(det.historyCount() <= MeterDetector::MaxHistory);
}

void testGate() {
    MeterDetector::Suggestion s;
    s.numerator = 3; s.denominator = 4; s.pulsesPerBar = 3; s.confidence = 0.82f;

    AutoMeterGate gate;
    // Manual meter on: never applies, however confident.
    for (std::uint32_t i = 1; i < 40; ++i) { s.evaluations = i; assert(!gate.update(true, false, s).apply); }
    // Host meter available: never applies.
    for (std::uint32_t i = 1; i < 40; ++i) { s.evaluations = i; assert(!gate.update(false, true, s).apply); }

    gate.reset();
    bool applied = false;
    std::uint32_t firstApply = 0;
    for (std::uint32_t i = 1; i < 40; ++i) {
        s.evaluations = i;
        const auto d = gate.update(false, false, s);
        if (d.apply && !applied) { applied = true; firstApply = i; assert(d.numerator == 3 && d.denominator == 4); }
    }
    assert(applied);
    assert(firstApply >= 2 * 3);  // held >= 2 bars of 3 pulses

    // Below threshold never applies.
    gate.reset();
    s.confidence = 0.6f;
    for (std::uint32_t i = 1; i < 60; ++i) { s.evaluations = i; assert(!gate.update(false, false, s).apply); }

    // Toggling manual on removes an applied meter immediately.
    gate.reset();
    s.confidence = 0.9f;
    for (std::uint32_t i = 1; i < 30; ++i) { s.evaluations = i; gate.update(false, false, s); }
    s.evaluations = 30;
    assert(gate.update(false, false, s).apply);
    assert(!gate.update(true, false, s).apply);

    // Custom threshold, NaN threshold falls back.
    gate.setThreshold(std::numeric_limits<float>::quiet_NaN());
    gate.setThreshold(0.95f);
    gate.reset();
    s.confidence = 0.9f;
    for (std::uint32_t i = 100; i < 160; ++i) { s.evaluations = i; assert(!gate.update(false, false, s).apply); }

    char buf[64];
    s.confidence = 0.82f;
    AutoMeterGate::formatReadout(buf, sizeof buf, s);
    assert(std::strcmp(buf, "Auto meter: 3/4 (82%)") == 0);
    s.confidence = 0.0f;
    AutoMeterGate::formatReadout(buf, sizeof buf, s);
    assert(std::strcmp(buf, "Auto meter: --") == 0);
    char tiny[6];
    AutoMeterGate::formatReadout(tiny, sizeof tiny, s);
    assert(tiny[5] == '\0');
    assert(AutoMeterGate::formatReadout(nullptr, 0, s) == 0);
}

void testEndToEndGate() {
    // Detector -> gate on a 7/8 stream: applies 7/8 only after confidence and hold, never with manual on.
    MeterDetector det;
    AutoMeterGate gate, manualGate;
    const auto st = synth(Kind::SevenEight, 5, 30, 0.01f, 0.02f);
    bool applied = false;
    for (const auto& o : st) {
        det.observeOnset(o.pos, o.strength);
        const auto d = gate.update(false, false, det.suggestion());
        if (d.apply) { applied = true; assert(d.numerator == 7 && d.denominator == 8); }
        assert(!manualGate.update(true, false, det.suggestion()).apply);
    }
    assert(applied);
}

} // namespace

int main() {
    testAccuracy();
    testHarshConditions();
    testAmbiguousDefaultsToFourFour();
    testShuffleFeelStaysFourFour();
    testNoFlappingAndConfidence();
    testNoEarlyLock();
    testMeterChangeAdapts();
    testRobustness();
    testGate();
    testEndToEndGate();
    return 0;
}
