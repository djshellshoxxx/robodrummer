// Production ownership adapter: no resource may be lost or destroyed by adoption.
#include "audio/RealtimeOwnerHandoff.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>

namespace {
thread_local bool audioThread = false;
std::atomic<int> audioAllocations{};
std::atomic<int> audioFrees{};
std::array<std::atomic<int>, 20032> destructions{};
std::atomic<int> alive{};
struct Resource {
    explicit Resource(int identity) : id(identity) { ++alive; }
    ~Resource() { assert(!audioThread); ++destructions[static_cast<std::size_t>(id)]; --alive; }
    void clear() noexcept { cleared = true; }
    int id;
    bool cleared{};
};
using Handoff = robodrummer::RealtimeOwnerHandoff<Resource, 4>;

bool adopt(Handoff& handoff) {
    audioThread = true;
    const bool changed = handoff.tryAdoptPending();
    audioThread = false;
    return changed;
}

void capacityAndReplacement() {
    {
        Handoff handoff;
        handoff.resetWhenStopped(std::make_unique<Resource>(0));
        assert(handoff.current()->id == 0);
        assert(!adopt(handoff));
        for (int id = 1; id <= 4; ++id) {
            handoff.publish(std::make_unique<Resource>(id));
            assert(adopt(handoff));
            assert(handoff.current()->id == id);
        }
        // A fifth and subsequent completed kit load cannot orphan the active kit.
        // Replacement of an unconsumed pending kit remains a producer operation.
        for (int id = 5; id <= 9; ++id) {
            handoff.publish(std::make_unique<Resource>(id));
            assert(!adopt(handoff));
            assert(handoff.current()->id == 4);
            assert(alive == 6); // current, four retired, latest pending
        }
        for (int id = 5; id < 9; ++id) assert(destructions[id] == 1);
        assert(destructions[4] == 0 && destructions[9] == 0);
        handoff.drainRetired();
        for (int id = 0; id < 4; ++id) assert(destructions[id] == 1);
        assert(adopt(handoff));
        assert(handoff.current()->id == 9);
        handoff.current()->clear(); // releaseResources retains sampler ownership
        assert(handoff.current()->cleared && alive == 2);
        // prepareToPlay replacement is permitted only after all participants stop.
        handoff.publish(std::make_unique<Resource>(10));
        handoff.resetWhenStopped(std::make_unique<Resource>(11));
        assert(handoff.current()->id == 11 && alive == 1);
        assert(!adopt(handoff));
        handoff.publish(std::make_unique<Resource>(12));
        assert(adopt(handoff));
        handoff.publish(std::make_unique<Resource>(13));
        // Shutdown must reclaim current + pending + retired exactly once.
    }
    assert(alive == 0);
    for (int id = 0; id <= 13; ++id) assert(destructions[id] == 1);
}

void concurrentProducerAndReclaimer() {
    constexpr int first = 32;
    constexpr int count = 20000;
    Handoff handoff;
    std::atomic<bool> producerDone{}, audioDone{};
    std::thread producer([&] {
        for (int id = first; id < first + count; ++id)
            handoff.publish(std::make_unique<Resource>(id));
        producerDone.store(true, std::memory_order_release);
    });
    std::thread audio([&] {
        audioThread = true;
        while (!producerDone.load(std::memory_order_acquire))
            handoff.tryAdoptPending();
        // The consumer continues draining; the last pending kit must eventually land.
        while (handoff.current() == nullptr || handoff.current()->id != first + count - 1)
            handoff.tryAdoptPending();
        audioThread = false;
        audioDone.store(true, std::memory_order_release);
    });
    while (!audioDone.load(std::memory_order_acquire)) handoff.drainRetired();
    producer.join();
    audio.join();
    handoff.resetWhenStopped();
    assert(alive == 0);
    for (int id = first; id < first + count; ++id) assert(destructions[id] == 1);
}
}

void* operator new(std::size_t size) {
    if (audioThread) ++audioAllocations;
    if (auto* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { if (audioThread && p != nullptr) ++audioFrees; std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

int main() {
    capacityAndReplacement();
    concurrentProducerAndReclaimer();
    assert(audioAllocations == 0 && audioFrees == 0);
}
