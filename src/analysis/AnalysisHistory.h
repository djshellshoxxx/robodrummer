// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "analysis/TempoTracker.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

// Lock-free single-producer/single-consumer ring. Capacity must be a power of two.
// When full, push() drops the NEW item and returns false (never blocks, never allocates).
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    bool push(const T& v) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false;
        items_[w & (Capacity - 1)] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        if (r == w) return false;
        out = items_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire));
    }
    static constexpr std::size_t capacity() noexcept { return Capacity; }
private:
    std::array<T, Capacity> items_{};
    std::atomic<std::uint64_t> write_{0};
    std::atomic<std::uint64_t> read_{0};
};

struct OnsetMarker { double seconds{0.0}; float strength{0.0f}; };
struct ConfidenceSample { double seconds{0.0}; float tempo{0.0f}; float beat{0.0f}; float downbeat{0.0f}; };
struct PhaseErrorSample { double seconds{0.0}; float errorCycles{0.0f}; };

enum class AnalysisEvent : std::uint8_t {
    None = 0, LockAcquired, LockLost, TempoJump, ResyncSoft, ResyncHard,
    SilenceEntered, SilenceExited, LeadershipChanged, CalibrationStarted, CalibrationFinished
};
struct StateTransition { double seconds{0.0}; AnalysisEvent code{AnalysisEvent::None}; std::int16_t arg{0}; };

struct TempoCandidateSnapshot {
    static constexpr std::size_t MaxCandidates = 5;
    double seconds{0.0};
    std::uint32_t count{0};
    std::array<TempoHypothesis, MaxCandidates> candidates{};
    double activeBpm{0.0};
};

// Audio thread pushes, GUI thread pops. Each stream is an independent SPSC ring.
class AnalysisHistory {
public:
    static constexpr double ConfidenceIntervalSeconds = 0.05;

    SpscRing<OnsetMarker, 256> onsets;
    SpscRing<ConfidenceSample, 512> confidence;
    SpscRing<PhaseErrorSample, 256> phaseError;
    SpscRing<StateTransition, 64> transitions;

    // --- audio thread ---
    void pushOnset(double seconds, float strength) noexcept { (void)onsets.push({seconds, strength}); }

    // Rate-limited to ~50 ms; call every block.
    void pushConfidence(double seconds, float tempo, float beat, float downbeat) noexcept {
        if (seconds < nextConfidenceAt_ - 10.0 || seconds >= nextConfidenceAt_) {
            if (confidence.push({seconds, tempo, beat, downbeat})) nextConfidenceAt_ = seconds + ConfidenceIntervalSeconds;
        }
    }
    void pushPhaseError(double seconds, float errorCycles) noexcept { (void)phaseError.push({seconds, errorCycles}); }
    void pushTransition(double seconds, AnalysisEvent code, int arg = 0) noexcept {
        (void)transitions.push({seconds, code, static_cast<std::int16_t>(arg)});
    }

    // Candidate snapshot: single-slot seqlock, latest value wins.
    void publishCandidates(double seconds, const TempoTracker& tracker, double activeBpm) noexcept {
        TempoCandidateSnapshot s;
        s.seconds = seconds; s.activeBpm = activeBpm;
        const auto top = tracker.topHypotheses();
        for (const auto& h : top) if (h.confidence > 0.0f && s.count < TempoCandidateSnapshot::MaxCandidates) s.candidates[s.count++] = h;
        const auto v = seq_.load(std::memory_order_relaxed);
        seq_.store(v + 1, std::memory_order_release); // odd = writing
        std::atomic_thread_fence(std::memory_order_release);
        snapshot_ = s;
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(v + 2, std::memory_order_release);
    }

    // --- GUI thread --- returns false if nothing published yet
    [[nodiscard]] bool readCandidates(TempoCandidateSnapshot& out) const noexcept {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto a = seq_.load(std::memory_order_acquire);
            if (a == 0) return false;
            if (a & 1u) continue;
            out = snapshot_;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_acquire) == a) return true;
        }
        return false;
    }

private:
    double nextConfidenceAt_{0.0};
    std::atomic<std::uint32_t> seq_{0};
    TempoCandidateSnapshot snapshot_{};
};

} // namespace robodrummer
