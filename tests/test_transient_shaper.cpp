#include "dsp/TransientShaper.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using robodrummer::dsp::TransientShaper;
using robodrummer::dsp::TransientShaperParams;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr bool kVerbose = false;

struct Fixture {
    TransientShaper ts;
    Fixture(const TransientShaperParams& p, double sr) { ts.setParams(p); ts.prepare(sr, 512); }
};

TransientShaperParams params(float attack, float sustain) {
    TransientShaperParams p;
    p.enabled = true; p.attackPct = attack; p.sustainPct = sustain;
    return p;
}

std::uint32_t lcg(std::uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
float noise(std::uint32_t& s) { return static_cast<float>(static_cast<std::int32_t>(lcg(s)) / 2147483648.0); }

// Silence, then a hit: 2 ms noise click on top of a 180 Hz sine with a 60 ms exponential decay.
std::vector<float> clickDecay(double sr, float amp, int preSilence, int total) {
    std::vector<float> x(static_cast<std::size_t>(total), 0.0f);
    std::uint32_t seed = 99;
    for (int i = preSilence; i < total; ++i) {
        const double t = (i - preSilence) / sr;
        double v = std::sin(2.0 * kPi * 180.0 * t) * std::exp(-t / 0.06);
        if (t < 0.002) v += 0.8 * noise(seed);
        x[static_cast<std::size_t>(i)] = static_cast<float>(amp * 0.5 * v);
    }
    return x;
}

double energyDb(const std::vector<float>& v, int from, int to) {
    double e = 0.0;
    for (int i = from; i < to; ++i) e += static_cast<double>(v[static_cast<std::size_t>(i)]) * v[static_cast<std::size_t>(i)];
    return 10.0 * std::log10(e + 1e-30);
}

struct Gains { double attack, tail; };
// dB change of energy in the attack window (first 10 ms) and the tail (80-300 ms after the hit).
Gains measure(const TransientShaperParams& p, double sr, float amp) {
    const int pre = static_cast<int>(0.05 * sr), total = static_cast<int>(0.5 * sr);
    const auto in = clickDecay(sr, amp, pre, total);
    auto out = in;
    Fixture f(p, sr);
    f.ts.processBlock(out.data(), out.data(), total);
    const int a0 = pre, a1 = pre + static_cast<int>(0.010 * sr);
    const int t0 = pre + static_cast<int>(0.080 * sr), t1 = pre + static_cast<int>(0.300 * sr);
    return { energyDb(out, a0, a1) - energyDb(in, a0, a1), energyDb(out, t0, t1) - energyDb(in, t0, t1) };
}

void testShaping() {
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        const auto off = measure(params(0, 0), sr, 0.5f);
        assert(std::fabs(off.attack) < 1e-6 && std::fabs(off.tail) < 1e-6);
        const auto ap = measure(params(100, 0), sr, 0.5f);
        const auto an = measure(params(-100, 0), sr, 0.5f);
        const auto sp = measure(params(0, 100), sr, 0.5f);
        const auto sn = measure(params(0, -100), sr, 0.5f);
        if (kVerbose) std::printf("sr %.0f A+ %.2f/%.2f A- %.2f/%.2f S+ %.2f/%.2f S- %.2f/%.2f\n", sr, ap.attack, ap.tail, an.attack, an.tail, sp.attack, sp.tail, sn.attack, sn.tail);
        assert(ap.attack > 3.0);   // attack boost
        assert(an.attack < -3.0);  // attack cut
        assert(sp.tail > 3.0);     // sustain boost
        assert(sn.tail < -3.0);    // sustain cut
        assert(std::fabs(ap.tail) < 1.5 && std::fabs(an.tail) < 1.5);     // attack control leaves the tail alone
        assert(std::fabs(sp.attack) < 1.5 && std::fabs(sn.attack) < 1.5); // sustain control leaves the attack alone
        // monotonic in the amount
        assert(measure(params(50, 0), sr, 0.5f).attack < ap.attack);
        assert(measure(params(50, 0), sr, 0.5f).attack > 0.5);
    }
}

void testLevelIndependence() {
    for (auto pr : { params(100, 0), params(-100, 0), params(0, 100), params(0, -100), params(60, -60) }) {
        const auto loud = measure(pr, 48000.0, 0.8f);
        const auto quiet = measure(pr, 48000.0, 0.03f);
        assert(std::fabs(loud.attack - quiet.attack) < 0.5);
        assert(std::fabs(loud.tail - quiet.tail) < 0.5);
    }
}

void testNeutrality() {
    // Steady sines pass at (nearly) unity with full attack and sustain boost.
    for (double freq : { 1000.0, 60.0 }) {
        Fixture f(params(100, 100), 48000.0);
        std::vector<float> x(48000);
        for (int i = 0; i < 48000; ++i) x[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::sin(2.0 * kPi * freq * i / 48000.0));
        auto y = x;
        f.ts.processBlock(y.data(), y.data(), 48000);
        const double dbDiff = energyDb(y, 24000, 48000) - energyDb(x, 24000, 48000);
        if (kVerbose) std::printf("steady %.0f Hz: %.3f dB\n", freq, dbDiff);
        assert(std::fabs(dbDiff) < (freq > 100.0 ? 0.3 : 1.5));
    }
    // 0/0 is bit exact, mix 0 is bit exact, output trim scales
    std::vector<float> x(4800);
    std::uint32_t s = 5;
    for (auto& v : x) v = 0.4f * noise(s);
    auto y = x;
    Fixture a(params(0, 0), 48000.0);
    a.ts.processBlock(y.data(), y.data(), 4800);
    assert(std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0);
    auto p = params(100, -100);
    p.mixPct = 0.0f;
    Fixture b(p, 48000.0);
    y = x;
    b.ts.processBlock(y.data(), y.data(), 4800);
    assert(std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0);
    auto q = params(0, 0);
    q.outputDb = -6.0f;
    Fixture c(q, 48000.0);
    y = x;
    c.ts.processBlock(y.data(), y.data(), 4800);
    const float g = std::pow(10.0f, -6.0f / 20.0f);
    for (std::size_t i = 4000; i < 4800; ++i) assert(std::fabs(y[i] - x[i] * g) < 1e-6f);
    // partial mix is the linear blend
    auto m = params(100, 0);
    m.mixPct = 40.0f;
    auto full = params(100, 0);
    Fixture fm(m, 48000.0), ff(full, 48000.0);
    const auto in = clickDecay(48000.0, 0.5f, 2400, 24000);
    auto ym = in, yf = in;
    fm.ts.processBlock(ym.data(), ym.data(), 24000);
    ff.ts.processBlock(yf.data(), yf.data(), 24000);
    for (std::size_t i = 0; i < in.size(); ++i) assert(std::fabs(ym[i] - (0.6f * in[i] + 0.4f * yf[i])) < 1e-5f);
}

float stepOne(TransientShaper& t, float x) {
    float l = x, r = x;
    t.processBlock(&l, &r, 1);
    return l;
}

void testBypassAndCrossfade() {
    auto p = params(100, 100);
    p.enabled = false;
    Fixture f(p, 48000.0);
    const auto x = clickDecay(48000.0, 0.5f, 2400, 24000);
    auto y = x;
    f.ts.processBlock(y.data(), y.data(), 24000);
    assert(std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0);

    p.enabled = true;
    const int fadeN = static_cast<int>(std::lround(0.005 * 48000.0));
    std::vector<float> always = x;
    {
        Fixture g(p, 48000.0);
        g.ts.processBlock(always.data(), always.data(), 24000);
    }
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(always[i] - x[i]));
    // Enable in the middle of the decay (a click would show as a jump); the difference to the input ramps from 0.
    const int start = 2400 + 3000;
    y = x;
    f.ts.processBlock(y.data(), y.data(), start);
    assert(std::memcmp(x.data(), y.data(), static_cast<std::size_t>(start) * sizeof(float)) == 0);
    f.ts.setParams(p);
    f.ts.processBlock(y.data() + start, y.data() + start, 24000 - start);
    for (int k = 0; k < fadeN; ++k) {
        const auto i = static_cast<std::size_t>(start + k);
        assert(std::fabs(y[i] - x[i]) <= static_cast<float>(k + 1) / static_cast<float>(fadeN) * 2.0f * maxDiff + 1e-6f);
    }
    for (std::size_t i = static_cast<std::size_t>(start); i < y.size(); ++i) assert(std::isfinite(y[i]));
    p.enabled = false;
    f.ts.setParams(p);
    for (int i = 0; i < fadeN + 4; ++i) stepOne(f.ts, 0.3f);
    assert(stepOne(f.ts, 0.3f) == 0.3f);
    auto z = x;
    f.ts.processBlock(z.data(), z.data(), 24000);
    assert(std::memcmp(x.data(), z.data(), x.size() * sizeof(float)) == 0);
    assert(f.ts.latencySamples() == 0);
}

void testNonFiniteAndDenormals() {
    for (bool enabled : { true, false }) {
        auto p = params(100, 100);
        p.enabled = enabled;
        Fixture f(p, 48000.0);
        std::vector<float> l(4096), r(4096);
        std::uint32_t s = 3;
        for (std::size_t i = 0; i < l.size(); ++i) {
            l[i] = noise(s); r[i] = noise(s);
            if (i % 89 == 0) l[i] = kNaN;
            if (i % 97 == 0) r[i] = kInf;
            if (i % 101 == 0) l[i] = -kInf;
            if (i % 103 == 0) r[i] = 3.0e38f;
        }
        f.ts.processBlock(l.data(), r.data(), 4096);
        for (std::size_t i = 0; i < l.size(); ++i) assert(std::isfinite(l[i]) && std::isfinite(r[i]));
        assert(std::isfinite(f.ts.gainDb()));
    }
    Fixture f(params(100, 100), 48000.0);
    std::vector<float> l(96000, 0.0f);
    for (int i = 0; i < 1000; ++i) l[static_cast<std::size_t>(i)] = 0.9f * static_cast<float>(std::sin(0.2 * i));
    for (std::size_t i = 2000; i < l.size(); i += 3) l[i] = 1.0e-40f; // subnormal inputs
    f.ts.processBlock(l.data(), nullptr, 96000);
    for (float v : l) assert(std::fpclassify(v) != FP_SUBNORMAL && std::isfinite(v));
}

void testBlockIndependence() {
    const int n = 48000;
    std::vector<float> x(static_cast<std::size_t>(n));
    std::uint32_t s = 21;
    for (int i = 0; i < n; ++i) {
        const double t = i / 48000.0, env = std::exp(-std::fmod(t, 0.2) * 14.0);
        x[static_cast<std::size_t>(i)] = static_cast<float>(0.7 * env * std::sin(2.0 * kPi * 120.0 * t) + 0.15 * env * noise(s));
    }
    auto p1 = params(80, -40);
    p1.mixPct = 85.0f; p1.outputDb = 2.0f;
    auto p2 = params(-60, 90);
    auto render = [&](int block) {
        Fixture f(p1, 48000.0);
        std::vector<float> l = x, r = x;
        for (auto& v : r) v *= 0.6f;
        const int split = 17000;
        for (int pos = 0; pos < n;) {
            if (pos == split) f.ts.setParams(p2);
            const int len = std::min(block, (pos < split ? split : n) - pos);
            f.ts.processBlock(l.data() + pos, r.data() + pos, len);
            pos += len;
        }
        l.insert(l.end(), r.begin(), r.end());
        return l;
    };
    const auto ref = render(512);
    for (int block : { 1, 5, 64, 1000 }) {
        const auto out = render(block);
        for (std::size_t i = 0; i < ref.size(); ++i) assert(std::fabs(out[i] - ref[i]) <= 1e-6f);
    }
    double d = 0.0;
    for (int i = 0; i < n; ++i) d += std::fabs(ref[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(i)]);
    assert(d > 1.0);
}

void testResetAndEdges() {
    Fixture f(params(100, 100), 48000.0), fresh(params(100, 100), 48000.0);
    const auto x = clickDecay(48000.0, 0.5f, 100, 9600);
    auto warm = x;
    f.ts.processBlock(warm.data(), warm.data(), 9600);
    f.ts.reset();
    assert(f.ts.gainDb() == 0.0f);
    auto a = x, b = x;
    f.ts.processBlock(a.data(), a.data(), 9600);
    fresh.ts.processBlock(b.data(), b.data(), 9600);
    assert(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);

    TransientShaperParams p;
    p.setAttackPct(kNaN).setSustainPct(500.0f).setMixPct(-5.0f).setOutputDb(kInf);
    assert(p.attackPct == 0.0f && p.sustainPct == 100.0f && p.mixPct == 0.0f && p.outputDb == 0.0f);
    TransientShaperParams q;
    q.attackPct = -1.0e9f; q.sustainPct = kNaN; q.mixPct = kNaN; q.outputDb = -99.0f;
    q.sanitise();
    assert(q.attackPct == -100.0f && q.sustainPct == 0.0f && q.mixPct == 100.0f && q.outputDb == -12.0f);

    f.ts.processBlock(nullptr, nullptr, 10);
    float l = 0.2f;
    f.ts.processBlock(&l, nullptr, 0);
    assert(l == 0.2f);
    f.ts.processBlock(&l, nullptr, 1);
    assert(std::isfinite(l));
    std::vector<float> big(70000, 0.4f);
    f.ts.processBlock(big.data(), big.data(), 70000);
    for (float v : big) assert(std::isfinite(v));
    f.ts.prepare(kNaN, -1);
    f.ts.processBlock(big.data(), big.data(), 100);
    assert(std::isfinite(big[0]));
}
} // namespace

int main() {
    testShaping();
    testLevelIndependence();
    testNeutrality();
    testBypassAndCrossfade();
    testNonFiniteAndDenormals();
    testBlockIndependence();
    testResetAndEdges();
    std::puts("test_transient_shaper: ok");
}
