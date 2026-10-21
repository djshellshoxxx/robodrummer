// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
// Drum Bus: soft-knee feed-forward compressor, parallel mix and gentle drive. Spec: docs/specs/01-drum-bus.md
#include "dsp/DspCommon.h"
#include <atomic>

namespace robodrummer::dsp {

struct DrumBusParams {
    bool enabled{false};
    float thresholdDb{-18.0f};  // -60..0
    float ratio{4.0f};          // 1..20
    float kneeDb{6.0f};         // 0..24
    float attackMs{10.0f};      // 0.1..100
    float releaseMs{120.0f};    // 10..1000
    float makeupDb{0.0f};       // -12..24
    bool detectRms{false};      // false = peak
    float scHpfHz{0.0f};        // 0 (off) or 20..500
    float mix{1.0f};            // 0..1, 1 = fully compressed
    float drive{0.0f};          // 0..1

    DrumBusParams& setThresholdDb(float v) noexcept { thresholdDb = clampParam(v, -60.0f, 0.0f, -18.0f); return *this; }
    DrumBusParams& setRatio(float v) noexcept { ratio = clampParam(v, 1.0f, 20.0f, 4.0f); return *this; }
    DrumBusParams& setKneeDb(float v) noexcept { kneeDb = clampParam(v, 0.0f, 24.0f, 6.0f); return *this; }
    DrumBusParams& setAttackMs(float v) noexcept { attackMs = clampParam(v, 0.1f, 100.0f, 10.0f); return *this; }
    DrumBusParams& setReleaseMs(float v) noexcept { releaseMs = clampParam(v, 10.0f, 1000.0f, 120.0f); return *this; }
    DrumBusParams& setMakeupDb(float v) noexcept { makeupDb = clampParam(v, -12.0f, 24.0f, 0.0f); return *this; }
    DrumBusParams& setScHpfHz(float v) noexcept {
        scHpfHz = clampParam(v, 0.0f, 500.0f, 0.0f);
        if (scHpfHz < 20.0f) scHpfHz = 0.0f; // below 20 Hz the filter is off
        return *this;
    }
    DrumBusParams& setMix(float v) noexcept { mix = clampParam(v, 0.0f, 1.0f, 1.0f); return *this; }
    DrumBusParams& setDrive(float v) noexcept { drive = clampParam(v, 0.0f, 1.0f, 0.0f); return *this; }
    DrumBusParams& sanitise() noexcept {
        return setThresholdDb(thresholdDb).setRatio(ratio).setKneeDb(kneeDb).setAttackMs(attackMs).setReleaseMs(releaseMs)
            .setMakeupDb(makeupDb).setScHpfHz(scHpfHz).setMix(mix).setDrive(drive);
    }
};

class DrumBus {
public:
    DrumBus() noexcept { prepare(44100.0, 512); }

    // Snaps smoothers/fader to the current params and clears state. Call setParams() first.
    void prepare(double sampleRate, int /*maxBlock*/) noexcept {
        sr_ = sanitiseSampleRate(sampleRate);
        for (auto* s : { &thr_, &ratio_, &knee_, &makeup_, &mix_, &drive_ }) s->setTime(sr_, 10.0);
        fader_.prepare(sr_);
        rmsCoef_ = timeCoef(sr_, 10.0);
        dcCoef_ = static_cast<float>(1.0 - 6.283185307179586 * 10.0 / sr_);
        applyParams();
        reset();
    }
    void setParams(const DrumBusParams& p) noexcept {
        p_ = p;
        p_.sanitise();
        applyParams();
    }
    [[nodiscard]] const DrumBusParams& params() const noexcept { return p_; }
    [[nodiscard]] int latencySamples() const noexcept { return 0; }
    // Gain reduction at the end of the last processed block (dB, <= 0). Safe to read from the UI thread.
    [[nodiscard]] float gainReductionDb() const noexcept { return meter_.load(std::memory_order_relaxed); }

    void reset() noexcept {
        clearState();
        thr_.snap(p_.thresholdDb); ratio_.snap(p_.ratio); knee_.snap(p_.kneeDb);
        makeup_.snap(p_.makeupDb); mix_.snap(p_.mix); drive_.snap(p_.drive);
        fader_.snap(p_.enabled);
        meter_.store(0.0f, std::memory_order_relaxed);
    }

    // Static compressor curve: gain reduction in dB (<= 0) for an input level in dB.
    [[nodiscard]] static float computeGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f / ratio - 1.0f;
        if (kneeDb < 1.0e-3f) return over > 0.0f ? slope * over : 0.0f;
        if (2.0f * over < -kneeDb) return 0.0f;
        if (2.0f * std::fabs(over) <= kneeDb) {
            const float t = over + 0.5f * kneeDb;
            return slope * t * t / (2.0f * kneeDb);
        }
        return slope * over;
    }

    // In-place stereo processing. R may be null (mono). Any block size, allocation free.
    void processBlock(float* L, float* R, int n) noexcept {
        if (L == nullptr || n <= 0) return;
        for (int i = 0; i < n; ++i) {
            const float inL = finiteOr0(L[i]);
            const float inR = R != nullptr ? finiteOr0(R[i]) : inL;
            const float f = fader_.next();
            if (f <= 0.0f && fader_.offAndSettled()) {
                if (!idle_) { clearState(); idle_ = true; }
                L[i] = inL;
                if (R != nullptr) R[i] = inR;
                continue;
            }
            idle_ = false;
            float wl = 0.0f, wr = 0.0f;
            processSample(inL, inR, wl, wr);
            L[i] = crossfade(inL, wl, f);
            if (R != nullptr) R[i] = crossfade(inR, wr, f);
        }
        meter_.store(gr_, std::memory_order_relaxed);
    }

private:
    struct Biquad { double b0{1}, b1{0}, b2{0}, a1{0}, a2{0}, z1[2]{}, z2[2]{}; };

    void applyParams() noexcept {
        thr_.setTarget(p_.thresholdDb); ratio_.setTarget(p_.ratio); knee_.setTarget(p_.kneeDb);
        makeup_.setTarget(p_.makeupDb); mix_.setTarget(p_.mix); drive_.setTarget(p_.drive);
        fader_.set(p_.enabled);
        atkCoef_ = timeCoef(sr_, p_.attackMs);
        relCoef_ = timeCoef(sr_, p_.releaseMs);
        hpOn_ = p_.scHpfHz >= 20.0f;
        if (hpOn_) {
            const double fc = std::min<double>(p_.scHpfHz, 0.45 * sr_);
            const double w0 = 6.283185307179586 * fc / sr_, c = std::cos(w0);
            const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476), a0 = 1.0 + alpha;
            hp_.b0 = (1.0 + c) * 0.5 / a0; hp_.b1 = -(1.0 + c) / a0; hp_.b2 = hp_.b0;
            hp_.a1 = -2.0 * c / a0; hp_.a2 = (1.0 - alpha) / a0;
        } else {
            for (int ch = 0; ch < 2; ++ch) hp_.z1[ch] = hp_.z2[ch] = 0.0;
        }
    }
    void clearState() noexcept {
        gr_ = 0.0f; ms_ = 0.0f;
        for (int ch = 0; ch < 2; ++ch) { hp_.z1[ch] = hp_.z2[ch] = 0.0; dcX_[ch] = dcY_[ch] = 0.0f; }
        idle_ = false;
    }
    [[nodiscard]] float hp(int ch, float x) noexcept {
        const double in = x;
        const double out = hp_.b0 * in + hp_.z1[ch];
        hp_.z1[ch] = hp_.b1 * in - hp_.a1 * out + hp_.z2[ch];
        hp_.z2[ch] = hp_.b2 * in - hp_.a2 * out;
        if (std::fabs(hp_.z1[ch]) < 1.0e-25) hp_.z1[ch] = 0.0;
        if (std::fabs(hp_.z2[ch]) < 1.0e-25) hp_.z2[ch] = 0.0;
        return static_cast<float>(out);
    }

    void processSample(float inL, float inR, float& wl, float& wr) noexcept {
        const float xl = clampSignal(inL), xr = clampSignal(inR);
        const float thr = thr_.next(), ratio = ratio_.next(), knee = knee_.next();
        const float makeup = makeup_.next(), mix = mix_.next(), drive = drive_.next();

        float scL = xl, scR = xr;
        if (hpOn_) { scL = hp(0, xl); scR = hp(1, xr); }
        float levelDb;
        if (p_.detectRms) {
            const float p = std::max(scL * scL, scR * scR);
            ms_ = flushDenormal(ms_ + (p - ms_) * (1.0f - rmsCoef_), 1.0e-20f);
            levelDb = 10.0f * std::log10(std::max(ms_, 1.0e-12f));
        } else {
            levelDb = 20.0f * std::log10(std::max(std::max(std::fabs(scL), std::fabs(scR)), 1.0e-6f));
        }
        const float target = computeGainDb(levelDb, thr, ratio, knee);
        const float c = target < gr_ ? atkCoef_ : relCoef_;
        gr_ = target + c * (gr_ - target);
        if (!std::isfinite(gr_) || (target == 0.0f && gr_ > -1.0e-6f)) gr_ = 0.0f;

        const float g = std::exp((gr_ + makeup) * kLn10Over20);
        float ul = xl * g, ur = xr * g;
        if (drive > 0.0f) {
            const float a = std::min(1.0f, 4.0f * drive), gd = 1.0f + 2.0f * drive, b = 0.1f * drive;
            const float tb = std::tanh(b), inv = 1.0f / gd;
            const float dL = (std::tanh(gd * ul + b) - tb) * inv - ul;
            const float dR = (std::tanh(gd * ur + b) - tb) * inv - ur;
            ul += a * dcBlock(0, dL);
            ur += a * dcBlock(1, dR);
        } else {
            dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0.0f;
        }
        wl = xl * (1.0f - mix) + ul * mix;
        wr = xr * (1.0f - mix) + ur * mix;
    }
    [[nodiscard]] float dcBlock(int ch, float x) noexcept {
        const float y = flushDenormal(x - dcX_[ch] + dcCoef_ * dcY_[ch], 1.0e-20f);
        dcX_[ch] = x; dcY_[ch] = y;
        return y;
    }

    DrumBusParams p_{};
    double sr_{44100.0};
    ParamSmoother thr_, ratio_, knee_, makeup_, mix_, drive_;
    EnableFader fader_;
    Biquad hp_;
    bool hpOn_{false}, idle_{false};
    float atkCoef_{0.0f}, relCoef_{0.0f}, rmsCoef_{0.0f}, dcCoef_{0.999f};
    float gr_{0.0f}, ms_{0.0f};
    float dcX_[2]{}, dcY_[2]{};
    std::atomic<float> meter_{0.0f};
};

} // namespace robodrummer::dsp
