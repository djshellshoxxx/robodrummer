// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
// Transient Shaper: level-independent attack/sustain via a fast/slow differential envelope.
// Spec: docs/specs/02-transient-shaper.md
#include "dsp/DspCommon.h"
#include <atomic>

namespace robodrummer::dsp {

struct TransientShaperParams {
    bool enabled{false};
    float attackPct{0.0f};   // -100..100
    float sustainPct{0.0f};  // -100..100
    float mixPct{100.0f};    // 0..100
    float outputDb{0.0f};    // -12..12

    TransientShaperParams& setAttackPct(float v) noexcept { attackPct = clampParam(v, -100.0f, 100.0f, 0.0f); return *this; }
    TransientShaperParams& setSustainPct(float v) noexcept { sustainPct = clampParam(v, -100.0f, 100.0f, 0.0f); return *this; }
    TransientShaperParams& setMixPct(float v) noexcept { mixPct = clampParam(v, 0.0f, 100.0f, 100.0f); return *this; }
    TransientShaperParams& setOutputDb(float v) noexcept { outputDb = clampParam(v, -12.0f, 12.0f, 0.0f); return *this; }
    TransientShaperParams& sanitise() noexcept { return setAttackPct(attackPct).setSustainPct(sustainPct).setMixPct(mixPct).setOutputDb(outputDb); }
};

class TransientShaper {
public:
    TransientShaper() noexcept { prepare(44100.0, 512); }

    // Snaps smoothers/fader to the current params and clears state. Call setParams() first.
    void prepare(double sampleRate, int /*maxBlock*/) noexcept {
        sr_ = sanitiseSampleRate(sampleRate);
        for (auto* s : { &att_, &sus_, &mix_, &out_ }) s->setTime(sr_, 10.0);
        fader_.prepare(sr_);
        fastAtk_ = timeCoef(sr_, 1.0);   fastRel_ = timeCoef(sr_, 35.0);
        slowAtk_ = timeCoef(sr_, 25.0);  slowRel_ = timeCoef(sr_, 150.0);
        applyParams();
        reset();
    }
    void setParams(const TransientShaperParams& p) noexcept {
        p_ = p;
        p_.sanitise();
        applyParams();
    }
    [[nodiscard]] const TransientShaperParams& params() const noexcept { return p_; }
    [[nodiscard]] int latencySamples() const noexcept { return 0; }
    // Gain (dB) applied at the end of the last processed block. Safe to read from the UI thread.
    [[nodiscard]] float gainDb() const noexcept { return meter_.load(std::memory_order_relaxed); }

    void reset() noexcept {
        fast_ = slow_ = 0.0f;
        idle_ = false;
        att_.snap(p_.attackPct * 0.01f); sus_.snap(p_.sustainPct * 0.01f);
        mix_.snap(p_.mixPct * 0.01f); out_.snap(outLinear());
        fader_.snap(p_.enabled);
        meter_.store(0.0f, std::memory_order_relaxed);
    }

    // In-place stereo processing. R may be null (mono). Any block size, allocation free.
    void processBlock(float* L, float* R, int n) noexcept {
        if (L == nullptr || n <= 0) return;
        float gdb = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float inL = finiteOr0(L[i]);
            const float inR = R != nullptr ? finiteOr0(R[i]) : inL;
            const float f = fader_.next();
            if (f <= 0.0f && fader_.offAndSettled()) {
                if (!idle_) { fast_ = slow_ = 0.0f; idle_ = true; }
                L[i] = inL;
                if (R != nullptr) R[i] = inR;
                continue;
            }
            idle_ = false;
            const float xl = clampSignal(inL), xr = clampSignal(inR);
            const float r = std::max(std::fabs(xl), std::fabs(xr));
            fast_ = flushDenormal(r + (r > fast_ ? fastAtk_ : fastRel_) * (fast_ - r), 1.0e-20f);
            slow_ = flushDenormal(r + (r > slow_ ? slowAtk_ : slowRel_) * (slow_ - r), 1.0e-20f);

            const float a = att_.next(), s = sus_.next(), mix = mix_.next(), trim = out_.next();
            gdb = 0.0f;
            if (a != 0.0f || s != 0.0f) {
                // 20*log10(x) = 8.685889638 * ln(x)
                const float d = 8.685889638f * std::log((fast_ + kEps) / (slow_ + kEps));
                // Dead zone: the peak followers disagree by ~1 dB on steady low-frequency ripple, which must not be shaped.
                gdb = d > 0.0f ? a * kCapDb * std::tanh(std::max(d - kDeadZoneDb, 0.0f) / kCapDb)
                              : s * kSusCapDb * std::tanh(kSusScale * std::max(-d - kSusDeadZoneDb, 0.0f) / kSusCapDb);
            }
            const float g = std::exp(gdb * kLn10Over20) * trim;
            const float wl = xl * ((1.0f - mix) + g * mix); // dry*(1-mix)+wet*mix
            const float wr = xr * ((1.0f - mix) + g * mix);
            L[i] = crossfade(inL, wl, f);
            if (R != nullptr) R[i] = crossfade(inR, wr, f);
        }
        meter_.store(gdb, std::memory_order_relaxed);
    }

private:
    static constexpr float kEps = 1.0e-4f;
    static constexpr float kDeadZoneDb = 1.5f;
    static constexpr float kSusDeadZoneDb = 0.3f;
    static constexpr float kCapDb = 12.0f;     // attack: soft cap (dB)
    static constexpr float kSusCapDb = 18.0f;  // sustain: soft cap (dB)
    static constexpr float kSusScale = 3.0f;   // sustain: decay-phase differential is small, so it is scaled up

    [[nodiscard]] float outLinear() const noexcept { return std::exp(p_.outputDb * kLn10Over20); }
    void applyParams() noexcept {
        att_.setTarget(p_.attackPct * 0.01f); sus_.setTarget(p_.sustainPct * 0.01f);
        mix_.setTarget(p_.mixPct * 0.01f); out_.setTarget(p_.outputDb == 0.0f ? 1.0f : outLinear());
        fader_.set(p_.enabled);
    }

    TransientShaperParams p_{};
    double sr_{44100.0};
    ParamSmoother att_, sus_, mix_, out_;
    EnableFader fader_;
    float fastAtk_{0.0f}, fastRel_{0.0f}, slowAtk_{0.0f}, slowRel_{0.0f};
    float fast_{0.0f}, slow_{0.0f};
    bool idle_{false};
    std::atomic<float> meter_{0.0f};
};

} // namespace robodrummer::dsp
