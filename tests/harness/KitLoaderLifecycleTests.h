// Controlled real-JUCE cancellation characterization and production join checks.
#pragma once
#include "audio/KitLoaderLifecycle.h"
#include <juce_core/juce_core.h>
#include <atomic>

namespace robodrummer::tests {

template <class Check>
void testKitLoaderLifecycle(Check&& check) {
    {
        juce::ThreadPool pool(1);
        juce::WaitableEvent started, release;
        std::atomic<bool> completed{}, published{};
        pool.addJob([&] {
            started.signal();
            release.wait(-1);
            // Characterize the old uncancellable lambda: a cancellation request
            // does not stop its body, and completion can still write the mailbox.
            published.store(true, std::memory_order_release);
            completed.store(true, std::memory_order_release);
        });
        const bool began = started.wait(5000);
        check(began, "controlled loader began");
        if (began) {
            const bool stopped = pool.removeAllJobs(true, 0);
            check(!stopped, "removeAllJobs timeout does not quiesce a running decode");
            check(!completed.load(std::memory_order_acquire), "decode remains active after timeout");
        }
        release.signal();
        check(pool.removeAllJobs(true, -1), "characterization decode eventually joins");
        check(published.load(std::memory_order_acquire), "timed-out uncancellable lambda can still publish");
    }
    {
        juce::ThreadPool pool(1);
        juce::WaitableEvent started, release;
        std::atomic<bool> cancellation{}, completed{}, published{};
        pool.addJob([&] {
            started.signal();
            release.wait(-1); // models an individual uncancellable filesystem read
            if (!cancellation.load(std::memory_order_acquire)) published.store(true, std::memory_order_release);
            completed.store(true, std::memory_order_release);
        });
        const bool began = started.wait(5000);
        check(began, "join-test loader began");
        struct ObservedPool {
            juce::ThreadPool& pool;
            juce::WaitableEvent& release;
            int calls{};
            bool removeAllJobs(bool interrupt, int timeout) {
                ++calls;
                if (calls == 2) release.signal(); // release only after fallback join starts
                return pool.removeAllJobs(interrupt, timeout);
            }
        } observed{pool, release};
        if (began) {
            const auto result = finishKitLoaderJobs(observed, cancellation, 0);
            check(result == KitLoaderStopResult::WaitedForRunningDecode, "production helper checks timeout and joins");
            check(observed.calls == 2, "production helper retries with an indefinite non-audio join");
            check(completed.load(std::memory_order_acquire), "owner can reset only after decode completion");
            check(!published.load(std::memory_order_acquire), "cooperative boundary discards cancelled completion");
            check(pool.getNumJobs() == 0, "no job can retain owner access after join returns");
        } else {
            release.signal();
            finishKitLoaderJobs(pool, cancellation, -1);
        }
        cancellation.store(false, std::memory_order_release); // prepare may now enable a fresh producer
        const auto immediate = finishKitLoaderJobs(pool, cancellation, 0);
        check(immediate == KitLoaderStopResult::WithinGracePeriod, "already-quiescent release is immediate");
    }
}

} // namespace robodrummer::tests
