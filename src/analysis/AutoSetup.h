// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
// Auto Setup ("Learn My Playing"), see docs/specs/06-auto-setup.md.
// The guitarist plays for ~10 s; the plug-in proposes parameter values plus a human-readable reason for each.
// Nothing is applied here: the UI shows a review card, then uses applyProposal()/AutoSetupUndo.
#include "analysis/OnsetDetector.h"
#include "analysis/PerformanceAnalyzer.h"
#include "analysis/TempoTracker.h"
#include "analysis/TimingAuthorityController.h"
#include "core/JamStyleProfile.h"
#include "core/StyleLibrary.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace robodrummer {

enum class AutoSetupState : std::uint8_t { Idle, Listening, Analyzing, Done, Cancelled };
enum class AutoSetupPrompt : std::uint8_t { None, GetReady, PlayNaturally, KeepPlaying, PlayLouder };
enum class AutoSetupStatus : std::uint8_t { Ok, NotEnoughSignal, TimedOut, Cancelled };

[[nodiscard]] inline const char* autoSetupPromptText(AutoSetupPrompt p) noexcept {
    switch (p) {
        case AutoSetupPrompt::GetReady: return "Get ready: stay quiet for a second, then play.";
        case AutoSetupPrompt::PlayNaturally: return "Play naturally, the way you would in a jam.";
        case AutoSetupPrompt::KeepPlaying: return "Keep playing, I need a little more to go on.";
        case AutoSetupPrompt::PlayLouder: return "I can barely hear you. Check your input or play a bit louder.";
        case AutoSetupPrompt::None: break;
    }
    return "";
}

// One proposed value. The UI may clear `apply` per field (the review card checkboxes).
template <class T>
struct AutoSetupField {
    bool apply{false};
    T value{};
};

struct AutoSetupProposal {
    AutoSetupField<float> inputGainDb;           // param inputGain, -24..36 dB
    AutoSetupField<bool> autoGain;               // param autoGain
    AutoSetupField<float> gateThresholdDb;       // param gateThreshold, -90 (off) .. -20 dB
    AutoSetupField<float> followStrength;        // 0..1
    AutoSetupField<float> tempoInertia;          // 0..1
    AutoSetupField<float> confidenceSensitivity; // 0..1
    AutoSetupField<LeadershipMode> timingMode;   // param leadershipMode
    AutoSetupField<float> leadership;            // param leadership 0..1 (Hybrid only)
    AutoSetupField<float> bpm;                   // param bpm 40..240
    AutoSetupField<JamStyle> jamStyle;
    AutoSetupField<float> intensity;             // 0..1
    std::uint64_t generation{0};                 // transient run identity; never serialized
    [[nodiscard]] bool any() const noexcept {
        return inputGainDb.apply || autoGain.apply || gateThresholdDb.apply || followStrength.apply || tempoInertia.apply ||
               confidenceSensitivity.apply || timingMode.apply || leadership.apply || bpm.apply || jamStyle.apply || intensity.apply;
    }
};

// What was measured (for the UI's details view and for tests).
struct AutoSetupMeasurements {
    float noiseFloorDb{-100.0f}, typicalRmsDb{-100.0f}, loudRmsDb{-100.0f}, peakDb{-100.0f};
    bool clipping{false};
    float activeSeconds{0.0f};
    int onsetCount{0};
    float trackedBpm{0.0f};      // median tracker tempo, octave-folded into 60..170
    float bpm{0.0f};             // final proposal basis
    float tempoConfidence{0.0f}; // median tracker confidence
    float regularity{0.0f};      // 0..1 grid regularity
    float timingSdMs{0.0f};      // RMS deviation from the fitted grid
    float looseness{0.0f};       // 0 tight .. 1 loose
    int subdivision{0};
    float swing{-1.0f};          // -1 unknown, 0 straight .. 1 triplet swing
    float onsetDensity{0.0f};    // onsets per second
    float lowShare{0.0f};
    float transientStrength{0.0f};
    float intensity{0.0f};
};

struct AutoSetupResult {
    static constexpr int MaxLines = 16;
    static constexpr int LineChars = 112;
    AutoSetupStatus status{AutoSetupStatus::NotEnoughSignal};
    bool levelsValid{false};
    bool rhythmValid{false};
    AutoSetupProposal proposal{};
    AutoSetupMeasurements measured{};
    int lineCount{0};
    std::array<std::array<char, LineChars>, MaxLines> lines{};
    [[nodiscard]] const char* line(int i) const noexcept {
        return (i >= 0 && i < lineCount) ? lines[static_cast<std::size_t>(i)].data() : "";
    }
};

// ---- Snapshot / apply / undo -------------------------------------------------------------------------------------------

struct AutoSetupSnapshot {
    float inputGainDb{0.0f};
    bool autoGain{false};
    float gateThresholdDb{-90.0f};
    float followStrength{1.0f};
    float tempoInertia{0.5f};
    float confidenceSensitivity{0.5f};
    LeadershipMode timingMode{LeadershipMode::DrummerLeads};
    float leadership{0.5f};
    float bpm{120.0f};
    JamStyle jamStyle{JamStyle::Rock};
    float intensity{0.5f};
};

namespace autosetup_detail {
[[nodiscard]] inline float sane(float v, float lo, float hi, float fallback) noexcept {
    if (!std::isfinite(v)) return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}
[[nodiscard]] inline int styleIndexSafe(JamStyle s) noexcept {
    const int i = static_cast<int>(s);
    return i >= 0 && i < kJamStyleCount ? i : 0;
}
} // namespace autosetup_detail

enum AutoSetupBit : std::uint32_t {
    kAsInputGain = 1u << 0, kAsAutoGain = 1u << 1, kAsGate = 1u << 2, kAsFollowStrength = 1u << 3, kAsInertia = 1u << 4,
    kAsConfidence = 1u << 5, kAsTimingMode = 1u << 6, kAsLeadership = 1u << 7, kAsBpm = 1u << 8, kAsStyle = 1u << 9,
    kAsIntensity = 1u << 10
};

[[nodiscard]] inline std::uint32_t appliedMask(const AutoSetupProposal& p) noexcept {
    std::uint32_t m = 0;
    if (p.inputGainDb.apply) m |= kAsInputGain;
    if (p.autoGain.apply) m |= kAsAutoGain;
    if (p.gateThresholdDb.apply) m |= kAsGate;
    if (p.followStrength.apply) m |= kAsFollowStrength;
    if (p.tempoInertia.apply) m |= kAsInertia;
    if (p.confidenceSensitivity.apply) m |= kAsConfidence;
    if (p.timingMode.apply) m |= kAsTimingMode;
    if (p.leadership.apply) m |= kAsLeadership;
    if (p.bpm.apply) m |= kAsBpm;
    if (p.jamStyle.apply) m |= kAsStyle;
    if (p.intensity.apply) m |= kAsIntensity;
    return m;
}

// Copies the fields selected by `mask` from `from` into `into` (range-clamped, NaN-safe).
inline void copyMasked(AutoSetupSnapshot& into, const AutoSetupSnapshot& from, std::uint32_t mask) noexcept {
    using autosetup_detail::sane;
    if (mask & kAsInputGain) into.inputGainDb = sane(from.inputGainDb, -24.0f, 36.0f, into.inputGainDb);
    if (mask & kAsAutoGain) into.autoGain = from.autoGain;
    if (mask & kAsGate) into.gateThresholdDb = sane(from.gateThresholdDb, -90.0f, -20.0f, into.gateThresholdDb);
    if (mask & kAsFollowStrength) into.followStrength = sane(from.followStrength, 0.0f, 1.0f, into.followStrength);
    if (mask & kAsInertia) into.tempoInertia = sane(from.tempoInertia, 0.0f, 1.0f, into.tempoInertia);
    if (mask & kAsConfidence) into.confidenceSensitivity = sane(from.confidenceSensitivity, 0.0f, 1.0f, into.confidenceSensitivity);
    if (mask & kAsTimingMode) into.timingMode = from.timingMode;
    if (mask & kAsLeadership) into.leadership = sane(from.leadership, 0.0f, 1.0f, into.leadership);
    if (mask & kAsBpm) into.bpm = sane(from.bpm, 40.0f, 240.0f, into.bpm);
    if (mask & kAsStyle) into.jamStyle = static_cast<JamStyle>(autosetup_detail::styleIndexSafe(from.jamStyle));
    if (mask & kAsIntensity) into.intensity = sane(from.intensity, 0.0f, 1.0f, into.intensity);
}

// The full parameter state after applying only the fields whose `apply` flag is set.
[[nodiscard]] inline AutoSetupSnapshot applyProposal(const AutoSetupProposal& p, AutoSetupSnapshot current) noexcept {
    AutoSetupSnapshot proposed = current;
    proposed.inputGainDb = p.inputGainDb.value; proposed.autoGain = p.autoGain.value;
    proposed.gateThresholdDb = p.gateThresholdDb.value; proposed.followStrength = p.followStrength.value;
    proposed.tempoInertia = p.tempoInertia.value; proposed.confidenceSensitivity = p.confidenceSensitivity.value;
    proposed.timingMode = p.timingMode.value; proposed.leadership = p.leadership.value; proposed.bpm = p.bpm.value;
    proposed.jamStyle = p.jamStyle.value; proposed.intensity = p.intensity.value;
    copyMasked(current, proposed, appliedMask(p));
    return current;
}

// Undo for one Apply. restore() yields the before-values and which fields to write back, so manual edits made to
// OTHER parameters after Apply are left alone. The integrator clears it on preset/state load and on a new run.
class AutoSetupUndo {
public:
    void capture(const AutoSetupSnapshot& before, const AutoSetupProposal& applied) noexcept {
        before_ = before; mask_ = appliedMask(applied); available_ = mask_ != 0;
    }
    [[nodiscard]] bool available() const noexcept { return available_; }
    [[nodiscard]] std::uint32_t mask() const noexcept { return mask_; }
    [[nodiscard]] const AutoSetupSnapshot& before() const noexcept { return before_; }
    // Returns `current` with the applied fields put back; marks the undo as used.
    [[nodiscard]] AutoSetupSnapshot restore(AutoSetupSnapshot current) noexcept {
        if (available_) copyMasked(current, before_, mask_);
        available_ = false;
        return current;
    }
    void clear() noexcept { available_ = false; mask_ = 0; }
private:
    AutoSetupSnapshot before_{};
    std::uint32_t mask_{0};
    bool available_{false};
};

// ---- Style choice table (transparent, deterministic) ---------------------------------------------------------------------

struct AutoStyleTarget {
    JamStyle style;
    float density;  // onsets per second
    float lowShare; // low band / total
    float strength; // mean onset strength 0..1
    float swing;    // 0 straight .. 1 swung
};

inline constexpr std::array<AutoStyleTarget, 18> kAutoStyleTargets{{
    {JamStyle::Rock, 3.5f, 0.35f, 0.50f, 0.0f},        {JamStyle::Blues, 2.5f, 0.35f, 0.45f, 0.7f},
    {JamStyle::Funk, 5.5f, 0.25f, 0.60f, 0.0f},        {JamStyle::Punk, 6.5f, 0.40f, 0.55f, 0.0f},
    {JamStyle::Metal, 6.0f, 0.55f, 0.65f, 0.0f},       {JamStyle::Shuffle, 4.0f, 0.35f, 0.50f, 1.0f},
    {JamStyle::ClassicRock, 3.5f, 0.35f, 0.50f, 0.0f}, {JamStyle::HardRock, 4.5f, 0.45f, 0.60f, 0.0f},
    {JamStyle::Alternative, 3.0f, 0.35f, 0.45f, 0.0f}, {JamStyle::Grunge, 3.0f, 0.50f, 0.60f, 0.0f},
    {JamStyle::Soul, 2.5f, 0.30f, 0.40f, 0.2f},        {JamStyle::Pop, 3.0f, 0.30f, 0.40f, 0.0f},
    {JamStyle::Indie, 3.5f, 0.30f, 0.45f, 0.0f},       {JamStyle::GarageRock, 4.0f, 0.35f, 0.60f, 0.0f},
    {JamStyle::Country, 3.5f, 0.30f, 0.45f, 0.0f},     {JamStyle::Reggae, 2.0f, 0.30f, 0.40f, 0.0f},
    {JamStyle::Disco, 4.5f, 0.25f, 0.50f, 0.0f},       {JamStyle::JazzSwing, 3.0f, 0.25f, 0.35f, 1.0f},
}};

struct AutoStyleChoice {
    JamStyle best{JamStyle::Rock};
    JamStyle second{JamStyle::Rock};
    float bestScore{0.0f};
};

[[nodiscard]] inline float autoStyleScore(const AutoStyleTarget& t, float bpm, float density, float lowShare, float strength, float swing) noexcept {
    const auto fit = [](float x, float target, float scale) { return std::max(0.0f, 1.0f - std::abs(x - target) / scale); };
    const Style s = styleFor(t.style);
    const float outside = bpm < s.tempoMinBpm ? s.tempoMinBpm - bpm : (bpm > s.tempoMaxBpm ? bpm - s.tempoMaxBpm : 0.0f);
    const float tempoFit = std::max(0.0f, 1.0f - outside / 25.0f);
    const float swingFit = swing < 0.0f ? 0.5f : fit(swing, t.swing, 1.0f);
    return 0.40f * tempoFit + 0.25f * fit(density, t.density, 3.0f) + 0.15f * fit(lowShare, t.lowShare, 0.3f) +
           0.10f * fit(strength, t.strength, 0.4f) + 0.10f * swingFit;
}

[[nodiscard]] inline AutoStyleChoice chooseAutoStyle(float bpm, float density, float lowShare, float strength, float swing) noexcept {
    AutoStyleChoice c;
    float best = -1.0f, second = -1.0f;
    for (const auto& t : kAutoStyleTargets) {
        const float sc = autoStyleScore(t, bpm, density, lowShare, strength, swing);
        if (sc > best) { second = best; c.second = c.best; best = sc; c.best = t.style; }
        else if (sc > second) { second = sc; c.second = t.style; }
    }
    c.bestScore = best;
    if (best < 0.45f) { c.best = JamStyle::Rock; }
    return c;
}

// ---- The analyser ------------------------------------------------------------------------------------------------------

class AutoSetup {
public:
    static constexpr int ChunkSamples = 256; // all sub-analysers see fixed chunks, so results do not depend on host block size

    // Audio thread, from prepareToPlay.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        advanceGeneration(AutoSetupState::Idle);
        audioGeneration_ = 0;
        cancelGeneration_.store(0);
        prompt_.store(AutoSetupPrompt::None);
        progress_.store(0.0f);
    }

    // Message thread. seconds is clamped to 8..12.
    void start(float seconds = 10.0f) noexcept {
        duration_.store(autosetup_detail::sane(seconds, 8.0f, 12.0f, 10.0f));
        progress_.store(0.0f);
        prompt_.store(AutoSetupPrompt::GetReady);
        peakDb_.store(-100.0f);
        result_ = {};
        advanceGeneration(AutoSetupState::Listening);
    }

    // Message thread. A listening cancellation is consumed at the next audio block.
    void cancel() noexcept {
        const auto token = runState_.load(std::memory_order_acquire);
        const auto s = decodeState(token);
        if (s == AutoSetupState::Listening) cancelGeneration_.store(token >> StateBits, std::memory_order_release);
        else if (s == AutoSetupState::Analyzing || s == AutoSetupState::Done) {
            result_.status = AutoSetupStatus::Cancelled; result_.proposal = {}; result_.lineCount = 0;
            auto expected = token;
            runState_.compare_exchange_strong(expected, encode(token >> StateBits, AutoSetupState::Cancelled),
                                             std::memory_order_release, std::memory_order_relaxed);
        }
    }

    // Host state/preset replacement may invalidate from another thread. This
    // changes only the atomic token, never audio-owned measurements or UI result storage.
    void invalidate() noexcept { advanceGeneration(AutoSetupState::Cancelled); }
    [[nodiscard]] std::uint64_t generation() const noexcept { return runState_.load(std::memory_order_acquire) >> StateBits; }
    [[nodiscard]] bool accepts(const AutoSetupProposal& proposal) const noexcept {
        const auto token = runState_.load(std::memory_order_acquire);
        return decodeState(token) == AutoSetupState::Done && proposal.generation != 0
            && proposal.generation == (token >> StateBits) && result_.status == AutoSetupStatus::Ok;
    }
    [[nodiscard]] AutoSetupState state() const noexcept { return decodeState(runState_.load(std::memory_order_acquire)); }
    [[nodiscard]] AutoSetupPrompt prompt() const noexcept { return prompt_.load(std::memory_order_relaxed); }
    [[nodiscard]] float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    [[nodiscard]] float inputPeakDb() const noexcept { return peakDb_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool running() const noexcept { return state() == AutoSetupState::Listening; }
    // Valid once state() == Done (or Cancelled).
    [[nodiscard]] const AutoSetupResult& result() const noexcept { return result_; }

    // Audio thread: RAW mono guitar (before InputConditioner). A cheap no-op unless a run is active.
    void process(const float* mono, int numSamples) noexcept {
        const auto token = runState_.load(std::memory_order_acquire);
        if (decodeState(token) != AutoSetupState::Listening) return;
        const auto runGeneration = token >> StateBits;
        if (audioGeneration_ != runGeneration) { beginRun(); audioGeneration_ = runGeneration; }
        if (runState_.load(std::memory_order_acquire) != token) return;
        if (cancelGeneration_.load(std::memory_order_acquire) == runGeneration) {
            auto expected = token;
            runState_.compare_exchange_strong(expected, encode(runGeneration, AutoSetupState::Cancelled),
                                             std::memory_order_release, std::memory_order_relaxed);
            return;
        }
        for (int i = 0; i < numSamples; ++i) {
            float x = 0.0f;
            if (mono != nullptr) {
                if (std::isfinite(mono[i])) x = std::clamp(mono[i], -8.0f, 8.0f);
                else ++badSamples_;
            }
            staging_[static_cast<std::size_t>(staged_++)] = x;
            if (staged_ == ChunkSamples) {
                staged_ = 0;
                processChunk();
                if (runState_.load(std::memory_order_acquire) != token) return;
            }
        }
    }

    // Message thread (UI timer) once state() == Analyzing. Does the maths and formats the explanations.
    void finalize() noexcept {
        const auto token = runState_.load(std::memory_order_acquire);
        if (decodeState(token) != AutoSetupState::Analyzing) return;
        buildResult();
        result_.proposal.generation = token >> StateBits;
        progress_.store(1.0f);
        prompt_.store(AutoSetupPrompt::None);
        auto expected = token;
        runState_.compare_exchange_strong(expected, encode(token >> StateBits, AutoSetupState::Done),
                                         std::memory_order_release, std::memory_order_relaxed);
    }

    [[nodiscard]] int badSampleCount() const noexcept { return badSamples_; }

private:
    // State and generation form one value so an old audio block cannot publish
    // Analyzing over a newer cancellation/start. No retry loop runs in process().
    static constexpr unsigned StateBits = 3;
    static constexpr std::uint64_t StateMask = (1u << StateBits) - 1u;
    static constexpr std::uint64_t encode(std::uint64_t generation, AutoSetupState state) noexcept {
        return (generation << StateBits) | static_cast<std::uint64_t>(state);
    }
    static constexpr AutoSetupState decodeState(std::uint64_t token) noexcept {
        return static_cast<AutoSetupState>(token & StateMask);
    }
    void advanceGeneration(AutoSetupState state) noexcept {
        auto previous = runState_.load(std::memory_order_relaxed);
        std::uint64_t next;
        do {
            const auto generation = ((previous >> StateBits) + 1) & (UINT64_MAX >> StateBits);
            next = encode(generation == 0 ? 1 : generation, state);
        } while (!runState_.compare_exchange_weak(previous, next, std::memory_order_acq_rel, std::memory_order_relaxed));
    }
    static constexpr int kMaxOnsets = 256;
    static constexpr int kMaxTempoSamples = 96;
    static constexpr int kMaxLead = 128;
    static constexpr int kHistBins = 100;

    struct GridFit {
        bool valid{false};
        float normalized{1.0f};
        float sdMs{0.0f};
        double beatSeconds{0.0};
        int sub{1};
        float score{9.0f};
    };

    void beginRun() noexcept {
        durationNominal_ = duration_.load();
        durationCap_ = durationNominal_ * 1.6f;
        detector_.prepare(sampleRate_);
        perf_.prepare(sampleRate_);
        tempo_.reset();
        windowSamples_ = std::max(1, static_cast<int>(sampleRate_ * 0.010));
        tempoEveryChunks_ = std::max(1, static_cast<int>(std::lround(0.25 * sampleRate_ / ChunkSamples)));
        staged_ = 0; chunks_ = 0; windowEnergy_ = 0.0; windowFill_ = 0; windowIndex_ = 0; leadCount_ = 0;
        haveLead_ = false; leadMedianDb_ = 0.0f; activeThresholdDb_ = -50.0f;
        hist_.fill(0); windows_ = 0; activeWindows_ = 0; lastWindowActive_ = false;
        peak_ = 0.0f; clipSamples_ = 0; badSamples_ = 0;
        onsetCount_ = 0; strengthSum_ = 0.0; tempoCount_ = 0;
        activeChunks_ = 0; bandLowSum_ = bandMidSum_ = bandHighSum_ = 0.0; intensitySum_ = 0.0;
        hitCap_ = false;
    }

    void processChunk() noexcept {
        const float* x = staging_.data();
        const long long baseSample = chunks_ * ChunkSamples;
        std::array<OnsetEvent, 32> events{};
        const std::size_t found = detector_.processBlock(x, ChunkSamples, events.data(), events.size());
        for (std::size_t i = 0; i < found; ++i) {
            const double t = static_cast<double>(baseSample + events[i].sampleOffset) / sampleRate_;
            const float strength = std::clamp(events[i].strength, 0.0f, 1.0f);
            if (onsetCount_ < kMaxOnsets) {
                onsetTimes_[static_cast<std::size_t>(onsetCount_)] = t;
                onsetStrengths_[static_cast<std::size_t>(onsetCount_)] = strength;
                ++onsetCount_;
            }
            strengthSum_ += strength;
            tempo_.addOnset(t, std::max(0.05f, strength));
            perf_.noteOnset(t, strength);
        }
        perf_.processBlock(x, ChunkSamples);

        float chunkPeak = 0.0f;
        for (int i = 0; i < ChunkSamples; ++i) {
            const float v = x[i];
            const float a = std::abs(v);
            chunkPeak = std::max(chunkPeak, a);
            if (a >= 0.99f) ++clipSamples_;
            windowEnergy_ += static_cast<double>(v) * v;
            if (++windowFill_ >= windowSamples_) { closeWindow(); }
        }
        peak_ = std::max(peak_, chunkPeak);
        peakDb_.store(peak_ > 1.0e-6f ? 20.0f * std::log10(peak_) : -100.0f, std::memory_order_relaxed);

        if (lastWindowActive_) {
            const PerformanceState ps = perf_.state();
            ++activeChunks_;
            bandLowSum_ += ps.bandLow; bandMidSum_ += ps.bandMid; bandHighSum_ += ps.bandHigh;
            intensitySum_ += ps.intensity;
        }

        ++chunks_;
        if (tempoCount_ < kMaxTempoSamples && onsetCount_ >= 8 && chunks_ % tempoEveryChunks_ == 0) {
            const TempoHypothesis h = tempo_.best();
            if (h.bpm > 0.0) {
                tempoBpm_[static_cast<std::size_t>(tempoCount_)] = static_cast<float>(h.bpm);
                tempoConf_[static_cast<std::size_t>(tempoCount_)] = h.confidence;
                ++tempoCount_;
            }
        }
        updateRunState();
    }

    void closeWindow() noexcept {
        const double rms = std::sqrt(windowEnergy_ / windowFill_);
        const float db = rms > 1.0e-6 ? static_cast<float>(20.0 * std::log10(rms)) : -100.0f;
        windowEnergy_ = 0.0; windowFill_ = 0;
        ++hist_[static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(db)) + 100, 0, kHistBins - 1))];
        ++windows_;
        if (windowIndex_ < 100) {
            if (leadCount_ < kMaxLead) lead_[static_cast<std::size_t>(leadCount_++)] = db;
            lastWindowActive_ = false;
        } else {
            if (!haveLead_) {
                haveLead_ = true;
                std::nth_element(lead_.begin(), lead_.begin() + leadCount_ / 2, lead_.begin() + leadCount_);
                leadMedianDb_ = lead_[static_cast<std::size_t>(leadCount_ / 2)];
                // A loud lead means the player started right away: do not trust it as a noise floor.
                activeThresholdDb_ = leadMedianDb_ < -35.0f ? std::max(-70.0f, leadMedianDb_ + 12.0f) : -50.0f;
            }
            lastWindowActive_ = db > activeThresholdDb_;
            if (lastWindowActive_) ++activeWindows_;
        }
        ++windowIndex_;
    }

    [[nodiscard]] float activeSeconds() const noexcept {
        return static_cast<float>(static_cast<double>(activeWindows_) * windowSamples_ / sampleRate_);
    }

    void updateRunState() noexcept {
        const float t = static_cast<float>(static_cast<double>(chunks_) * ChunkSamples / sampleRate_);
        const float act = activeSeconds();
        progress_.store(std::min(0.99f, t / durationNominal_), std::memory_order_relaxed);

        AutoSetupPrompt p;
        if (t < 1.0f) p = AutoSetupPrompt::GetReady;
        else if (t > 3.0f && peak_ < 0.0056f && onsetCount_ < 2) p = AutoSetupPrompt::PlayLouder; // -45 dBFS
        else if ((t > 3.0f && act < 0.15f * (t - 1.0f)) || t >= durationNominal_) p = AutoSetupPrompt::KeepPlaying;
        else p = AutoSetupPrompt::PlayNaturally;
        prompt_.store(p, std::memory_order_relaxed);

        bool finish = false;
        if (t >= 5.0f && act < 0.3f && onsetCount_ < 2) finish = true;                       // nothing heard
        else if (t >= durationNominal_ && act >= 4.0f && onsetCount_ >= 12) finish = true;    // enough material
        else if (t >= durationCap_) { finish = true; hitCap_ = true; }                         // timeout
        if (finish) {
            auto expected = encode(audioGeneration_, AutoSetupState::Listening);
            runState_.compare_exchange_strong(expected, encode(audioGeneration_, AutoSetupState::Analyzing),
                                             std::memory_order_release, std::memory_order_relaxed);
        }
    }

    // ---- analysis (message thread) ----

    [[nodiscard]] float percentileDb(double fraction) const noexcept {
        if (windows_ == 0) return -100.0f;
        const long long target = static_cast<long long>(std::ceil(fraction * static_cast<double>(windows_)));
        long long cumulative = 0;
        for (int bin = 0; bin < kHistBins; ++bin) {
            cumulative += hist_[static_cast<std::size_t>(bin)];
            if (cumulative >= target) return static_cast<float>(bin - 100) + 0.5f;
        }
        return 0.0f;
    }

    [[nodiscard]] static double foldBpm(double bpm) noexcept {
        if (!(bpm > 1.0) || !std::isfinite(bpm)) return 0.0;
        while (bpm < 60.0) bpm *= 2.0;
        while (bpm >= 170.0) bpm *= 0.5;
        return bpm;
    }

    [[nodiscard]] GridFit fitGrid(double beatSeconds, int sub) const noexcept {
        GridFit g;
        g.sub = sub; g.beatSeconds = beatSeconds;
        const int n = onsetCount_;
        if (n < 8 || !(beatSeconds > 0.05)) return g;
        const double twoPi = 6.283185307179586;
        const double P = beatSeconds / sub;
        double c = 0.0, s = 0.0;
        for (int i = 0; i < n; ++i) {
            const double u = onsetTimes_[static_cast<std::size_t>(i)] / P;
            const double ang = twoPi * (u - std::floor(u));
            c += std::cos(ang); s += std::sin(ang);
        }
        if (c * c + s * s < 1.0e-9) return g;
        double a = std::atan2(s, c) / twoPi * P;
        double b = P;
        for (int iter = 0; iter < 3; ++iter) {
            double Sn = 0, Snn = 0, St = 0, Snt = 0;
            for (int i = 0; i < n; ++i) {
                const double t = onsetTimes_[static_cast<std::size_t>(i)];
                const double k = std::round((t - a) / b);
                Sn += k; Snn += k * k; St += t; Snt += k * t;
            }
            const double det = n * Snn - Sn * Sn;
            if (det < 1.0e-6) return g; // all onsets on one grid index
            const double b2 = (n * Snt - Sn * St) / det;
            const double a2 = (St - b2 * Sn) / n;
            if (!(b2 > 0.7 * P && b2 < 1.3 * P)) break;
            a = a2; b = b2;
        }
        double sq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = onsetTimes_[static_cast<std::size_t>(i)];
            const double r = t - (a + b * std::round((t - a) / b));
            sq += r * r;
        }
        const double rms = std::sqrt(sq / n);
        g.valid = true;
        g.normalized = static_cast<float>(rms / b);
        g.sdMs = static_cast<float>(rms * 1000.0);
        g.beatSeconds = b * sub;
        g.score = g.normalized + 0.002f * static_cast<float>(sub - 1);
        return g;
    }

    template <class... A>
    void addLine(AutoSetupResult& r, const char* fmt, A... args) noexcept {
        if (r.lineCount >= AutoSetupResult::MaxLines) return;
        auto& buf = r.lines[static_cast<std::size_t>(r.lineCount++)];
        if constexpr (sizeof...(A) == 0) std::snprintf(buf.data(), buf.size(), "%s", fmt);
        else std::snprintf(buf.data(), buf.size(), fmt, args...);
    }

    void buildResult() noexcept {
        using autosetup_detail::sane;
        AutoSetupResult r;
        auto& m = r.measured;
        const float act = activeSeconds();
        m.activeSeconds = act;
        m.onsetCount = onsetCount_;

        // --- levels ---
        float noise = percentileDb(0.10);
        if (haveLead_ && leadCount_ >= 50) noise = std::min(noise, leadMedianDb_);
        m.noiseFloorDb = noise;
        m.typicalRmsDb = percentileDb(0.50);
        m.loudRmsDb = percentileDb(0.95);
        m.peakDb = peak_ > 1.0e-6f ? 20.0f * std::log10(peak_) : -100.0f;
        m.clipping = peak_ >= 0.98f || clipSamples_ >= 3;
        r.levelsValid = act >= 1.5f && m.peakDb > -70.0f;

        // --- rhythm ---
        if (onsetCount_ >= 12 && tempoCount_ >= 1) {
            std::array<float, kMaxTempoSamples> bpms{}, confs{};
            int nb = 0, nc = 0;
            for (int i = 0; i < tempoCount_; ++i) {
                confs[static_cast<std::size_t>(nc++)] = tempoConf_[static_cast<std::size_t>(i)];
                if (tempoConf_[static_cast<std::size_t>(i)] >= 0.25f) bpms[static_cast<std::size_t>(nb++)] = tempoBpm_[static_cast<std::size_t>(i)];
            }
            if (nb == 0) for (int i = 0; i < tempoCount_; ++i) bpms[static_cast<std::size_t>(nb++)] = tempoBpm_[static_cast<std::size_t>(i)];
            std::nth_element(bpms.begin(), bpms.begin() + nb / 2, bpms.begin() + nb);
            std::nth_element(confs.begin(), confs.begin() + nc / 2, confs.begin() + nc);
            const double folded = foldBpm(static_cast<double>(bpms[static_cast<std::size_t>(nb / 2)]));
            m.tempoConfidence = confs[static_cast<std::size_t>(nc / 2)];
            m.trackedBpm = static_cast<float>(folded);

            GridFit best;
            if (folded > 0.0) {
                const double candidates[5] = {folded, folded * 2.0, folded * 0.5, folded * (2.0 / 3.0), folded * 1.5};
                for (const double cand : candidates) {
                    if (cand < 60.0 || cand >= 170.0) continue;
                    for (int sub = 1; sub <= 4; ++sub) {
                        GridFit g = fitGrid(60.0 / cand, sub);
                        // Gentle prior towards common jam tempi settles genuine ties (e.g. swung 90 vs dotted 135).
                        g.score += 0.02f * static_cast<float>(std::abs(std::log2(60.0 / g.beatSeconds / 105.0)));
                        if (g.valid && g.score < best.score) best = g;
                    }
                }
            }
            if (best.valid) {
                const float regularity = std::clamp(1.0f - best.normalized / 0.289f, 0.0f, 1.0f);
                const double fitBpm = 60.0 / best.beatSeconds;
                m.regularity = regularity;
                m.timingSdMs = best.sdMs;
                m.subdivision = best.sub;
                m.looseness = std::clamp(0.6f * std::clamp((best.sdMs - 12.0f) / 28.0f, 0.0f, 1.0f) + 0.4f * (1.0f - regularity), 0.0f, 1.0f);
                m.bpm = static_cast<float>(regularity >= 0.6f ? fitBpm : folded);
                r.rhythmValid = m.bpm >= 40.0f && m.bpm <= 240.0f;
                m.swing = swingRatio(60.0 / static_cast<double>(m.bpm));
                const double span = onsetTimes_[static_cast<std::size_t>(onsetCount_ - 1)] - onsetTimes_[0];
                m.onsetDensity = span > 0.5 ? static_cast<float>((onsetCount_ - 1) / span) : 0.0f;
                m.transientStrength = static_cast<float>(strengthSum_ / std::max(1, onsetCount_));
                m.regularity = regularity;
            }
        }
        if (activeChunks_ > 0) {
            const double low = bandLowSum_, tot = bandLowSum_ + bandMidSum_ + bandHighSum_;
            m.lowShare = tot > 1.0e-9 ? static_cast<float>(low / tot) : 0.0f;
            m.intensity = static_cast<float>(intensitySum_ / activeChunks_);
        }

        // --- status ---
        const bool someSignal = act >= 0.1f || onsetCount_ >= 1;
        if (!r.levelsValid && !r.rhythmValid) {
            r.status = (hitCap_ && someSignal) ? AutoSetupStatus::TimedOut : AutoSetupStatus::NotEnoughSignal;
            if (r.status == AutoSetupStatus::TimedOut)
                addLine(r, "I only heard sparse playing in %.0f seconds, so I changed nothing. Try again and keep playing.", static_cast<double>(durationCap_));
            else addLine(r, "I could not hear enough playing, so I changed nothing. Check your input and try again.");
            result_ = r;
            return;
        }
        r.status = AutoSetupStatus::Ok;
        auto& p = r.proposal;

        if (r.levelsValid) {
            float gain = sane(-9.0f - m.peakDb, -12.0f, 24.0f, 0.0f);
            const bool needAuto = (-9.0f - m.peakDb) > 24.0f;
            if (m.clipping) {
                gain = std::min(gain, -6.0f);
                addLine(r, "Input gain set to %+.0f dB because your signal reached full scale and was clipping.", static_cast<double>(gain));
                addLine(r, "Warning: also turn down your guitar or interface input, clipping before the plug-in cannot be fixed here.");
            } else {
                addLine(r, "Input gain set to %+.0f dB because your loudest peak was %.0f dBFS (target -9 dBFS).",
                        static_cast<double>(gain), static_cast<double>(m.peakDb));
            }
            p.inputGainDb = {true, gain};
            p.autoGain = {true, needAuto};
            if (needAuto) addLine(r, "Auto Gain turned on because your signal is very quiet and needs more than +24 dB.");

            const float noisePost = noise + gain, loudPost = m.loudRmsDb + gain;
            if (noisePost < -72.0f) {
                p.gateThresholdDb = {true, -90.0f};
                addLine(r, "Gate left off because the noise floor is very low (%.0f dB).", static_cast<double>(noise));
            } else {
                const float ceiling = std::max(-80.0f, std::min(-30.0f, loudPost - 20.0f));
                const float gate = std::clamp(noisePost + 12.0f, -80.0f, std::min(-20.0f, ceiling));
                p.gateThresholdDb = {true, gate};
                addLine(r, "Gate set to %.0f dB because noise floor measured %.0f dB (%+.0f dB gain applied).",
                        static_cast<double>(gate), static_cast<double>(noise), static_cast<double>(gain));
            }
            if (m.intensity > 0.0f) {
                const float inten = std::clamp(0.25f + 0.60f * m.intensity, 0.25f, 0.85f);
                p.intensity = {true, inten};
                addLine(r, "Intensity %.0f%% to match how hard you play.", static_cast<double>(inten * 100.0f));
            }
        } else {
            addLine(r, "Not enough playing to judge levels (%.1f s heard), so gain and gate are unchanged.", static_cast<double>(act));
        }

        if (r.rhythmValid) {
            const float L = m.looseness;
            const bool steady = L < 0.35f && m.regularity >= 0.5f && m.tempoConfidence >= 0.4f;
            const float follow = 1.0f - 0.45f * L;
            const float inertia = 0.35f + 0.50f * L;
            const float conf = std::max(0.0f, 0.60f - 0.35f * L - (m.tempoConfidence < 0.4f ? 0.10f : 0.0f));
            if (steady) {
                p.timingMode = {true, LeadershipMode::Hybrid};
                p.leadership = {true, 0.70f};
                addLine(r, "Timing Mode: Hybrid (70%%) because your timing is steady (about %.0f ms off the beat grid).", static_cast<double>(m.timingSdMs));
            } else {
                p.timingMode = {true, LeadershipMode::DrummerLeads};
                addLine(r, "Timing Mode: Drummer Leads because your timing varies by about %.0f ms, so the drummer keeps the pulse steady.",
                        static_cast<double>(m.timingSdMs));
            }
            p.followStrength = {true, follow};
            p.tempoInertia = {true, inertia};
            p.confidenceSensitivity = {true, conf};
            addLine(r, "Follow %.0f%%, Inertia %.0f%%, Confidence %.0f%%: %s.", static_cast<double>(follow * 100.0f),
                    static_cast<double>(inertia * 100.0f), static_cast<double>(conf * 100.0f),
                    L < 0.35f ? "tight playing, quick to follow" : "looser playing, slower and less strict");
            p.bpm = {true, sane(m.bpm, 40.0f, 240.0f, 120.0f)};
            addLine(r, "Base tempo %.1f BPM, the median of your tracked tempo.", static_cast<double>(m.bpm));
            const AutoStyleChoice sc = chooseAutoStyle(m.bpm, m.onsetDensity, m.lowShare, m.transientStrength, m.swing);
            p.jamStyle = {true, sc.best};
            addLine(r, "Jam Style: %s from %.0f BPM, %.1f notes/s, %.0f%% low end (next best: %s).",
                    kJamStyleNames[static_cast<std::size_t>(autosetup_detail::styleIndexSafe(sc.best))], static_cast<double>(m.bpm),
                    static_cast<double>(m.onsetDensity), static_cast<double>(m.lowShare * 100.0f),
                    kJamStyleNames[static_cast<std::size_t>(autosetup_detail::styleIndexSafe(sc.second))]);
        } else {
            addLine(r, "Not enough clear rhythm (%d notes heard) to suggest timing, tempo or style, so those are unchanged.", onsetCount_);
        }
        if (badSamples_ > 0) addLine(r, "Warning: %d invalid input samples were ignored.", badSamples_);
        result_ = r;
    }

    // Mean longer/(sum) over consecutive interval pairs that add up to one beat; -1 when fewer than 4 pairs.
    [[nodiscard]] float swingRatio(double beat) const noexcept {
        double sum = 0.0;
        int pairs = 0;
        for (int i = 0; i + 2 < onsetCount_; ++i) {
            const double d1 = onsetTimes_[static_cast<std::size_t>(i + 1)] - onsetTimes_[static_cast<std::size_t>(i)];
            const double d2 = onsetTimes_[static_cast<std::size_t>(i + 2)] - onsetTimes_[static_cast<std::size_t>(i + 1)];
            const double both = d1 + d2;
            if (std::abs(both - beat) > 0.12 * beat || d1 < 0.2 * beat || d2 < 0.2 * beat) continue;
            sum += std::max(d1, d2) / both;
            ++pairs;
        }
        if (pairs < 4) return -1.0f;
        return std::clamp(static_cast<float>((sum / pairs - 0.5) / 0.17), 0.0f, 1.0f);
    }

    double sampleRate_{48000.0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> runState_{encode(0, AutoSetupState::Idle)};
    std::atomic<std::uint64_t> cancelGeneration_{0};
    std::uint64_t audioGeneration_{0}; // audio thread only
    std::atomic<AutoSetupPrompt> prompt_{AutoSetupPrompt::None};
    std::atomic<float> progress_{0.0f};
    std::atomic<float> peakDb_{-100.0f};
    std::atomic<float> duration_{10.0f};

    // Audio-thread-owned run data (reset in beginRun).
    AdaptiveOnsetDetector detector_{};
    PerformanceAnalyzer perf_{};
    TempoTracker tempo_{};
    std::array<float, ChunkSamples> staging_{};
    int staged_{0};
    long long chunks_{0};
    float durationNominal_{10.0f}, durationCap_{16.0f};
    int windowSamples_{480}, windowFill_{0}, windowIndex_{0}, tempoEveryChunks_{47};
    double windowEnergy_{0.0};
    std::array<float, kMaxLead> lead_{};
    int leadCount_{0};
    bool haveLead_{false};
    float leadMedianDb_{0.0f}, activeThresholdDb_{-50.0f};
    std::array<int, kHistBins> hist_{};
    long long windows_{0}, activeWindows_{0};
    bool lastWindowActive_{false};
    float peak_{0.0f};
    int clipSamples_{0}, badSamples_{0};
    std::array<double, kMaxOnsets> onsetTimes_{};
    std::array<float, kMaxOnsets> onsetStrengths_{};
    int onsetCount_{0};
    double strengthSum_{0.0};
    std::array<float, kMaxTempoSamples> tempoBpm_{}, tempoConf_{};
    int tempoCount_{0};
    long long activeChunks_{0};
    double bandLowSum_{0.0}, bandMidSum_{0.0}, bandHighSum_{0.0}, intensitySum_{0.0};
    bool hitCap_{false};

    AutoSetupResult result_{};
};

} // namespace robodrummer
