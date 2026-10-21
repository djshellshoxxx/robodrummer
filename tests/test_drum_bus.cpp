#include "dsp/DrumBus.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using robodrummer::dsp::DrumBus;
using robodrummer::dsp::DrumBusParams;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

float dbOf(double lin) { return static_cast<float>(20.0 * std::log10(lin)); }

DrumBusParams baseParams() {
    DrumBusParams p;
    p.enabled = true;
    p.thresholdDb = -20.0f; p.ratio = 4.0f; p.kneeDb = 0.0f;
    p.attackMs = 10.0f; p.releaseMs = 100.0f; p.makeupDb = 0.0f;
    p.mix = 1.0f; p.drive = 0.0f; p.scHpfHz = 0.0f; p.detectRms = false;
    return p;
}

// DrumBus holds an atomic, so it is not copyable/movable: construct in place instead.
struct Fixture {
    DrumBus bus;
    Fixture(const DrumBusParams& p, double sr) { bus.setParams(p); bus.prepare(sr, 512); }
};

void run(DrumBus& b, float inL, float inR, int n) {
    std::vector<float> l(static_cast<std::size_t>(n), inL), r(static_cast<std::size_t>(n), inR);
    b.processBlock(l.data(), r.data(), n);
}
// Feed one sample at a time (worst case for block independence) and return the last output.
float stepOne(DrumBus& b, float x) {
    float l = x, r = x;
    b.processBlock(&l, &r, 1);
    return l;
}

std::uint32_t lcg(std::uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
float noise(std::uint32_t& s) { return static_cast<float>(static_cast<std::int32_t>(lcg(s)) / 2147483648.0); }

std::vector<float> testSignal(int n, double sr) {
    std::vector<float> x(static_cast<std::size_t>(n));
    std::uint32_t seed = 12345;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const double env = std::exp(-std::fmod(t, 0.25) * 12.0);
        x[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * env * std::sin(2.0 * kPi * 90.0 * t) + 0.2 * env * noise(seed) + 0.05 * std::sin(2.0 * kPi * 3000.0 * t));
    }
    return x;
}

void testCurve() {
    // Hard knee: DC 0.5 = -6.0206 dB, T=-20, R=4 -> over 13.9794, GR = -0.75*13.9794
    assert(std::fabs(DrumBus::computeGainDb(-6.0206f, -20.0f, 4.0f, 0.0f) - (-10.4846f)) < 0.01f);
    assert(DrumBus::computeGainDb(-30.0f, -20.0f, 4.0f, 0.0f) == 0.0f);
    assert(DrumBus::computeGainDb(-20.0f, -20.0f, 4.0f, 0.0f) == 0.0f);
    assert(DrumBus::computeGainDb(0.0f, -20.0f, 1.0f, 6.0f) == 0.0f);                 // ratio 1: no compression
    assert(std::fabs(DrumBus::computeGainDb(0.0f, -20.0f, 20.0f, 0.0f) - (-19.0f)) < 1e-4f);
    // Soft knee W=10, R=4: at T -> -0.9375, at T+W/2 -> -3.75 (continuous with hard curve), at T-W/2 -> 0
    assert(std::fabs(DrumBus::computeGainDb(-20.0f, -20.0f, 4.0f, 10.0f) - (-0.9375f)) < 1e-4f);
    assert(std::fabs(DrumBus::computeGainDb(-15.0f, -20.0f, 4.0f, 10.0f) - (-3.75f)) < 1e-4f);
    assert(std::fabs(DrumBus::computeGainDb(-25.0f, -20.0f, 4.0f, 10.0f)) < 1e-6f);
    // Monotonic and continuous through the knee
    float prev = 0.0f;
    for (float x = -60.0f; x <= 0.0f; x += 0.05f) {
        const float g = DrumBus::computeGainDb(x, -24.0f, 6.0f, 12.0f);
        assert(g <= prev + 1e-5f);
        assert(prev - g < 0.2f);
        prev = g;
    }
}

void testSteadyState() {
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        auto p = baseParams();
        p.makeupDb = 3.0f;
        Fixture f(p, sr);
        run(f.bus, 0.5f, 0.5f, static_cast<int>(sr)); // 1 s settle (attack 10 ms)
        const float expectGr = DrumBus::computeGainDb(dbOf(0.5), -20.0f, 4.0f, 0.0f);
        assert(std::fabs(f.bus.gainReductionDb() - expectGr) < 0.05f);
        const float out = stepOne(f.bus, 0.5f);
        const float expectOut = 0.5f * std::pow(10.0f, (expectGr + 3.0f) / 20.0f);
        assert(std::fabs(out - expectOut) < 1e-4f);
    }
    // Ratio and threshold sweeps
    for (float ratio : { 2.0f, 8.0f, 20.0f })
        for (float thr : { -40.0f, -12.0f }) {
            auto p = baseParams();
            p.ratio = ratio; p.thresholdDb = thr; p.kneeDb = 8.0f;
            Fixture f(p, 48000.0);
            run(f.bus, 0.7f, 0.2f, 48000); // linked: louder channel drives detection
            const float expect = DrumBus::computeGainDb(dbOf(0.7), thr, ratio, 8.0f);
            assert(std::fabs(f.bus.gainReductionDb() - expect) < 0.05f);
        }
    // Below threshold nothing changes
    auto p = baseParams();
    p.thresholdDb = -6.0f;
    Fixture f(p, 48000.0);
    run(f.bus, 0.1f, 0.1f, 4800);
    assert(f.bus.gainReductionDb() == 0.0f);
    assert(stepOne(f.bus, 0.1f) == 0.1f);
}

void testTimeConstants() {
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 }) {
        auto p = baseParams();
        p.ratio = 20.0f; p.thresholdDb = -20.0f; p.attackMs = 20.0f; p.releaseMs = 200.0f;
        Fixture f(p, sr);
        run(f.bus, 0.0f, 0.0f, static_cast<int>(sr * 0.1));
        const float target = -19.0f;
        const int atkN = static_cast<int>(std::lround(0.020 * sr));
        for (int i = 0; i < atkN; ++i) stepOne(f.bus, 1.0f);
        const float atkFrac = f.bus.gainReductionDb() / target;
        assert(std::fabs(atkFrac - 0.632f) < 0.632f * 0.05f);
        // settle fully, then release
        run(f.bus, 1.0f, 1.0f, static_cast<int>(sr * 0.5));
        assert(std::fabs(f.bus.gainReductionDb() - target) < 0.02f);
        const int relN = static_cast<int>(std::lround(0.200 * sr));
        for (int i = 0; i < relN; ++i) stepOne(f.bus, 0.0f);
        const float relFrac = f.bus.gainReductionDb() / target; // remaining fraction -> 1/e
        assert(std::fabs(relFrac - 0.368f) < 0.368f * 0.05f);
    }
}

void testRmsAndPeak() {
    auto p = baseParams();
    p.detectRms = true;
    Fixture f(p, 48000.0);
    run(f.bus, 0.5f, 0.5f, 48000);
    assert(std::fabs(f.bus.gainReductionDb() - DrumBus::computeGainDb(dbOf(0.5), -20.0f, 4.0f, 0.0f)) < 0.1f);
    // A sine's peak is 3 dB above its RMS, so the peak detector must compress it harder.
    auto sine = [&](bool rms) {
        auto q = baseParams(); q.detectRms = rms; q.attackMs = 1.0f; q.releaseMs = 300.0f;
        Fixture g(q, 48000.0);
        double sum = 0.0;
        for (int i = 0; i < 48000; ++i) {
            stepOne(g.bus, static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * i / 48000.0)));
            if (i > 24000) sum += g.bus.gainReductionDb();
        }
        return sum / 23999.0;
    };
    assert(sine(false) < sine(true) - 1.0);
}

void testSidechainHpf() {
    auto meanGr = [](float hpf) {
        auto p = baseParams();
        p.detectRms = true; p.thresholdDb = -30.0f; p.scHpfHz = hpf;
        Fixture f(p, 48000.0);
        double sum = 0.0;
        for (int i = 0; i < 48000; ++i) {
            stepOne(f.bus, static_cast<float>(0.5 * std::sin(2.0 * kPi * 40.0 * i / 48000.0)));
            if (i > 24000) sum += f.bus.gainReductionDb();
        }
        return sum / 23999.0;
    };
    const double off = meanGr(0.0f), on = meanGr(300.0f);
    assert(off < -5.0);
    assert(on > off + 5.0);
}

void testParallelMix() {
    auto p = baseParams();
    p.mix = 0.0f;
    Fixture f(p, 48000.0);
    run(f.bus, 0.5f, 0.5f, 4800);
    assert(stepOne(f.bus, 0.5f) == 0.5f);
    p.mix = 0.5f;
    Fixture h(p, 48000.0);
    run(h.bus, 0.5f, 0.5f, 48000);
    const float g = std::pow(10.0f, DrumBus::computeGainDb(dbOf(0.5), -20.0f, 4.0f, 0.0f) / 20.0f);
    assert(std::fabs(stepOne(h.bus, 0.5f) - 0.5f * (0.5f + 0.5f * g)) < 1e-4f);
}

void testDrive() {
    auto p = baseParams();
    p.thresholdDb = 0.0f; // no compression: isolate the shaper
    p.drive = 0.0f;
    Fixture clean(p, 48000.0);
    p.drive = 1.0f;
    Fixture driven(p, 48000.0);
    std::vector<float> a(48000), b(48000);
    for (int i = 0; i < 48000; ++i)
        a[static_cast<std::size_t>(i)] = b[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 100.0 * i / 48000.0));
    std::vector<float> a2 = a;
    clean.bus.processBlock(a.data(), a2.data(), 48000);
    driven.bus.processBlock(b.data(), b.data(), 48000);
    double meanB = 0.0, diff = 0.0, peak = 0.0;
    for (int i = 0; i < 48000; ++i) {
        const auto k = static_cast<std::size_t>(i);
        assert(std::fabs(a[k] - 0.5f * std::sin(2.0 * kPi * 100.0 * i / 48000.0)) < 1e-6f); // drive 0: transparent
        assert(std::isfinite(b[k]));
        if (i >= 24000) { meanB += b[k]; diff += std::fabs(b[k] - a[k]); peak = std::max<double>(peak, std::fabs(b[k])); }
    }
    assert(std::fabs(meanB / 24000.0) < 1e-3);   // DC-free
    assert(diff / 24000.0 > 1e-3);               // audibly different
    assert(peak < 1.0);                           // soft limited
}

void testBypassAndCrossfade() {
    auto p = baseParams();
    p.enabled = false;
    Fixture f(p, 48000.0);
    auto x = testSignal(9600, 48000.0);
    auto y = x;
    f.bus.processBlock(y.data(), y.data(), 9600);
    assert(std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0);

    // enable, run, disable: crossfade is bounded (no click), then bit exact once settled
    p.enabled = true;
    f.bus.setParams(p);
    run(f.bus, 0.5f, 0.5f, 48000);
    const float wet = stepOne(f.bus, 0.5f);
    assert(std::fabs(wet - 0.5f) > 0.05f);
    p.enabled = false;
    f.bus.setParams(p);
    const int fadeN = static_cast<int>(std::lround(0.005 * 48000.0));
    float prev = wet, maxStep = 0.0f;
    for (int i = 0; i < fadeN + 4; ++i) {
        const float o = stepOne(f.bus, 0.5f);
        maxStep = std::max(maxStep, std::fabs(o - prev));
        prev = o;
    }
    assert(maxStep <= std::fabs(0.5f - wet) / static_cast<float>(fadeN) * 1.05f + 1e-6f);
    assert(prev == 0.5f);
    auto z = x;
    f.bus.processBlock(z.data(), z.data(), 9600);
    assert(std::memcmp(x.data(), z.data(), x.size() * sizeof(float)) == 0);
    assert(f.bus.latencySamples() == 0);
}

void testNonFinite() {
    for (bool enabled : { true, false }) {
        auto p = baseParams();
        p.enabled = enabled; p.drive = 0.7f; p.detectRms = true; p.scHpfHz = 100.0f;
        Fixture f(p, 48000.0);
        std::vector<float> l(4096), r(4096);
        std::uint32_t s = 7;
        for (std::size_t i = 0; i < l.size(); ++i) {
            l[i] = noise(s); r[i] = noise(s);
            if (i % 97 == 0) l[i] = kNaN;
            if (i % 101 == 0) r[i] = kInf;
            if (i % 103 == 0) l[i] = -kInf;
            if (i % 107 == 0) r[i] = 3.0e38f;
        }
        f.bus.processBlock(l.data(), r.data(), 4096);
        for (std::size_t i = 0; i < l.size(); ++i) assert(std::isfinite(l[i]) && std::isfinite(r[i]));
        // recovers: ordinary signal afterwards is sane
        std::vector<float> a(4800);
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<float>(0.3 * std::sin(0.05 * static_cast<double>(i)));
        f.bus.processBlock(a.data(), nullptr, 4800);
        for (float v : a) assert(std::isfinite(v) && std::fabs(v) < 2.0f);
        assert(std::isfinite(f.bus.gainReductionDb()));
    }
}

void testDenormals() {
    auto p = baseParams();
    p.drive = 0.5f; p.detectRms = true; p.scHpfHz = 80.0f;
    Fixture f(p, 48000.0);
    run(f.bus, 0.9f, 0.9f, 4800);
    std::vector<float> l(96000, 0.0f), r(96000, 0.0f);
    for (std::size_t i = 0; i < l.size(); i += 3) { l[i] = 1.0e-40f; r[i] = -1.0e-41f; } // subnormal inputs
    f.bus.processBlock(l.data(), r.data(), 96000);
    for (std::size_t i = 0; i < l.size(); ++i) {
        assert(std::fpclassify(l[i]) != FP_SUBNORMAL);
        assert(std::fpclassify(r[i]) != FP_SUBNORMAL);
    }
    assert(std::fabs(f.bus.gainReductionDb()) < 1e-3f); // released fully back to unity
}

void testBlockIndependence() {
    const int n = 48000;
    const auto x = testSignal(n, 48000.0);
    auto p = baseParams();
    p.detectRms = true; p.scHpfHz = 120.0f; p.drive = 0.4f; p.mix = 0.6f; p.kneeDb = 9.0f;
    auto p2 = p;
    p2.thresholdDb = -30.0f; p2.ratio = 8.0f; p2.makeupDb = 4.0f; p2.mix = 0.9f; p2.drive = 0.0f;

    auto render = [&](int block) {
        Fixture f(p, 48000.0);
        std::vector<float> l = x, r = x;
        for (std::size_t i = 0; i < r.size(); ++i) r[i] *= 0.7f;
        const int split = 20000;
        for (int pos = 0; pos < n;) {
            if (pos == split) f.bus.setParams(p2);
            const int limit = pos < split ? split : n;
            const int len = std::min(block, limit - pos);
            f.bus.processBlock(l.data() + pos, r.data() + pos, len);
            pos += len;
        }
        l.insert(l.end(), r.begin(), r.end());
        return l;
    };
    const auto ref = render(512);
    for (int block : { 1, 7, 64, 333 }) {
        const auto out = render(block);
        for (std::size_t i = 0; i < ref.size(); ++i) assert(std::fabs(out[i] - ref[i]) <= 1e-6f);
    }
    // sanity: the effect actually did something
    double d = 0.0;
    for (int i = 0; i < n; ++i) d += std::fabs(ref[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(i)]);
    assert(d > 1.0);
}

void testResetAndDeterminism() {
    auto p = baseParams();
    p.drive = 0.3f; p.scHpfHz = 100.0f;
    Fixture f(p, 48000.0), fresh(p, 48000.0);
    for (int i = 0; i < 20000; ++i) stepOne(f.bus, static_cast<float>(std::sin(0.3 * i)));
    assert(f.bus.gainReductionDb() < -5.0f);
    f.bus.reset();
    assert(f.bus.gainReductionDb() == 0.0f);
    const auto x = testSignal(4800, 48000.0);
    auto a = x, b = x;
    f.bus.processBlock(a.data(), a.data(), 4800);
    fresh.bus.processBlock(b.data(), b.data(), 4800);
    assert(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

void testSanitisingAndEdgeCases() {
    DrumBusParams p;
    p.setThresholdDb(kNaN).setRatio(kInf).setKneeDb(-5.0f).setAttackMs(0.0f).setReleaseMs(1.0e9f)
        .setMakeupDb(-kInf).setScHpfHz(10.0f).setMix(2.0f).setDrive(kNaN);
    assert(p.thresholdDb == -18.0f && p.ratio == 4.0f && p.kneeDb == 0.0f && DrumBusParams{}.setRatio(1.0e9f).ratio == 20.0f);
    assert(p.attackMs == 0.1f && p.releaseMs == 1000.0f && p.makeupDb == 0.0f && DrumBusParams{}.setMakeupDb(-99.0f).makeupDb == -12.0f);
    assert(p.scHpfHz == 0.0f && p.mix == 1.0f && p.drive == 0.0f);
    DrumBusParams q;
    q.thresholdDb = kNaN; q.ratio = -3.0f; q.mix = kNaN; q.scHpfHz = 9999.0f;
    q.sanitise();
    assert(q.thresholdDb == -18.0f && q.ratio == 1.0f && q.mix == 1.0f && q.scHpfHz == 500.0f);

    Fixture f(baseParams(), 48000.0);
    f.bus.processBlock(nullptr, nullptr, 64);
    float l = 0.25f;
    f.bus.processBlock(&l, nullptr, 0);
    assert(l == 0.25f);
    f.bus.processBlock(&l, nullptr, 1);          // mono
    assert(std::isfinite(l));
    std::vector<float> big(70000, 0.4f);          // larger than the prepared maxBlock
    f.bus.processBlock(big.data(), big.data(), 70000);
    for (float v : big) assert(std::isfinite(v));
    f.bus.prepare(std::numeric_limits<double>::quiet_NaN(), 0); // bad sample rate falls back
    f.bus.processBlock(big.data(), big.data(), 100);
    assert(std::isfinite(big[0]));
}

void testSampleRates() {
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 }) {
        auto p = baseParams();
        p.drive = 0.5f; p.detectRms = true; p.scHpfHz = 150.0f; p.kneeDb = 6.0f;
        Fixture f(p, sr);
        auto x = testSignal(static_cast<int>(sr), sr);
        f.bus.processBlock(x.data(), x.data(), static_cast<int>(x.size()));
        double energy = 0.0;
        for (float v : x) { assert(std::isfinite(v)); energy += static_cast<double>(v) * v; }
        assert(energy > 1.0);
    }
}
} // namespace

int main() {
    testCurve();
    testSteadyState();
    testTimeConstants();
    testRmsAndPeak();
    testSidechainHpf();
    testParallelMix();
    testDrive();
    testBypassAndCrossfade();
    testNonFinite();
    testDenormals();
    testBlockIndependence();
    testResetAndDeterminism();
    testSanitisingAndEdgeCases();
    testSampleRates();
    std::puts("test_drum_bus: ok");
}
