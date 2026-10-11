// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace robodrummer {

// Cheap realtime-safe stereo room/ambience: a few early-reflection taps plus four short
// damped feedback combs per side. All buffers are sized in prepare() (not realtime-safe);
// process() and reset() never allocate. Output is wet-only.
class RoomAmbience {
public:
    RoomAmbience() { prepare(44100.0); }

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 8000.0 ? sampleRate : 44100.0;
        const double sr = sampleRate_;
        const auto ms = [sr](double v) { return static_cast<std::size_t>(std::max(1.0, v * 0.001 * sr)); };

        earlyLen_ = ms(90.0) + 1;
        earlyL_.assign(earlyLen_, 0.0f);
        earlyR_.assign(earlyLen_, 0.0f);
        static constexpr double tapMsL[NumTaps] = {7.3, 13.1, 23.9, 37.7, 53.3};
        static constexpr double tapMsR[NumTaps] = {9.1, 16.7, 27.7, 41.3, 61.9};
        static constexpr float tapGain[NumTaps] = {0.55f, 0.42f, 0.32f, 0.22f, 0.15f};
        for (std::size_t i = 0; i < NumTaps; ++i) {
            tapL_[i] = ms(tapMsL[i]);
            tapR_[i] = ms(tapMsR[i]);
            tapG_[i] = tapGain[i];
        }

        static constexpr double combMs[NumCombs] = {29.7, 37.1, 41.1, 43.7};
        for (std::size_t i = 0; i < NumCombs; ++i) {
            combL_[i].init(ms(combMs[i]));
            combR_[i].init(ms(combMs[i]) + 23);
        }
        setDecay(decay_);
        setDamping(damping_);
        reset();
    }

    // decay 0..1 (maps to comb feedback 0.30..0.80); damping 0..1 (higher = darker).
    void setDecay(float decay) noexcept {
        decay_ = std::clamp(decay, 0.0f, 1.0f);
        feedback_ = 0.30f + 0.50f * decay_;
    }
    void setDamping(float damping) noexcept {
        damping_ = std::clamp(damping, 0.0f, 1.0f);
        damp_ = 0.05f + 0.85f * damping_;
    }

    void reset() noexcept {
        std::fill(earlyL_.begin(), earlyL_.end(), 0.0f);
        std::fill(earlyR_.begin(), earlyR_.end(), 0.0f);
        earlyPos_ = 0;
        for (auto& c : combL_) c.clear();
        for (auto& c : combR_) c.clear();
    }

    // Approximate seconds until the tail has decayed below audibility (-80 dB).
    [[nodiscard]] double tailSeconds() const noexcept {
        const double fb = std::max(0.05, static_cast<double>(feedback_));
        return 0.050 * (std::log(0.0001) / std::log(fb)) + 0.15;
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept {
        const float mono = 0.5f * (inL + inR);
        earlyL_[earlyPos_] = inL;
        earlyR_[earlyPos_] = inR;
        float eL = 0.0f, eR = 0.0f;
        for (std::size_t i = 0; i < NumTaps; ++i) {
            eL += tapG_[i] * earlyL_[(earlyPos_ + earlyLen_ - tapL_[i]) % earlyLen_];
            eR += tapG_[i] * earlyR_[(earlyPos_ + earlyLen_ - tapR_[i]) % earlyLen_];
        }
        if (++earlyPos_ >= earlyLen_) earlyPos_ = 0;

        float cL = 0.0f, cR = 0.0f;
        for (std::size_t i = 0; i < NumCombs; ++i) {
            cL += combL_[i].process(mono, feedback_, damp_);
            cR += combR_[i].process(mono, feedback_, damp_);
        }
        outL = 0.5f * eL + 0.18f * cL;
        outR = 0.5f * eR + 0.18f * cR;
    }

private:
    static constexpr std::size_t NumTaps = 5;
    static constexpr std::size_t NumCombs = 4;

    struct Comb {
        std::vector<float> buf;
        std::size_t pos{0};
        float lp{0.0f};
        void init(std::size_t len) { buf.assign(std::max<std::size_t>(len, 2), 0.0f); pos = 0; lp = 0.0f; }
        void clear() noexcept { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; lp = 0.0f; }
        float process(float in, float fb, float damp) noexcept {
            const float y = buf[pos];
            lp += (y - lp) * (1.0f - damp);
            if (std::abs(lp) < 1e-20f) lp = 0.0f; // flush denormals
            buf[pos] = in + lp * fb;
            if (++pos >= buf.size()) pos = 0;
            return y;
        }
    };

    double sampleRate_{44100.0};
    std::size_t earlyLen_{1}, earlyPos_{0};
    std::vector<float> earlyL_, earlyR_;
    std::size_t tapL_[NumTaps]{}, tapR_[NumTaps]{};
    float tapG_[NumTaps]{};
    Comb combL_[NumCombs], combR_[NumCombs];
    float decay_{0.5f}, damping_{0.5f}, feedback_{0.55f}, damp_{0.475f};
};

} // namespace robodrummer
