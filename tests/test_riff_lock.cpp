#include "analysis/RiffLock.h"
#include "core/GrooveGenerator.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace robodrummer;

namespace {
using Bar = std::array<DrumEvent, 64>;

struct Gen {
    Bar ev{};
    std::size_t n{0};
    GrooveContext ctx;
};

Gen makeBar(const Style& style, std::uint32_t seed, int num = 4, int den = 4, float intensity = 0.6f) {
    Gen g;
    g.ctx.bpm = 120.0;
    g.ctx.sampleRate = 48000.0;
    g.ctx.numerator = num;
    g.ctx.denominator = den;
    g.ctx.intensity = intensity;
    g.ctx.seed = seed;
    g.n = GrooveGenerator{}.generateBar(style, g.ctx, g.ev);
    return g;
}

void feed(RiffLock& rl, const std::vector<int>& slots, int grid, int bars, float low = 1.0f, float strength = 0.8f) {
    for (int b = 0; b < bars; ++b) {
        for (int s : slots) rl.addOnset((static_cast<double>(s) + 0.05) / grid, strength, low);
        rl.endBar();
    }
}

bool same(const Bar& a, const Bar& b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (a[i].instrument != b[i].instrument || a[i].sampleOffset != b[i].sampleOffset
            || a[i].velocity != b[i].velocity || a[i].sequence != b[i].sequence) return false;
    return true;
}

std::uint64_t slotMask(const Bar& ev, std::size_t n, DrumInstrument inst, const Gen& g, int spb, float minVel = 0.0f) {
    const double samplesPerBeat = g.ctx.sampleRate * 60.0 / g.ctx.bpm * (4.0 / g.ctx.denominator);
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (ev[i].instrument != inst || ev[i].velocity < minVel) continue;
        const long s = std::lround(ev[i].sampleOffset / (samplesPerBeat / spb));
        m |= (1ull << s);
    }
    return m;
}

std::uint64_t bits(const std::vector<int>& v) {
    std::uint64_t m = 0;
    for (int s : v) m |= (1ull << s);
    return m;
}

int popcount(std::uint64_t m) { int c = 0; while (m) { c += int(m & 1u); m >>= 1; } return c; }

RiffLockParams full() { RiffLockParams p; p.amount = 1.0f; p.sensitivity = 0.5f; return p; }
} // namespace

int main() {
    const std::vector<int> eighths{0, 2, 6, 8, 10, 14}; // 8th palm-mute riff minus the backbeat snare beats
    const std::vector<int> eighthsAll{0, 2, 4, 6, 8, 10, 12, 14};

    // --- steady 8th palm-mute riff locks within 2 bars ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, eighthsAll, 16, 1);
        assert(rl.snapshot().confidence == 0.0f); // one bar cannot prove repetition
        feed(rl, eighthsAll, 16, 1);
        const auto snap = rl.snapshot();
        assert(snap.confidence > 0.9f);
        for (int s : eighthsAll) assert(snap.consistency[static_cast<std::size_t>(s)] > 0.8f);
        assert(snap.consistency[1] == 0.0f);

        int totalAdded = 0;
        for (std::uint32_t seed = 1; seed <= 20; ++seed) {
            auto g = makeBar(Style::basicRock(), seed);
            const std::uint64_t before = slotMask(g.ev, g.n, DrumInstrument::Kick, g, 4);
            const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, full(), seed);
            const std::uint64_t after = slotMask(g.ev, r.count, DrumInstrument::Kick, g, 4);
            assert((after & before) == before || r.removed > 0);
            const std::uint64_t added = after & ~before;
            assert((added & ~bits(eighths)) == 0);            // only on guitar slots, never on backbeat slots 4/12
            assert(popcount(added) == r.added);
            totalAdded += r.added;
        }
        assert(totalAdded > 0);
    }

    // --- syncopated riff reproduced (metal allows density) ---
    {
        const std::vector<int> pat{0, 3, 6, 10, 14};
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, pat, 16, 3);
        const auto snap = rl.snapshot();
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            auto g = makeBar(Style::metal(), seed);
            const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::metal(), snap, full(), seed);
            const std::uint64_t after = slotMask(g.ev, r.count, DrumInstrument::Kick, g, 4);
            assert((after & bits(pat)) == bits(pat));                      // riff rhythm present in kick
            assert((after & ~(bits(pat) | bits({8}))) == 0);               // conflicting off-riff kicks removed/absent
            assert(r.count <= 64);
        }
    }

    // --- random input yields no change ---
    {
        std::uint32_t rng = 12345;
        auto next = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 20) % 16u; };
        for (int trial = 0; trial < 200; ++trial) {
            RiffLock rl;
            rl.configure(4, 4, 0.0f);
            rl.setTrackerLocked(true);
            for (int b = 0; b < 6; ++b) {
                for (int k = 0; k < 6; ++k) rl.addOnset((next() + 0.02) / 16.0, 0.7f, 1.0f);
                rl.endBar();
            }
            auto g = makeBar(Style::funk(), static_cast<std::uint32_t>(trial + 1));
            const Bar orig = g.ev;
            const std::size_t n0 = g.n;
            const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::funk(), rl.snapshot(), full(), 7);
            assert(r.count == n0 && !r.applied && same(g.ev, orig, n0));
        }
    }

    // --- amount 0 identity, NaN amount identity, unlocked tracker identity, grid mismatch identity ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, eighthsAll, 16, 3);
        auto snap = rl.snapshot();
        auto g = makeBar(Style::basicRock(), 3);
        const Bar orig = g.ev;
        RiffLockParams p = full();
        p.amount = 0.0f;
        auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, p, 9);
        assert(r.count == g.n && same(g.ev, orig, g.n));
        p.amount = std::numeric_limits<float>::quiet_NaN();
        r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, p, 9);
        assert(r.count == g.n && same(g.ev, orig, g.n));
        auto unlocked = snap;
        unlocked.locked = false;
        r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), unlocked, full(), 9);
        assert(r.count == g.n && same(g.ev, orig, g.n));
        auto g68 = makeBar(Style::basicRock(), 3, 6, 8);
        const Bar orig68 = g68.ev;
        r = RiffLockTransform::apply(g68.ev, g68.n, g68.ctx, Style::basicRock(), snap, full(), 9);
        assert(r.count == g68.n && same(g68.ev, orig68, g68.n));
        // NaN sensitivity and NaN ctx stay finite and safe
        p = full();
        p.sensitivity = std::numeric_limits<float>::quiet_NaN();
        r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, p, 9);
        for (std::size_t i = 0; i < r.count; ++i) assert(std::isfinite(g.ev[i].velocity) && g.ev[i].velocity <= 1.0f);
        auto bad = makeBar(Style::basicRock(), 3);
        bad.ctx.bpm = std::numeric_limits<double>::quiet_NaN();
        const Bar origBad = bad.ev;
        r = RiffLockTransform::apply(bad.ev, bad.n, bad.ctx, Style::basicRock(), snap, full(), 9);
        assert(r.count == bad.n && same(bad.ev, origBad, bad.n));
    }

    // --- determinism, snare backbeat preserved, beat-1 kick never removed, caps ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, 16, 4);
        const auto snap = rl.snapshot();
        const Style styles[] = {Style::basicRock(), Style::blues(), Style::funk(), Style::punk(), Style::metal()};
        for (const Style& st : styles) {
            const auto lim = RiffLockLimits::fromStyle(st, 4);
            for (std::uint32_t seed = 1; seed <= 25; ++seed) {
                auto a = makeBar(st, seed);
                auto b = makeBar(st, seed);
                const Bar orig = a.ev;
                const std::size_t n0 = a.n;
                const auto ra = RiffLockTransform::apply(a.ev, a.n, a.ctx, st, snap, full(), seed);
                const auto rb = RiffLockTransform::apply(b.ev, b.n, b.ctx, st, snap, full(), seed);
                assert(ra.count == rb.count && same(a.ev, b.ev, ra.count));

                // snare events untouched
                std::vector<DrumEvent> s0, s1;
                for (std::size_t i = 0; i < n0; ++i) if (orig[i].instrument == DrumInstrument::Snare) s0.push_back(orig[i]);
                for (std::size_t i = 0; i < ra.count; ++i) if (a.ev[i].instrument == DrumInstrument::Snare) s1.push_back(a.ev[i]);
                assert(s0.size() == s1.size());
                for (std::size_t i = 0; i < s0.size(); ++i)
                    assert(s0[i].sampleOffset == s1[i].sampleOffset && s0[i].velocity == s1[i].velocity);

                // beat-1 kick survives; kick on backbeat only if style allows
                const bool had1 = (slotMask(orig, n0, DrumInstrument::Kick, a, 4) & 1ull) != 0;
                const std::uint64_t kAfter = slotMask(a.ev, ra.count, DrumInstrument::Kick, a, 4);
                if (had1) assert(kAfter & 1ull);
                const std::uint64_t kBefore = slotMask(orig, n0, DrumInstrument::Kick, a, 4);
                const std::uint64_t backbeatMask = bits({4, 12});
                if (!lim.allowBackbeatKick) assert(((kAfter & ~kBefore) & backbeatMask) == 0);

                // caps
                assert(ra.added <= lim.maxAdded);
                int kicks = 0;
                for (std::size_t i = 0; i < ra.count; ++i) kicks += a.ev[i].instrument == DrumInstrument::Kick;
                int kicks0 = 0;
                for (std::size_t i = 0; i < n0; ++i) kicks0 += orig[i].instrument == DrumInstrument::Kick;
                assert(kicks <= std::max(lim.maxKicks, kicks0));
                // added hits stay in the bar, velocity valid, sequences consistent
                for (std::size_t i = 0; i < ra.count; ++i) {
                    assert(a.ev[i].sampleOffset >= 0 && a.ev[i].sampleOffset < 96000);
                    assert(a.ev[i].velocity >= 0.0f && a.ev[i].velocity <= 1.0f);
                    if (ra.added || ra.removed) assert(a.ev[i].sequence == i);
                }
                // non-adjacent added kicks for rock families
                if (!lim.allowAdjacent) {
                    const std::uint64_t added = kAfter & ~kBefore;
                    assert((added & (kBefore << 1)) == 0 && (added & (kBefore >> 1)) == 0);
                }
            }
        }
    }

    // --- amount scaling: lower amount changes less on average ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, eighths, 16, 3);
        const auto snap = rl.snapshot();
        int lo = 0, hi = 0;
        for (std::uint32_t seed = 1; seed <= 50; ++seed) {
            auto a = makeBar(Style::funk(), seed);
            auto b = makeBar(Style::funk(), seed);
            RiffLockParams pl = full(); pl.amount = 0.25f;
            lo += RiffLockTransform::apply(a.ev, a.n, a.ctx, Style::funk(), snap, pl, seed).added;
            hi += RiffLockTransform::apply(b.ev, b.n, b.ctx, Style::funk(), snap, full(), seed).added;
        }
        assert(hi >= lo);
    }

    // --- triplet (shuffle) meter ---
    {
        const std::vector<int> pat{0, 2, 3, 5, 6, 8, 9, 11};
        RiffLock rl;
        rl.configure(4, 4, Style::shuffle().swing);
        assert(rl.grid().triplet && rl.grid().slots == 12 && rl.grid().slotsPerBeat == 3);
        rl.setTrackerLocked(true);
        feed(rl, pat, 12, 3);
        const auto snap = rl.snapshot();
        assert(snap.confidence > 0.9f);
        int total = 0;
        for (std::uint32_t seed = 1; seed <= 20; ++seed) {
            auto g = makeBar(Style::shuffle(), seed);
            const std::uint64_t before = slotMask(g.ev, g.n, DrumInstrument::Kick, g, 3);
            const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::shuffle(), snap, full(), seed);
            const std::uint64_t after = slotMask(g.ev, r.count, DrumInstrument::Kick, g, 3);
            const std::uint64_t added = after & ~before;
            assert((added & ~bits({0, 2, 5, 6, 8, 11})) == 0); // pattern minus backbeat slots 3 and 9
            // added offsets sit exactly on the generator formula
            const double spbSamples = 48000.0 * 60.0 / 120.0;
            for (std::size_t i = 0; i < r.count; ++i) {
                if (i < g.n) continue;
                const long s = std::lround(g.ev[i].sampleOffset / (spbSamples / 3.0));
                assert(g.ev[i].sampleOffset == std::llround(static_cast<double>(s) / 3.0 * spbSamples));
            }
            total += r.added;
        }
        assert(total > 0);
        // straight style against triplet snapshot: passthrough (grid mismatch)
        auto g = makeBar(Style::basicRock(), 1);
        const Bar orig = g.ev;
        const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, full(), 1);
        assert(r.count == g.n && same(g.ev, orig, g.n));
    }

    // --- 6/8 grid ---
    {
        const std::vector<int> pat{0, 2, 4, 8, 10}; // 12 slots (2 per eighth); slot 6 is the snare beat
        RiffLock rl;
        rl.configure(6, 8, 0.0f);
        assert(rl.grid().slots == 12 && !rl.grid().triplet);
        rl.setTrackerLocked(true);
        feed(rl, pat, 12, 3);
        const auto snap = rl.snapshot();
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            auto g = makeBar(Style::basicRock(), seed, 6, 8);
            const std::uint64_t before = slotMask(g.ev, g.n, DrumInstrument::Kick, g, 2);
            const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, full(), seed);
            const std::uint64_t added = slotMask(g.ev, r.count, DrumInstrument::Kick, g, 2) & ~before;
            assert((added & ~bits(pat)) == 0 && (added & bits({6})) == 0);
        }
    }

    // --- adaptation: new riff takes over within ~4 bars; silence decays confidence ---
    {
        const std::vector<int> a{0, 3, 6, 10}, b{0, 2, 7, 11};
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        feed(rl, a, 16, 4);
        feed(rl, b, 16, 4);
        const auto snap = rl.snapshot();
        for (int s : b) assert(snap.consistency[static_cast<std::size_t>(s)] >= 0.6f);
        assert(snap.consistency[3] < 0.2f && snap.consistency[6] < 0.2f);
        RiffLock sil;
        sil.configure(4, 4, 0.0f);
        feed(sil, eighthsAll, 16, 3);
        const float c0 = sil.snapshot().confidence;
        for (int i = 0; i < 6; ++i) sil.endBar();
        assert(c0 > 0.9f && sil.snapshot().confidence < 0.3f);
    }

    // --- low-band weighting: high-note noodling does not lock ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, eighthsAll, 16, 4, 0.0f, 0.3f);
        const auto snap = rl.snapshot();
        auto g = makeBar(Style::basicRock(), 2);
        const Bar orig = g.ev;
        const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::basicRock(), snap, full(), 2);
        assert(!r.applied && same(g.ev, orig, g.n));
        RiffLock lo;
        lo.configure(4, 4, 0.0f);
        feed(lo, eighthsAll, 16, 4, 1.0f, 0.3f);
        assert(lo.snapshot().confidence > 0.9f);
    }

    // --- NaN/Inf/huge input safety ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        rl.addOnset(std::numeric_limits<double>::quiet_NaN(), 0.5f, 0.5f);
        rl.addOnset(std::numeric_limits<double>::infinity(), 0.5f, 0.5f);
        rl.addOnset(1e300, nan, nan);
        rl.addOnset(-3.25, inf, -inf);
        rl.addOnset(0.5, 1e30f, 1e30f);
        rl.endBar();
        rl.configure(0, 0, nan);
        rl.configure(1000, 1000, inf);
        rl.addOnset(0.3, 1.0f, 1.0f);
        rl.endBar();
        const auto s = rl.snapshot();
        assert(std::isfinite(s.confidence) && std::isfinite(s.repetition));
        for (float v : s.consistency) assert(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
        assert(s.grid.slots <= RiffGrid::MaxSlots && s.grid.slots > 0);
        LowBandMeter lb;
        lb.prepare(48000.0);
        float buf[256];
        for (int i = 0; i < 256; ++i) buf[i] = (i % 7 == 0) ? nan : (i % 11 == 0 ? inf : 0.3f);
        const float r = lb.processBlock(buf, 256);
        assert(std::isfinite(r) && r >= 0.0f && r <= 1.0f);
        assert(lb.processBlock(nullptr, 10) == lb.ratio() && lb.processBlock(buf, 0) == lb.ratio());
        // a low sine reads low-band heavy, a high sine does not
        LowBandMeter lowM, highM;
        lowM.prepare(48000.0); highM.prepare(48000.0);
        float lowV = 0, highV = 0;
        for (int blk = 0; blk < 20; ++blk) {
            float l[256], h[256];
            for (int i = 0; i < 256; ++i) {
                const double t = (blk * 256 + i) / 48000.0;
                l[i] = 0.5f * static_cast<float>(std::sin(6.283185307 * 80.0 * t));
                h[i] = 0.5f * static_cast<float>(std::sin(6.283185307 * 2500.0 * t));
            }
            lowV = lowM.processBlock(l, 256);
            highV = highM.processBlock(h, 256);
        }
        assert(lowV > 0.7f && highV < 0.1f);
    }

    // --- capacity-full safety: tiny array never overflows ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        feed(rl, eighths, 16, 3);
        const auto snap = rl.snapshot();
        std::array<DrumEvent, 2> tiny{};
        tiny[0] = {DrumInstrument::Kick, 0, 0.8f, 0};
        tiny[1] = {DrumInstrument::Snare, 24000, 0.9f, 1};
        RiffLockParams p = full();
        p.accentCrash = true;
        const auto r = RiffLockTransform::apply(tiny, 5 /*oversized count is clamped*/, GrooveContext{}, Style::metal(), snap, p, 4);
        assert(r.count == 2 && r.added == 0);
        std::array<DrumEvent, 3> three{};
        three[0] = {DrumInstrument::Kick, 0, 0.8f, 0};
        three[1] = {DrumInstrument::Snare, 24000, 0.9f, 1};
        const auto r3 = RiffLockTransform::apply(three, 2, GrooveContext{}, Style::metal(), snap, full(), 4);
        assert(r3.count <= 3 && r3.added <= 1);
    }

    // --- optional crash accent ---
    {
        RiffLock rl;
        rl.configure(4, 4, 0.0f);
        rl.setTrackerLocked(true);
        std::vector<int> pat{0, 8, 10, 12};
        for (int bar = 0; bar < 3; ++bar) {
            for (int s : pat) rl.addOnset((s + 0.05) / 16.0, s == 12 ? 1.0f : 0.4f, 1.0f);
            rl.endBar();
        }
        const auto snap = rl.snapshot();
        RiffLockParams p = full();
        p.accentCrash = true;
        int crashes = 0;
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            auto g = makeBar(Style::metal(), seed);
            const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::metal(), snap, p, seed);
            for (std::size_t i = g.n; i < r.count; ++i)
                if (g.ev[i].instrument == DrumInstrument::Crash) { ++crashes; assert(g.ev[i].sampleOffset == 72000); }
        }
        assert(crashes > 0);
        auto g = makeBar(Style::metal(), 1);
        const auto r = RiffLockTransform::apply(g.ev, g.n, g.ctx, Style::metal(), snap, full(), 1);
        for (std::size_t i = g.n; i < r.count; ++i) assert(g.ev[i].instrument != DrumInstrument::Crash);
    }
    return 0;
}
