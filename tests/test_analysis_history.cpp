#include "analysis/AnalysisHistory.h"
#include <cassert>
#include <thread>

int main() {
    using namespace robodrummer;
    SpscRing<int, 4> r;
    int v = 0;
    assert(!r.pop(v));
    for (int i = 0; i < 4; ++i) assert(r.push(i));
    assert(!r.push(99)); // full drops new
    for (int i = 0; i < 4; ++i) { assert(r.pop(v)); assert(v == i); }
    assert(!r.pop(v));
    assert(r.push(7) && r.pop(v) && v == 7); // wraparound

    AnalysisHistory h;
    h.pushOnset(1.0, 0.5f);
    OnsetMarker m;
    assert(h.onsets.pop(m) && m.seconds == 1.0 && m.strength == 0.5f);

    // confidence rate limit ~50 ms
    for (int i = 0; i < 100; ++i) h.pushConfidence(i * 0.01, 0.5f, 0.5f, 0.5f);
    assert(h.confidence.size() >= 18 && h.confidence.size() <= 21);

    h.pushTransition(2.0, AnalysisEvent::LockAcquired, 120);
    StateTransition t;
    assert(h.transitions.pop(t) && t.code == AnalysisEvent::LockAcquired && t.arg == 120);

    TempoCandidateSnapshot snap;
    assert(!h.readCandidates(snap));
    TempoTracker tt;
    for (int i = 0; i < 16; ++i) tt.addOnset(i * 0.5, 1.0f);
    h.publishCandidates(8.0, tt, 120.0);
    assert(h.readCandidates(snap));
    assert(snap.count >= 1 && snap.candidates[0].bpm > 100.0 && snap.activeBpm == 120.0);

    // Concurrent SPSC ordering
    SpscRing<int, 64> q;
    std::thread prod([&] { for (int i = 0; i < 100000;) if (q.push(i)) ++i; });
    int expect = 0;
    while (expect < 100000) { int x; if (q.pop(x)) { assert(x == expect); ++expect; } }
    prod.join();
    return 0;
}
