// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

namespace robodrummer {

// Owns sampler replacements across one audio thread, non-audio publishers and
// one non-audio reclaimer. current() is an audio-thread-only borrowed pointer.
// Publishing/draining may delete; tryAdoptPending never allocates or destroys.
// Before resetWhenStopped or destruction, stop publishers, the audio consumer
// and the reclaimer. Lifecycle calls are not audio callback operations.
template <class Resource, std::size_t RetirementCapacity = 4>
class RealtimeOwnerHandoff {
public:
    static_assert(RetirementCapacity > 0);
    static_assert(std::atomic<Resource*>::is_always_lock_free);

    RealtimeOwnerHandoff() noexcept = default;
    RealtimeOwnerHandoff(const RealtimeOwnerHandoff&) = delete;
    RealtimeOwnerHandoff& operator=(const RealtimeOwnerHandoff&) = delete;
    ~RealtimeOwnerHandoff() { resetWhenStopped(); }

    Resource* current() const noexcept { return current_; }

    // Non-audio producer. Unconsumed results use latest-completed-load wins,
    // matching the processor's existing publication policy.
    void publish(std::unique_ptr<Resource> resource) noexcept {
        delete pending_.exchange(resource.release(), std::memory_order_acq_rel);
    }

    // Single audio consumer. If retirement is full, retain both current and
    // pending ownership and retry after the message thread reclaims a slot.
    bool tryAdoptPending() noexcept {
        if (pending_.load(std::memory_order_acquire) == nullptr) return false;
        std::atomic<Resource*>* vacancy = nullptr;
        if (current_ != nullptr) {
            for (auto& retired : retired_) {
                if (retired.load(std::memory_order_acquire) == nullptr) {
                    vacancy = &retired;
                    break;
                }
            }
            if (vacancy == nullptr) return false;
        }
        // Only this audio consumer fills retirement slots, so a vacant slot
        // cannot become occupied between the scan and publication below.
        auto* incoming = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (incoming == nullptr) return false;
        auto* previous = current_;
        current_ = incoming;
        if (previous != nullptr) vacancy->store(previous, std::memory_order_release);
        return true;
    }

    // Single non-audio consumer. Once published here, the audio thread no
    // longer accesses the retired object.
    void drainRetired() noexcept {
        for (auto& retired : retired_) delete retired.exchange(nullptr, std::memory_order_acq_rel);
    }

    void resetWhenStopped(std::unique_ptr<Resource> replacement = {}) noexcept {
        delete current_;
        current_ = replacement.release();
        delete pending_.exchange(nullptr, std::memory_order_acq_rel);
        drainRetired();
    }

private:
    Resource* current_{};
    std::atomic<Resource*> pending_{};
    std::array<std::atomic<Resource*>, RetirementCapacity> retired_{};
};

} // namespace robodrummer
