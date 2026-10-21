#pragma once
// Riff Lock: the kick (and optionally a crash accent) mirrors the guitarist's repeating
// low-note rhythm. Spec: docs/specs/03-riff-lock.md. Header-only, JUCE-free, RT-safe
// (fixed storage, no allocation/locks/IO, NaN/Inf-safe).
#include "core/DrumEvent.h"
#include "core/MeterProfile.h"
#include "core/Style.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

struct RiffGrid {
    static constexpr int MaxSlots = 64;
    int numerator{4};
    int denominator{4};
    int slotsPerBeat{4};
    int slots{16};
    bool triplet{false};

    [[nodiscard]] static RiffGrid forMeter(int numerator, int denominator, float swing) noexcept {
        RiffGrid g;
        g.numerator = std::clamp(numerator, 2, MeterProfile::MaxBeats);
        g.denominator = denominator;
        const bool wide = denominator >= 8;
        const float sw = std::isfinite(swing) ? swing : 0.0f;
        g.triplet = !wide && sw >= 0.20f;
        g.slotsPerBeat = g.triplet ? 3 : (wide ? 2 : 4);
        g.slots = std::min(g.numerator * g.slotsPerBeat, MaxSlots);
        return g;
    }
    [[nodiscard]] bool operator==(const RiffGrid& o) const noexcept {
        return numerator == o.numerator && denominator == o.denominator && slotsPerBeat == o.slotsPerBeat;
    }
};

// Telemetry + transform input. Plain copyable value (for the ANALYSIS tab).
struct RiffSnapshot {
    std::array<float, RiffGrid::MaxSlots> consistency{}; // 0..1: fraction of recent bars the guitar hit this slot
    std::array<float, RiffGrid::MaxSlots> strength{};    // 0..1: mean onset strength on this slot
    RiffGrid grid{};
    float confidence{0.0f};  // 0..1 repetition-based lock confidence
    float repetition{0.0f};  // 0..1 bar-to-bar similarity (smoothed)
    bool locked{false};      // tempo/bar tracker locked (set by integrator)
    int barsSeen{0};         // completed bars containing onsets
};

struct RiffLockParams {
    float amount{0.0f};      // 0..1 (0 = bit-exact passthrough)
    float sensitivity{0.5f}; // 0..1
    bool accentCrash{false};
};

struct RiffLockResult {
    std::size_t count{0};
    int added{0};
    int removed{0};
    int accented{0};
    bool applied{false};
};

struct RiffLockLimits {
    int maxKicks{5};
    int maxAdded{3};
    bool allowBackbeatKick{false};
    bool allowAdjacent{false};

    [[nodiscard]] static RiffLockLimits fromStyle(const Style& style, int beats) noexcept {
        const float extra = std::isfinite(style.extraKickProbability) ? std::clamp(style.extraKickProbability, 0.0f, 1.0f) : 0.0f;
        const float b = static_cast<float>(std::clamp(beats, 1, MeterProfile::MaxBeats));
        RiffLockLimits l;
        l.maxKicks = std::clamp(static_cast<int>(std::lround(b * (1.0f + 3.0f * extra))), 2, 16);
        l.maxAdded = std::clamp(static_cast<int>(std::ceil(b * (0.5f + 1.5f * extra))), 1, 12);
        l.allowBackbeatKick = extra >= 0.30f;
        l.allowAdjacent = extra >= 0.30f;
        return l;
    }
};

// Cheap low-band ratio (~200 Hz one-pole) so the integrator can weight onsets by low-note content.
class LowBandMeter {
public:
    void prepare(double sampleRate) noexcept {
        const double sr = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        alpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 200.0 / sr));
        reset();
    }
    void reset() noexcept { lp_ = 0.0f; ratio_ = 1.0f; }
    // Returns smoothed low-band energy ratio 0..1 (1 when silent/unknown).
    float processBlock(const float* in, int n) noexcept {
        if (in == nullptr || n <= 0) return ratio_;
        double low = 0.0, total = 0.0;
        for (int i = 0; i < n; ++i) {
            const float x = std::isfinite(in[i]) ? std::clamp(in[i], -4.0f, 4.0f) : 0.0f;
            lp_ += alpha_ * (x - lp_);
            low += static_cast<double>(lp_) * lp_;
            total += static_cast<double>(x) * x;
        }
        if (total > 1.0e-9) {
            const float r = static_cast<float>(std::clamp(low / total, 0.0, 1.0));
            ratio_ += 0.5f * (r - ratio_);
        }
        return ratio_;
    }
    [[nodiscard]] float ratio() const noexcept { return ratio_; }
private:
    float alpha_{0.0262f};
    float lp_{0.0f};
    float ratio_{1.0f};
};

class RiffLock {
public:
    static constexpr float BarDecay = 0.6f;

    RiffLock() noexcept { reset(); }

    void configure(int numerator, int denominator, float swing) noexcept {
        const RiffGrid g = RiffGrid::forMeter(numerator, denominator, swing);
        if (!(g == grid_) || g.triplet != grid_.triplet) { grid_ = g; reset(); }
    }
    void reset() noexcept {
        hist_.fill(0.0f); strAcc_.fill(0.0f); cur_.fill(0.0f); curStr_.fill(0.0f);
        prev1_.fill(0.0f); prev2_.fill(0.0f);
        barWeight_ = 0.0f; repetition_ = 0.0f; repInit_ = false;
        prev1Valid_ = prev2Valid_ = false; barsSeen_ = 0;
    }
    void setTrackerLocked(bool locked) noexcept { locked_ = locked; }

    // barPhase: position within the current bar, 0..1 (wrapped). low: low-band ratio 0..1.
    void addOnset(double barPhase, float strength, float low = 1.0f) noexcept {
        if (!std::isfinite(barPhase)) return;
        const float st = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
        const float lo = std::isfinite(low) ? std::clamp(low, 0.0f, 1.0f) : 1.0f;
        const double ph = barPhase - std::floor(barPhase);
        const double pos = ph * grid_.slots;
        const long rounded = std::lround(pos);
        const double dist = std::abs(pos - static_cast<double>(rounded));
        if (dist > 0.4) return;
        const auto s = static_cast<std::size_t>(rounded % grid_.slots);
        const float closeness = static_cast<float>(1.0 - dist / 0.5);
        const float w = closeness * (0.25f + 0.75f * lo) * (0.7f + 0.3f * st);
        cur_[s] = std::min(1.0f, cur_[s] + w);
        curStr_[s] = std::max(curStr_[s], st);
    }

    // Commit the finished bar (call at each bar boundary).
    void endBar() noexcept {
        float total = 0.0f;
        for (int s = 0; s < grid_.slots; ++s) total += cur_[static_cast<std::size_t>(s)];
        for (std::size_t s = 0; s < hist_.size(); ++s) { hist_[s] *= BarDecay; strAcc_[s] *= BarDecay; }
        barWeight_ = barWeight_ * BarDecay + 1.0f;

        if (total < 0.5f) {
            repetition_ *= 0.6f;
            prev1Valid_ = prev2Valid_ = false;
        } else {
            float best = -1.0f;
            if (prev1Valid_) best = std::max(best, similarity(cur_, prev1_));
            if (prev2Valid_) best = std::max(best, similarity(cur_, prev2_));
            if (best >= 0.0f) {
                repetition_ = repInit_ ? repetition_ + 0.6f * (best - repetition_) : best;
                repInit_ = true;
            }
            for (std::size_t s = 0; s < hist_.size(); ++s) {
                hist_[s] += cur_[s];
                strAcc_[s] += curStr_[s] * cur_[s];
            }
            prev2_ = prev1_; prev2Valid_ = prev1Valid_;
            prev1_ = cur_; prev1Valid_ = true;
            ++barsSeen_;
        }
        cur_.fill(0.0f); curStr_.fill(0.0f);
    }

    [[nodiscard]] RiffSnapshot snapshot() const noexcept {
        RiffSnapshot r;
        r.grid = grid_;
        r.locked = locked_;
        r.barsSeen = barsSeen_;
        r.repetition = std::clamp(repetition_, 0.0f, 1.0f);
        int strong = 0;
        for (int s = 0; s < grid_.slots; ++s) {
            const auto i = static_cast<std::size_t>(s);
            const float c = barWeight_ > 0.0f ? std::clamp(hist_[i] / barWeight_, 0.0f, 1.0f) : 0.0f;
            r.consistency[i] = c;
            r.strength[i] = hist_[i] > 1.0e-6f ? std::clamp(strAcc_[i] / hist_[i], 0.0f, 1.0f) : 0.0f;
            if (c >= 0.5f) ++strong;
        }
        r.confidence = r.repetition * std::clamp(static_cast<float>(strong) / 2.0f, 0.0f, 1.0f);
        return r;
    }
    [[nodiscard]] const RiffGrid& grid() const noexcept { return grid_; }

private:
    using Arr = std::array<float, RiffGrid::MaxSlots>;
    [[nodiscard]] float similarity(const Arr& a, const Arr& b) const noexcept {
        float mn = 0.0f, mx = 0.0f;
        for (int s = 0; s < grid_.slots; ++s) {
            const auto i = static_cast<std::size_t>(s);
            mn += std::min(a[i], b[i]);
            mx += std::max(a[i], b[i]);
        }
        return mx > 1.0e-6f ? mn / mx : 0.0f;
    }

    RiffGrid grid_{};
    Arr hist_{}, strAcc_{}, cur_{}, curStr_{}, prev1_{}, prev2_{};
    float barWeight_{0.0f};
    float repetition_{0.0f};
    bool repInit_{false}, prev1Valid_{false}, prev2Valid_{false}, locked_{false};
    int barsSeen_{0};
};

class RiffLockTransform {
public:
    static constexpr float gateFor(float sensitivity) noexcept { return 0.75f - 0.40f * sensitivity; }
    static constexpr float slotThresholdFor(float sensitivity) noexcept { return 0.85f - 0.45f * sensitivity; }

    template <std::size_t N>
    [[nodiscard]] static RiffLockResult apply(std::array<DrumEvent, N>& ev, std::size_t count,
                                              const GrooveContext& ctx, const Style& style,
                                              const RiffSnapshot& riff, const RiffLockParams& params,
                                              std::uint32_t seed) noexcept {
        count = std::min(count, N);
        RiffLockResult res;
        res.count = count;

        const float amount = std::isfinite(params.amount) ? std::clamp(params.amount, 0.0f, 1.0f) : 0.0f;
        const float sens = std::isfinite(params.sensitivity) ? std::clamp(params.sensitivity, 0.0f, 1.0f) : 0.5f;
        if (amount <= 0.0f || !riff.locked) return res;
        if (!(ctx.sampleRate > 0.0) || !(ctx.bpm > 0.0) || !std::isfinite(ctx.sampleRate) || !std::isfinite(ctx.bpm)
            || ctx.numerator <= 0 || ctx.denominator <= 0) return res;
        const RiffGrid& g = riff.grid;
        if (g.numerator != ctx.numerator || g.denominator != ctx.denominator || g.slots <= 0) return res;
        if (g.triplet != (ctx.denominator < 8 && std::isfinite(style.swing) && style.swing >= 0.20f)) return res;

        const float gate = gateFor(sens);
        const float conf = std::isfinite(riff.confidence) ? riff.confidence : 0.0f;
        if (conf < gate) return res;
        const float x = std::clamp((conf - gate) / std::max(1.0e-3f, 1.0f - gate), 0.0f, 1.0f);
        const float e = amount * (0.35f + 0.65f * x * x * (3.0f - 2.0f * x));
        const float thr = slotThresholdFor(sens);

        const int spb = g.slotsPerBeat;
        const int slots = g.slots;
        const double samplesPerBeat = ctx.sampleRate * 60.0 / ctx.bpm * (4.0 / static_cast<double>(ctx.denominator));
        const double slotSamples = samplesPerBeat / spb;
        const auto meter = MeterProfile::forMeter(ctx.numerator, ctx.denominator);
        const auto lim = RiffLockLimits::fromStyle(style, ctx.numerator);

        auto slotOf = [&](int offset) -> int {
            const double pos = static_cast<double>(offset) / slotSamples;
            const long r = std::lround(pos);
            if (std::abs(pos - static_cast<double>(r)) > 0.3 || r < 0 || r >= slots) return -1;
            return static_cast<int>(r);
        };
        auto isBackbeatSlot = [&](int s) {
            return s % spb == 0 && s / spb < MeterProfile::MaxBeats && meter.backbeat[static_cast<std::size_t>(s / spb)];
        };
        auto isAnchorSlot = [&](int s) {
            return s % spb == 0 && s / spb < MeterProfile::MaxBeats && meter.kickAnchor[static_cast<std::size_t>(s / spb)];
        };
        auto cons = [&](int s) { return riff.consistency[static_cast<std::size_t>(s)]; };

        int lockedSlots = 0;
        for (int s = 0; s < slots; ++s) if (cons(s) >= thr) ++lockedSlots;
        if (lockedSlots == 0) return res;

        // (b) remove conflicting off-anchor kicks (never beat 1 / slot 0), only against a clear riff.
        if (lockedSlots >= 3) {
            std::size_t w = 0;
            for (std::size_t i = 0; i < count; ++i) {
                bool drop = false;
                if (ev[i].instrument == DrumInstrument::Kick) {
                    const int s = slotOf(ev[i].sampleOffset);
                    if (s > 0 && cons(s) < 0.2f && !isAnchorSlot(s) && hash01(seed, s, 1u) < 0.85f * e) drop = true;
                }
                if (drop) ++res.removed; else ev[w++] = ev[i];
            }
            count = w;
        }

        std::array<bool, RiffGrid::MaxSlots> kickAt{}, snareAt{}, crashAt{};
        int kicks = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const int s = slotOf(ev[i].sampleOffset);
            if (s < 0) { if (ev[i].instrument == DrumInstrument::Kick) ++kicks; continue; }
            const auto u = static_cast<std::size_t>(s);
            if (ev[i].instrument == DrumInstrument::Kick) {
                kickAt[u] = true; ++kicks;
                // (c) accent existing kicks that land on guitar accents
                if (cons(s) >= thr) {
                    const float v = ev[i].velocity + 0.10f * e * riff.strength[u];
                    ev[i].velocity = std::clamp(std::isfinite(v) ? v : ev[i].velocity, 0.0f, 1.0f);
                    ++res.accented;
                }
            } else if (ev[i].instrument == DrumInstrument::Snare && ev[i].velocity >= 0.5f) {
                snareAt[u] = true;
            } else if (ev[i].instrument == DrumInstrument::Crash) {
                crashAt[u] = true;
            }
        }

        // (a) add kicks on consistent guitar slots.
        std::array<int, RiffGrid::MaxSlots> cand{};
        int nc = 0;
        for (int s = 0; s < slots; ++s) {
            if (cons(s) < thr || kickAt[static_cast<std::size_t>(s)]) continue;
            if (!lim.allowBackbeatKick && (snareAt[static_cast<std::size_t>(s)] || isBackbeatSlot(s))) continue;
            cand[static_cast<std::size_t>(nc++)] = s;
        }
        auto better = [&](int a, int b) {
            if (cons(a) != cons(b)) return cons(a) > cons(b);
            const bool ba = a % spb == 0, bb = b % spb == 0;
            if (ba != bb) return ba;
            return a < b;
        };
        for (int i = 1; i < nc; ++i) {
            const int v = cand[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0 && better(v, cand[static_cast<std::size_t>(j)])) {
                cand[static_cast<std::size_t>(j + 1)] = cand[static_cast<std::size_t>(j)];
                --j;
            }
            cand[static_cast<std::size_t>(j + 1)] = v;
        }
        const float intensity = std::isfinite(ctx.intensity) ? std::clamp(ctx.intensity, 0.0f, 1.0f) : 0.5f;
        for (int k = 0; k < nc; ++k) {
            if (res.added >= lim.maxAdded || kicks >= lim.maxKicks || count >= N) break;
            const int s = cand[static_cast<std::size_t>(k)];
            const auto u = static_cast<std::size_t>(s);
            if (!lim.allowAdjacent && ((s > 0 && kickAt[u - 1]) || (s + 1 < slots && kickAt[u + 1]))) continue;
            if (!(hash01(seed, s, 2u) < std::min(1.0f, e * (0.3f + 0.9f * cons(s))))) continue;
            const int offset = static_cast<int>(std::llround(static_cast<double>(s) / spb * samplesPerBeat));
            const float vel = 0.50f + 0.30f * intensity + 0.15f * riff.strength[u];
            ev[count] = {DrumInstrument::Kick, offset, std::clamp(vel, 0.0f, 1.0f), 0};
            ++count; ++kicks; ++res.added; kickAt[u] = true;
        }

        // optional crash accent on the strongest on-beat guitar accent (never beat 1: generator owns that)
        if (params.accentCrash && count < N) {
            int best = -1;
            float bestScore = 0.0f;
            for (int s = spb; s < slots; s += spb) {
                const auto u = static_cast<std::size_t>(s);
                const float sc = cons(s) >= thr ? riff.strength[u] * cons(s) : 0.0f;
                if (sc > bestScore) { bestScore = sc; best = s; }
            }
            if (best > 0 && !crashAt[static_cast<std::size_t>(best)] && hash01(seed, best, 3u) < e) {
                const int offset = static_cast<int>(std::llround(static_cast<double>(best) / spb * samplesPerBeat));
                ev[count++] = {DrumInstrument::Crash, offset, std::clamp(0.70f + 0.20f * intensity, 0.0f, 1.0f), 0};
                ++res.added;
            }
        }

        if (res.added > 0 || res.removed > 0)
            for (std::size_t i = 0; i < count; ++i) ev[i].sequence = static_cast<std::uint32_t>(i);
        res.count = count;
        res.applied = res.added > 0 || res.removed > 0 || res.accented > 0;
        return res;
    }

private:
    static float hash01(std::uint32_t seed, int slot, std::uint32_t salt) noexcept {
        std::uint32_t h = seed * 2654435761u ^ (static_cast<std::uint32_t>(slot) + 1u) * 0x9E3779B1u ^ salt * 0x85EBCA6Bu;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        return static_cast<float>(h >> 8) / static_cast<float>(0x01000000u);
    }
};

} // namespace robodrummer
