// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace robodrummer {

// Causal meter suggestion from accent periodicity (see docs/specs/07-auto-meter.md). It only ever publishes a
// suggestion and a confidence; deciding whether to use it is AutoMeterGate's / the integrator's job.
class MeterDetector {
public:
    static constexpr int MaxHistory = 60;
    static constexpr int MinL = 2;
    static constexpr int MaxL = 7;

    struct Settings {
        int minBars{3};          // evidence needed before any confidence is reported
        int switchPulses{8};     // consecutive evaluations a challenger must win before the suggestion changes
    };

    struct Suggestion {
        int numerator{4};
        int denominator{4};
        int pulsesPerBar{4};
        float confidence{0.0f};      // 0..1
        bool ternary{false};         // subdivision feel (triplet / compound) of the tracked pulse
        std::uint32_t evaluations{0};  // increments once per committed pulse
    };

    MeterDetector() noexcept { reset(); }

    void setSettings(const Settings& s) noexcept {
        settings_.minBars = std::clamp(s.minBars, 2, 8);
        settings_.switchPulses = std::clamp(s.switchPulses, 1, 64);
    }

    void reset() noexcept {
        accents_.fill(0.0f);
        count_ = 0;
        started_ = false;
        pendingPulse_ = 0;
        pendingAccent_ = 0.0f;
        lastPos_ = 0.0;
        duple_ = triplet_ = 0.0f;
        ternary_ = false;
        published_ = Suggestion{};
        challenger_ = -1;
        streak_ = 0;
    }

    // beatPos: monotone position in tracked pulses; strength: onset strength 0..1.
    void observeOnset(double beatPos, float strength) noexcept {
        if (!std::isfinite(beatPos) || !std::isfinite(strength) || strength <= 0.0f) return;
        strength = std::min(strength, 1.0f);
        if (started_ && beatPos < lastPos_ - 0.5) { reset(); }
        lastPos_ = std::max(lastPos_, beatPos);

        const double nearest = std::floor(beatPos + 0.5);
        const double dev = beatPos - nearest;
        if (!started_) {
            started_ = true;
            pendingPulse_ = std::fabs(dev) <= OnBeatTol ? static_cast<long long>(nearest)
                                                       : static_cast<long long>(std::ceil(beatPos - OnBeatTol));
        }
        advanceTo(beatPos);

        if (std::fabs(dev) <= OnBeatTol) {
            if (static_cast<long long>(nearest) == pendingPulse_) pendingAccent_ = std::max(pendingAccent_, strength);
            return;
        }
        // Off-beat: classify by the nearest subdivision centre.
        const float phase = static_cast<float>(beatPos - std::floor(beatPos));
        static constexpr std::array<float, 5> centres {0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f};
        int best = -1;
        float bestDist = SubTol;
        for (int i = 0; i < 5; ++i) {
            const float d = std::fabs(phase - centres[static_cast<std::size_t>(i)]);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        if (best < 0) return;
        if (best == 1 || best == 3) triplet_ += strength; else duple_ += strength;
    }

    // Moves the clock forward without an onset (a rest) so silent pulses still become zero accents.
    void advanceTo(double beatPos) noexcept {
        if (!started_ || !std::isfinite(beatPos)) return;
        while (static_cast<double>(pendingPulse_) + 0.25 < beatPos) {
            commit(pendingAccent_);
            pendingAccent_ = 0.0f;
            ++pendingPulse_;
        }
    }

    [[nodiscard]] Suggestion suggestion() const noexcept { return published_; }
    [[nodiscard]] int historyCount() const noexcept { return count_; }

private:
    static constexpr double OnBeatTol = 0.18;
    static constexpr float SubTol = 0.07f;
    static constexpr float Prior4 = 0.05f;
    static constexpr float SignificantF = 6.0f;
    static constexpr float RestWeight = 0.1f;
    static constexpr float MinFit = 0.35f;

    struct Fit { float adj{-1.0f}; bool valid{false}; };

    static void meterFor(int L, bool ternary, int& n, int& d) noexcept {
        switch (L) {
            case 2: n = ternary ? 6 : 4; d = ternary ? 8 : 4; break;
            case 3: n = 3; d = 4; break;
            case 5: n = 5; d = 4; break;
            case 6: n = 6; d = 8; break;
            case 7: n = 7; d = 8; break;
            default: n = 4; d = 4; break;
        }
    }

    void commit(float accent) noexcept {
        if (count_ < MaxHistory) {
            accents_[static_cast<std::size_t>(count_++)] = accent;
        } else {
            std::copy(accents_.begin() + 1, accents_.end(), accents_.begin());
            accents_[MaxHistory - 1] = accent;
        }
        duple_ *= 0.97f;
        triplet_ *= 0.97f;
        const float mass = duple_ + triplet_;
        if (mass >= 2.5f) {
            const float ratio = triplet_ / mass;
            if (ratio > 0.65f) ternary_ = true; else if (ratio < 0.35f) ternary_ = false;
        } else if (mass < 1.0f) {
            ternary_ = false;
        }
        evaluate();
    }

    // Weighted one-way fold of the newest n accents modulo L. Rest pulses (accent 0) carry reduced weight so a
    // dropped strum on a strong beat does not wreck the fit. Returns false when there is no variance to explain.
    // `effN` receives the effective sample size.
    bool fold(int L, int n, float& ssBetween, float& ssTotal, float& effN) const noexcept {
        const int start = count_ - n;
        std::array<float, MaxL> sum{}, wsum{};
        float total = 0.0f, wTotal = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float a = accents_[static_cast<std::size_t>(start + i)];
            const float w = a > 0.0f ? 1.0f : RestWeight;
            sum[static_cast<std::size_t>(i % L)] += w * a;
            wsum[static_cast<std::size_t>(i % L)] += w;
            total += w * a;
            wTotal += w;
        }
        const float mean = total / wTotal;
        ssTotal = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float a = accents_[static_cast<std::size_t>(start + i)];
            const float dv = a - mean;
            ssTotal += (a > 0.0f ? 1.0f : RestWeight) * dv * dv;
        }
        ssBetween = 0.0f;
        for (int sl = 0; sl < L; ++sl) {
            const float ws = wsum[static_cast<std::size_t>(sl)];
            if (ws <= 0.0f) continue;
            const float dv = sum[static_cast<std::size_t>(sl)] / ws - mean;
            ssBetween += dv * dv * ws;
        }
        effN = wTotal;
        return ssTotal >= 1.0e-6f * static_cast<float>(n);
    }

    [[nodiscard]] Fit fitFor(int L) const noexcept {
        Fit fit;
        const int bars = count_ / L;
        if (bars < 2) return fit;
        const int n = bars * L;
        fit.valid = true;
        float ssb = 0.0f, sst = 0.0f, effN = 0.0f;
        if (!fold(L, n, ssb, sst, effN) || effN - static_cast<float>(L) < 2.0f) { fit.adj = 0.0f; return fit; }
        const float eta2 = std::clamp(ssb / sst, 0.0f, 1.0f);
        fit.adj = 1.0f - (1.0f - eta2) * (effN - 1.0f) / (effN - static_cast<float>(L));
        return fit;
    }

    // Nested F test: does folding at L explain significantly more than folding at its divisor d (same window)?
    [[nodiscard]] bool longerIsSignificant(int L, int d) const noexcept {
        const int n = (count_ / L) * L;
        float ssbL = 0.0f, sstL = 0.0f, ssbD = 0.0f, sstD = 0.0f, effL = 0.0f, effD = 0.0f;
        if (!fold(L, n, ssbL, sstL, effL) || effL - static_cast<float>(L) < 2.0f) return false;
        fold(d, n, ssbD, sstD, effD);
        const float resid = std::max(sstL - ssbL, 1.0e-6f) / (effL - static_cast<float>(L));
        const float extra = std::max(ssbL - ssbD, 0.0f) / static_cast<float>(L - d);
        return extra / resid > SignificantF;
    }

    void evaluate() noexcept {
        ++published_.evaluations;
        if (count_ < 12) return;

        std::array<Fit, MaxL + 1> fits{};
        int winner = 0;
        float best = -1.0f;
        for (int L = MinL; L <= MaxL; ++L) {
            fits[static_cast<std::size_t>(L)] = fitFor(L);
            const auto& f = fits[static_cast<std::size_t>(L)];
            const float score = f.adj + (L == 4 ? Prior4 : 0.0f);
            if (f.valid && score > best) { best = score; winner = L; }
        }
        if (winner == 0) return;
        // Prefer the shortest divisor of the winner whose extra structure is not significant.
        for (int d = MinL; d < winner; ++d) {
            if (winner % d != 0 || !fits[static_cast<std::size_t>(d)].valid) continue;
            if (!longerIsSignificant(winner, d)) { winner = d; break; }
        }
        float winnerAdj = fits[static_cast<std::size_t>(winner)].adj;
        if (winnerAdj < MinFit) { winner = 4; winnerAdj = fits[4].valid ? std::max(0.0f, fits[4].adj) : 0.0f; }

        const int pulses = (count_ / winner) * winner;
        float other = 0.0f;
        for (int L = MinL; L <= MaxL; ++L) {
            if (L == winner || L % winner == 0 || winner % L == 0) continue;
            if (fits[static_cast<std::size_t>(L)].valid) other = std::max(other, fits[static_cast<std::size_t>(L)].adj);
        }
        const float evidence = std::clamp((static_cast<float>(pulses) - 6.0f) / 14.0f, 0.0f, 1.0f);
        const float sep = std::clamp((winnerAdj - other) / 0.3f, 0.0f, 1.0f);
        float raw = std::clamp(winnerAdj, 0.0f, 1.0f) * evidence * sep;
        if (count_ / winner < settings_.minBars) raw = 0.0f;

        int num = 4, den = 4;
        meterFor(winner, ternary_, num, den);
        const int id = num * 100 + den;
        const int publishedId = published_.numerator * 100 + published_.denominator;

        if (id == publishedId) {
            challenger_ = -1;
            streak_ = 0;
            published_.confidence += (raw - published_.confidence) * 0.35f;
            published_.pulsesPerBar = winner;
        } else {
            streak_ = (challenger_ == id) ? streak_ + 1 : 1;
            challenger_ = id;
            published_.confidence *= 0.9f;
            if (streak_ >= settings_.switchPulses) {
                published_.numerator = num;
                published_.denominator = den;
                published_.pulsesPerBar = winner;
                published_.confidence = raw * 0.6f;
                challenger_ = -1;
                streak_ = 0;
            }
        }
        published_.ternary = ternary_;
        published_.confidence = std::clamp(published_.confidence, 0.0f, 1.0f);
    }

    Settings settings_{};
    std::array<float, MaxHistory> accents_{};
    int count_{0};
    bool started_{false};
    long long pendingPulse_{0};
    float pendingAccent_{0.0f};
    double lastPos_{0.0};
    float duple_{0.0f};
    float triplet_{0.0f};
    bool ternary_{false};
    Suggestion published_{};
    int challenger_{-1};
    int streak_{0};
};

// Decides when a suggestion may actually be used. Never applies while manual meter is on or the host supplies one.
class AutoMeterGate {
public:
    struct Settings {
        float threshold{0.75f};
        int holdBars{2};
    };
    struct Decision {
        bool apply{false};
        int numerator{4};
        int denominator{4};
        float confidence{0.0f};
    };

    void setThreshold(float t) noexcept { settings_.threshold = std::isfinite(t) ? std::clamp(t, 0.5f, 0.95f) : 0.75f; }
    void setHoldBars(int bars) noexcept { settings_.holdBars = std::clamp(bars, 1, 8); }
    void reset() noexcept { *this = AutoMeterGate{settings_}; }

    AutoMeterGate() noexcept = default;
    explicit AutoMeterGate(const Settings& s) noexcept : settings_(s) {}

    // Call once per block with the latest suggestion.
    Decision update(bool manualEnabled, bool hostAvailable, const MeterDetector::Suggestion& s) noexcept {
        if (manualEnabled || hostAvailable) { clear(); return {}; }

        const bool sameAsApplied = applied_ && s.numerator == num_ && s.denominator == den_;
        const float needed = sameAsApplied ? std::max(0.0f, settings_.threshold - 0.25f) : settings_.threshold;
        const bool passing = s.confidence >= needed;

        if (!passing) {
            if (applied_ && sameAsApplied) { clear(); }
            heldPulses_ = 0;
            lastEvaluations_ = s.evaluations;
            return result(s);
        }
        if (s.numerator != heldNum_ || s.denominator != heldDen_) {
            heldNum_ = s.numerator; heldDen_ = s.denominator;
            heldPulses_ = 0;
        } else if (s.evaluations != lastEvaluations_) {
            heldPulses_ += static_cast<int>(std::min<std::uint32_t>(s.evaluations - lastEvaluations_, 64u));
        }
        lastEvaluations_ = s.evaluations;
        if (heldPulses_ >= settings_.holdBars * std::max(1, s.pulsesPerBar)) {
            applied_ = true;
            num_ = s.numerator;
            den_ = s.denominator;
        }
        return result(s);
    }

    // "Auto meter: 3/4 (82%)"; "Auto meter: --" when there is nothing useful to show.
    static int formatReadout(char* buf, std::size_t cap, const MeterDetector::Suggestion& s) noexcept {
        if (buf == nullptr || cap == 0) return 0;
        if (s.confidence < 0.01f)
            return std::snprintf(buf, cap, "Auto meter: --");
        return std::snprintf(buf, cap, "Auto meter: %d/%d (%d%%)", s.numerator, s.denominator,
                             static_cast<int>(std::lround(std::clamp(s.confidence, 0.0f, 1.0f) * 100.0f)));
    }

private:
    void clear() noexcept {
        applied_ = false;
        heldPulses_ = 0;
        heldNum_ = heldDen_ = 0;
        lastEvaluations_ = 0;
    }
    Decision result(const MeterDetector::Suggestion& s) const noexcept {
        Decision d;
        d.apply = applied_;
        d.numerator = applied_ ? num_ : 4;
        d.denominator = applied_ ? den_ : 4;
        d.confidence = s.confidence;
        return d;
    }

    Settings settings_{};
    bool applied_{false};
    int num_{4}, den_{4};
    int heldNum_{0}, heldDen_{0};
    int heldPulses_{0};
    std::uint32_t lastEvaluations_{0};
};

} // namespace robodrummer
