// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <atomic>

namespace robodrummer {

enum class KitLoaderStopResult { WithinGracePeriod, WaitedForRunningDecode };

// Non-audio lifecycle boundary for JUCE-compatible thread pools. Request
// cooperative cancellation before waiting. A timed-out job still owns access
// to the processor: never reset its mailbox or destroy members until it exits.
// Local filesystem decoding may be uncancellable inside one read, so teardown
// can exceed the grace period rather than terminating a thread unsafely.
// No new jobs may be submitted until this returns (and reset runs, if needed).
template <class Pool>
KitLoaderStopResult finishKitLoaderJobs(Pool& pool, std::atomic<bool>& cancellation,
                                       int gracePeriodMs = 15000) {
    cancellation.store(true, std::memory_order_release);
    if (pool.removeAllJobs(true, gracePeriodMs)) return KitLoaderStopResult::WithinGracePeriod;
    // JUCE documents -1 as an indefinite wait. Check the result rather than
    // treating the initial timeout as successful cancellation.
    while (!pool.removeAllJobs(true, -1)) {}
    return KitLoaderStopResult::WaitedForRunningDecode;
}

} // namespace robodrummer
