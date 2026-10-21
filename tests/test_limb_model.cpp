#include "core/FillGenerator.h"
#include "core/GrooveGenerator.h"
#include "core/JamStyleProfile.h"
#include "core/LimbModel.h"
#include "core/StyleLibrary.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace robodrummer;

namespace {

constexpr double SR = 48000.0;

LimbContext ctxFor(double bpm, int n = 4, int d = 4) { return {bpm, SR, n, d}; }
int msToSamples(double ms) { return static_cast<int>(std::lround(ms * SR / 1000.0)); }
double spb(double bpm, int d = 4) { return SR * 60.0 / bpm * (4.0 / d); }

DrumEvent ev(DrumInstrument i, int off, float vel = 0.8f, std::uint32_t seq = 0) { return {i, off, vel, seq}; }

// Independent checker (does not share code with the solver): per-limb stroke limits, at most four hits in any
// window shorter than the single-stroke limit, and every hit on a limb that can legally play it.
std::size_t independentViolations(const std::vector<DrumEvent>& evs, const LimbReport& rep, const LimbSettings& s, double sr) {
    std::size_t bad = 0;
    for (std::size_t i = 0; i < evs.size(); ++i) {
        const bool footInst = evs[i].instrument == DrumInstrument::Kick;
        const Limb l = rep.limb[i];
        if (footInst && l != Limb::RightFoot && l != Limb::LeftFoot) ++bad;
        if (!footInst && (l == Limb::RightFoot || l == Limb::LeftFoot) && evs[i].instrument != DrumInstrument::ClosedHat) ++bad;
        if (l == Limb::None) ++bad;
    }
    for (int limb = 0; limb < 4; ++limb) {
        std::vector<int> t;
        for (std::size_t i = 0; i < evs.size(); ++i)
            if (static_cast<int>(rep.limb[i]) == limb) t.push_back(evs[i].sampleOffset);
        std::sort(t.begin(), t.end());
        const double minMs = limb >= 2 ? s.footMinIntervalMs : s.handMinIntervalMs;
        for (std::size_t i = 1; i < t.size(); ++i)
            if (static_cast<double>(t[i] - t[i - 1]) < minMs * sr / 1000.0 - 1.0) ++bad;
    }
    const double window = 0.99 * s.handMinIntervalMs * sr / 1000.0;
    for (std::size_t i = 0; i < evs.size(); ++i) {
        int n = 0;
        for (std::size_t j = 0; j < evs.size(); ++j)
            if (evs[j].sampleOffset >= evs[i].sampleOffset && evs[j].sampleOffset < evs[i].sampleOffset + window) ++n;
        if (n > 4) ++bad;
    }
    return bad;
}

std::size_t countInstrument(const DrumEvent* e, std::size_t n, DrumInstrument i) {
    std::size_t c = 0;
    for (std::size_t k = 0; k < n; ++k) c += e[k].instrument == i ? 1 : 0;
    return c;
}

bool sameEvent(const DrumEvent& a, const DrumEvent& b) {
    return a.instrument == b.instrument && a.sampleOffset == b.sampleOffset && a.velocity == b.velocity && a.sequence == b.sequence;
}

// Stream of one instrument on a 16th grid for one 4/4 bar.
std::vector<DrumEvent> sixteenths(DrumInstrument inst, double bpm, int hits = 16) {
    std::vector<DrumEvent> v;
    for (int i = 0; i < hits; ++i)
        v.push_back(ev(inst, static_cast<int>(std::lround(i * spb(bpm) / 4.0)), 0.7f, static_cast<std::uint32_t>(i)));
    return v;
}

std::size_t run(LimbModel& m, std::vector<DrumEvent>& v, double bpm, float amount, LimbReport& rep, int n = 4, int d = 4) {
    const std::size_t out = m.process(v.data(), v.size(), ctxFor(bpm, n, d), amount, &rep);
    v.resize(out);
    return out;
}

void testPassthrough() {
    LimbModel m;
    std::vector<DrumEvent> base;
    for (int i = 0; i < 40; ++i) base.push_back(ev(static_cast<DrumInstrument>(i % 16), i * 5, 0.5f, static_cast<std::uint32_t>(i)));
    for (const float amount : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
        auto v = base;
        LimbReport rep;
        const auto n = m.process(v.data(), v.size(), ctxFor(120.0), amount, &rep);
        assert(n == base.size());
        assert(std::memcmp(v.data(), base.data(), base.size() * sizeof(DrumEvent)) == 0);
        assert(rep.removedCount == 0 && rep.keptCount == base.size());
    }
    // Invalid context and null/empty input are passthrough too and never crash.
    auto v = base;
    assert(m.process(v.data(), v.size(), {0.0, SR, 4, 4}, 1.0f) == base.size());
    assert(m.process(v.data(), v.size(), {std::numeric_limits<double>::quiet_NaN(), SR, 4, 4}, 1.0f) == base.size());
    assert(m.process(v.data(), v.size(), {120.0, -1.0, 4, 4}, 1.0f) == base.size());
    assert(m.process(v.data(), v.size(), {120.0, SR, 0, 4}, 1.0f) == base.size());
    assert(m.process(nullptr, 5, ctxFor(120.0), 1.0f) == 5);
    assert(m.process(v.data(), 0, ctxFor(120.0), 1.0f) == 0);
    assert(std::memcmp(v.data(), base.data(), base.size() * sizeof(DrumEvent)) == 0);
    // Oversized input is left alone and flagged.
    std::vector<DrumEvent> big(LimbModel::MaxEvents + 10, ev(DrumInstrument::ClosedHat, 100));
    LimbReport rep;
    assert(m.process(big.data(), big.size(), ctxFor(120.0), 1.0f, &rep) == big.size());
    assert(rep.overflow);
}

void testFiveSimultaneousHits() {
    LimbModel m;
    const int backbeat = static_cast<int>(std::lround(spb(120.0)));  // beat index 1 = backbeat in 4/4
    std::vector<DrumEvent> v = {
        ev(DrumInstrument::Kick, backbeat, 0.9f, 0), ev(DrumInstrument::Snare, backbeat, 0.9f, 1),
        ev(DrumInstrument::ClosedHat, backbeat, 0.7f, 2), ev(DrumInstrument::Ride, backbeat, 0.7f, 3),
        ev(DrumInstrument::Crash, backbeat, 0.8f, 4), ev(DrumInstrument::HighTom, backbeat, 0.7f, 5),
        ev(DrumInstrument::MidTom, backbeat, 0.7f, 6), ev(DrumInstrument::FloorTom, backbeat, 0.7f, 7)};
    LimbReport rep;
    const auto n = run(m, v, 120.0, 1.0f, rep);
    assert(n <= 4 && n >= 3);
    assert(rep.removedCount == 8 - n);
    // Distinct limbs.
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j) assert(rep.limb[i] != rep.limb[j]);
    assert(independentViolations(v, rep, m.settings(), SR) == 0);
    // Kick (RF) and the backbeat snare survive; hats/toms/ride lose to them.
    assert(countInstrument(v.data(), n, DrumInstrument::Kick) == 1);
    assert(countInstrument(v.data(), n, DrumInstrument::Snare) == 1);

    // Quiet hat can be played by the left foot, so a full four-limb unison is legal.
    std::vector<DrumEvent> four = {ev(DrumInstrument::Kick, backbeat, 0.9f, 0), ev(DrumInstrument::Snare, backbeat, 0.9f, 1),
                                   ev(DrumInstrument::Crash, backbeat, 0.8f, 2), ev(DrumInstrument::ClosedHat, backbeat, 0.3f, 3)};
    run(m, four, 120.0, 1.0f, rep);
    assert(four.size() == 4);
    std::vector<DrumEvent> five = four;
    five.push_back(ev(DrumInstrument::Ride, backbeat, 0.8f, 4));
    five.push_back(ev(DrumInstrument::HighTom, backbeat, 0.8f, 5));
    five.insert(five.begin(), ev(DrumInstrument::Cowbell, backbeat, 0.8f, 6));
    run(m, five, 120.0, 1.0f, rep);
    assert(five.size() <= 4);
}

void testRateCapsByTempo() {
    LimbSettings s;
    LimbModel m;
    // Hand cap: hat 16ths survive up to ~170 BPM, then get thinned.
    for (const double bpm : {60.0, 100.0, 140.0, 160.0, 170.0}) {
        auto v = sixteenths(DrumInstrument::ClosedHat, bpm);
        LimbReport rep;
        run(m, v, bpm, 1.0f, rep);
        assert(v.size() == 16);
        assert(independentViolations(v, rep, s, SR) == 0);
    }
    std::size_t last = 16;
    for (const double bpm : {190.0, 210.0, 240.0}) {
        auto v = sixteenths(DrumInstrument::ClosedHat, bpm);
        LimbReport rep;
        run(m, v, bpm, 1.0f, rep);
        assert(v.size() < 16 && v.size() >= 6);
        assert(v.size() <= last);
        last = v.size();
        assert(independentViolations(v, rep, s, SR) == 0);
        assert(rep.removedCount == 16 - v.size());
        assert(rep.removedByReason[static_cast<std::size_t>(LimbRemoval::Rate)] > 0);
        for (std::size_t i = 0; i < v.size(); ++i) assert(rep.limb[i] == Limb::RightHand);  // no hand-swapping tricks
    }
    // Snare 16ths: two hands may alternate (a roll) so the cap is higher but exists.
    {
        auto v = sixteenths(DrumInstrument::Snare, 240.0, 32);
        LimbReport rep;
        run(m, v, 240.0, 1.0f, rep);
        assert(v.size() <= 32 && independentViolations(v, rep, s, SR) == 0);
    }
    // Foot cap (double-kick limit) and double pedal.
    {
        auto v = sixteenths(DrumInstrument::Kick, 120.0);
        LimbReport rep;
        run(m, v, 120.0, 1.0f, rep);
        assert(v.size() == 16);
        v = sixteenths(DrumInstrument::Kick, 200.0);
        run(m, v, 200.0, 1.0f, rep);
        assert(v.size() < 16 && independentViolations(v, rep, s, SR) == 0);

        LimbSettings dp;
        dp.doublePedal = true;
        LimbModel dm;
        dm.setSettings(dp);
        v = sixteenths(DrumInstrument::Kick, 200.0);
        dm.process(v.data(), v.size(), ctxFor(200.0), 1.0f, &rep);
        assert(rep.keptCount == 16);
        for (std::size_t i = 0; i < 16; ++i) assert(rep.limb[i] == Limb::RightFoot || rep.limb[i] == Limb::LeftFoot);
        assert(dm.countViolations(v.data(), 16, rep.limb.data(), ctxFor(200.0), 1.0f) == 0);
    }
    assert(LimbModel::maxSixteenthBpm(85.0f) > 170.0 && LimbModel::maxSixteenthBpm(85.0f) < 180.0);
    assert(LimbModel::maxSixteenthBpm(0.0f) == 0.0);

    // Travel: hat then ride 40 ms later needs the other hand or loses a hit.
    std::vector<DrumEvent> far = {ev(DrumInstrument::ClosedHat, 1000, 0.6f, 0), ev(DrumInstrument::Ride, 1000 + msToSamples(40), 0.6f, 1)};
    LimbReport rep;
    run(m, far, 120.0, 1.0f, rep);
    assert(far.size() == 2 && rep.limb[0] != rep.limb[1]);
}

void testPriorityPreserved() {
    LimbModel m;
    const int beat1 = static_cast<int>(std::lround(spb(120.0)));
    // Both hands are busy just before the backbeat snare: a ghost snare (LH) and a ride (RH). The protected snare
    // must evict the weaker blocker (the ghost) and the downbeat kick/crash must survive anything.
    std::vector<DrumEvent> v = {
        ev(DrumInstrument::Kick, 0, 0.9f, 0), ev(DrumInstrument::Crash, 0, 0.9f, 1), ev(DrumInstrument::ClosedHat, 0, 0.6f, 2),
        ev(DrumInstrument::Snare, beat1 - msToSamples(20), 0.3f, 3), ev(DrumInstrument::Ride, beat1 - msToSamples(15), 0.7f, 4),
        ev(DrumInstrument::Snare, beat1, 0.95f, 5)};
    LimbReport rep;
    run(m, v, 120.0, 1.0f, rep);
    bool ghost = false, backbeat = false, crash = false, kick = false;
    for (const auto& e : v) {
        ghost = ghost || (e.instrument == DrumInstrument::Snare && e.velocity < 0.4f);
        backbeat = backbeat || (e.instrument == DrumInstrument::Snare && e.velocity > 0.9f);
        crash = crash || e.instrument == DrumInstrument::Crash;
        kick = kick || e.instrument == DrumInstrument::Kick;
    }
    assert(backbeat && crash && kick && !ghost);
    assert(rep.protectedDropped == 0 && rep.protectedKept >= 3);
    assert(rep.removedByReason[static_cast<std::size_t>(LimbRemoval::Evicted)] >= 1);
    assert(independentViolations(v, rep, m.settings(), SR) == 0);

    // Fast tempo: the downbeat crash is never lost to a hat that just played.
    std::vector<DrumEvent> w = {ev(DrumInstrument::ClosedHat, 0, 0.6f, 0), ev(DrumInstrument::Crash, msToSamples(10), 0.9f, 1)};
    // (crash at 10 ms is inside the downbeat window, so it is protected)
    run(m, w, 240.0, 1.0f, rep);
    assert(countInstrument(w.data(), w.size(), DrumInstrument::Crash) == 1);

    // Lower priority never evicts higher: a hat cannot push out a tom.
    std::vector<DrumEvent> z = {ev(DrumInstrument::FloorTom, 5000, 0.9f, 0), ev(DrumInstrument::OpenHat, 5000 + msToSamples(20), 0.6f, 1),
                                ev(DrumInstrument::Crash, 5000 + msToSamples(25), 0.6f, 2)};
    run(m, z, 120.0, 1.0f, rep);
    assert(countInstrument(z.data(), z.size(), DrumInstrument::FloorTom) == 1);
}

void testDeterminismAndAmount() {
    GrooveGenerator gen;
    const Style style = styleFor(JamStyle::Metal);
    GrooveContext ctx;
    ctx.bpm = 220.0; ctx.intensity = 1.0f; ctx.complexity = 1.0f; ctx.humanize = 1.0f; ctx.sectionStart = true; ctx.seed = 7;
    std::array<DrumEvent, 256> events{};
    const auto count = gen.generateBar(style, ctx, events);
    assert(count > 8);

    LimbModel a, b;
    std::vector<DrumEvent> v1(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count)), v2 = v1;
    LimbReport r1, r2;
    run(a, v1, 220.0, 1.0f, r1);
    run(b, v2, 220.0, 1.0f, r2);
    assert(v1.size() == v2.size());
    assert(std::memcmp(v1.data(), v2.data(), v1.size() * sizeof(DrumEvent)) == 0);
    assert(r1.removedCount == r2.removedCount);
    for (std::size_t i = 0; i < v1.size(); ++i) assert(r1.limb[i] == r2.limb[i]);
    // Re-running the same instance (scratch is rebuilt every call) is identical, even after other work.
    std::vector<DrumEvent> junk = sixteenths(DrumInstrument::Snare, 90.0);
    LimbReport rj;
    run(a, junk, 90.0, 1.0f, rj);
    std::vector<DrumEvent> v3(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count));
    run(a, v3, 220.0, 1.0f, r1);
    assert(v3.size() == v1.size() && std::memcmp(v3.data(), v1.data(), v1.size() * sizeof(DrumEvent)) == 0);

    // Partial amount removes no more than full, and stays internally consistent.
    std::size_t prevKept = count;
    for (const float amount : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        std::vector<DrumEvent> v(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count));
        LimbReport rep;
        run(a, v, 220.0, amount, rep);
        assert(v.size() <= prevKept || amount == 0.0f);
        prevKept = std::min(prevKept, v.size());
        assert(rep.keptCount + rep.removedCount == count);
        assert(a.countViolations(v.data(), v.size(), rep.limb.data(), ctxFor(220.0), amount) == 0);
    }
}

void testCarry() {
    LimbSettings s;
    s.carry = true;
    LimbModel m;
    m.setSettings(s);
    const double bar = spb(120.0) * 4.0;
    std::vector<DrumEvent> b1 = {ev(DrumInstrument::ClosedHat, static_cast<int>(bar) - msToSamples(20), 0.6f, 0)};
    LimbReport rep;
    run(m, b1, 120.0, 1.0f, rep);
    std::vector<DrumEvent> b2 = {ev(DrumInstrument::ClosedHat, 100, 0.6f, 0)};
    run(m, b2, 120.0, 1.0f, rep);
    // Same hand 20 ms + 100 samples after its last hit: the hat is taken over by nothing (stream rule) and dropped.
    assert(b2.empty());
    m.resetCarry();
    b2 = {ev(DrumInstrument::ClosedHat, 100, 0.6f, 0)};
    run(m, b2, 120.0, 1.0f, rep);
    assert(b2.size() == 1);
    // Carry off: bars are independent.
    LimbModel plain;
    b1 = {ev(DrumInstrument::ClosedHat, static_cast<int>(bar) - msToSamples(20), 0.6f, 0)};
    run(plain, b1, 120.0, 1.0f, rep);
    b2 = {ev(DrumInstrument::ClosedHat, 100, 0.6f, 0)};
    run(plain, b2, 120.0, 1.0f, rep);
    assert(b2.size() == 1);
}

void testSettingsSanitized() {
    LimbSettings s;
    s.handMinIntervalMs = std::numeric_limits<float>::quiet_NaN();
    s.footMinIntervalMs = std::numeric_limits<float>::infinity();
    s.travelBaseMs = -5.0f;
    s.footHatMaxVelocity = 99.0f;
    LimbModel m;
    m.setSettings(s);
    assert(std::isfinite(m.settings().handMinIntervalMs) && m.settings().handMinIntervalMs > 0.0f);
    assert(std::isfinite(m.settings().footMinIntervalMs));
    assert(m.settings().travelBaseMs >= 0.0f && m.settings().footHatMaxVelocity <= 1.0f);
    // NaN velocity events do not break processing.
    std::vector<DrumEvent> v = {ev(DrumInstrument::ClosedHat, 10, std::numeric_limits<float>::quiet_NaN()), ev(DrumInstrument::Snare, 10, 0.9f)};
    LimbReport rep;
    run(m, v, 120.0, 1.0f, rep);
    assert(v.size() == 2);
}

void testReportRemovedList() {
    LimbModel m;
    auto v = sixteenths(DrumInstrument::ClosedHat, 240.0, 16);
    LimbReport rep;
    run(m, v, 240.0, 1.0f, rep);
    assert(rep.removedCount > 0 && rep.removedCount <= LimbReport::MaxRemoved);
    for (std::size_t i = 0; i < rep.removedCount; ++i) {
        assert(rep.removed[i].instrument == DrumInstrument::ClosedHat);
        assert(rep.removed[i].velocity > 0.0f);
    }
    std::size_t sum = 0;
    for (const auto c : rep.removedByReason) sum += c;
    assert(sum == rep.removedCount);
}

// All 22 styles x tempos 40-240 x seeds x meters, with and without fills, heavy humanize and full complexity.
void testRealGeneratorSweep() {
    GrooveGenerator gen;
    LimbModel model;
    std::size_t bars = 0, removedTotal = 0, groovesChanged = 0;
    const std::array<std::pair<int, int>, 5> meters {{{4, 4}, {3, 4}, {6, 8}, {5, 4}, {7, 8}}};
    for (int styleIndex = 0; styleIndex < 22; ++styleIndex) {
        const Style style = styleFor(static_cast<JamStyle>(styleIndex));
        for (int bpmStep = 40; bpmStep <= 240; bpmStep += 10) {
            for (std::uint32_t seed = 1; seed <= 3; ++seed) {
                for (std::size_t mi = 0; mi < meters.size(); ++mi) {
                    // Meters other than 4/4 only on every other tempo to keep the sweep quick.
                    if (mi > 0 && (bpmStep / 10) % 2 != 0) continue;
                    const auto [num, den] = meters[mi];
                    for (int variant = 0; variant < 3; ++variant) {
                        GrooveContext ctx;
                        ctx.bpm = bpmStep;
                        ctx.sampleRate = SR;
                        ctx.numerator = num;
                        ctx.denominator = den;
                        ctx.intensity = variant == 0 ? 0.5f : 1.0f;
                        ctx.complexity = variant == 0 ? 0.5f : 1.0f;
                        ctx.humanize = variant == 2 ? 1.0f : 0.0f;
                        ctx.sectionStart = (seed % 2) == 1;
                        ctx.seed = seed * 977u + static_cast<std::uint32_t>(styleIndex);
                        std::array<DrumEvent, 256> events{};
                        std::size_t count = gen.generateBar(style, ctx, events);
                        const std::size_t grooveCount = count;

                        if (variant >= 1) {
                            // Merge a fill on top of the groove (stress: the engine would replace, here we add).
                            FillSettings fs;
                            fs.length = FillLength::OneBar;
                            fs.complexity = 1.0f;
                            fs.surprise = 0.5f;
                            std::array<FillHit, FillGenerator::MaxHits> hits{};
                            const int fillBeats = std::min(num, variant == 1 ? 2 : num);
                            const auto nh = FillGenerator::generate(fs, 1.0f, fillBeats, seed, hits);
                            const double sb = spb(ctx.bpm, den);
                            const int barSamples = static_cast<int>(std::llround(sb * num));
                            for (std::size_t i = 0; i < nh && count < events.size(); ++i) {
                                const int off = static_cast<int>(std::llround((num - fillBeats + hits[i].beatOffset) * sb));
                                if (off >= 0 && off < barSamples)
                                    events[count] = {hits[i].instrument, off, hits[i].velocity, static_cast<std::uint32_t>(count)}, ++count;
                            }
                        }

                        std::vector<DrumEvent> input(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count));
                        std::vector<DrumEvent> v = input;
                        LimbReport rep;
                        const auto lc = ctxFor(ctx.bpm, num, den);
                        const auto kept = model.process(v.data(), v.size(), lc, 1.0f, &rep);
                        v.resize(kept);

                        // Zero violations (solver's own checker and the independent one).
                        assert(model.countViolations(v.data(), kept, rep.limb.data(), lc, 1.0f) == 0);
                        assert(independentViolations(v, rep, model.settings(), SR) == 0);
                        // Output is an order-preserving subset of the input; nothing added or altered.
                        std::size_t j = 0;
                        for (const auto& e : v) {
                            while (j < input.size() && !sameEvent(input[j], e)) ++j;
                            assert(j < input.size());
                            ++j;
                        }
                        assert(rep.keptCount + rep.removedCount == count);
                        // Groove-only bars never lose a protected hit.
                        if (variant != 1 && variant != 2) assert(rep.protectedDropped == 0);
                        // Determinism.
                        if (bars % 37 == 0) {
                            LimbModel other;
                            std::vector<DrumEvent> v2 = input;
                            const auto k2 = other.process(v2.data(), v2.size(), lc, 1.0f);
                            assert(k2 == kept && std::memcmp(v2.data(), v.data(), kept * sizeof(DrumEvent)) == 0);
                        }
                        // Slow tempos are untouched by the stroke limits (except genuinely simultaneous overload).
                        (void)grooveCount;
                        removedTotal += rep.removedCount;
                        groovesChanged += rep.removedCount > 0 ? 1 : 0;
                        ++bars;
                    }
                }
            }
        }
    }
    std::printf("limb sweep: %zu bars, %zu changed, %zu hits removed\n", bars, groovesChanged, removedTotal);
    assert(bars > 3000);
    assert(groovesChanged > 0);
}

// At relaxed tempos plain grooves should pass through (the model must not damage ordinary music).
void testSlowGroovesMostlyUntouched() {
    GrooveGenerator gen;
    LimbModel model;
    std::size_t removed = 0, total = 0;
    for (int styleIndex = 0; styleIndex < 22; ++styleIndex) {
        const Style style = styleFor(static_cast<JamStyle>(styleIndex));
        for (std::uint32_t seed = 1; seed <= 4; ++seed) {
            GrooveContext ctx;
            ctx.bpm = 110.0;
            ctx.seed = seed;
            ctx.sectionStart = true;
            std::array<DrumEvent, 256> events{};
            const auto count = gen.generateBar(style, ctx, events);
            LimbReport rep;
            model.process(events.data(), count, ctxFor(110.0), 1.0f, &rep);
            removed += rep.removedCount;
            total += count;
        }
    }
    std::printf("slow grooves: removed %zu of %zu\n", removed, total);
    assert(removed * 50 <= total);  // <= 2%
}

} // namespace

int main() {
    testPassthrough();
    testFiveSimultaneousHits();
    testRateCapsByTempo();
    testPriorityPreserved();
    testDeterminismAndAmount();
    testCarry();
    testSettingsSanitized();
    testReportRemovedList();
    testRealGeneratorSweep();
    testSlowGroovesMostlyUntouched();
    return 0;
}
