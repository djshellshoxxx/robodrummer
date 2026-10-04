#include "analysis/ResyncPlanner.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    ResyncPlanner planner;
    planner.reset();

    RhythmState rhythm;
    rhythm.locked = true;
    rhythm.tempoConfidence = 0.90f;
    rhythm.beatConfidence = 0.90f;
    rhythm.downbeatConfidence = 0.20f;
    rhythm.beatInBar = 3;
    rhythm.beatPhase = 0.02;

    // Large phase errors should not cause an immediate arbitrary reset.
    auto action = planner.update(true, rhythm, 0.10);
    assert(action == ResyncAction::Wait);

    // Repeated disagreement with a reliable downbeat should schedule a musical reset.
    rhythm.downbeatConfidence = 0.80f;
    rhythm.beatInBar = 1;
    for (int i = 0; i < 5; ++i)
        action = planner.update(true, rhythm, 0.10);
    assert(action == ResyncAction::BreakAndRealign);

    // Once recovered, the planner clears persistence.
    action = planner.update(false, rhythm, 0.10);
    assert(action == ResyncAction::None);

    // Low-confidence tracking must never force a hard reset.
    planner.reset();
    rhythm.locked = false;
    rhythm.downbeatConfidence = 0.95f;
    for (int i = 0; i < 20; ++i)
        action = planner.update(true, rhythm, 0.10);
    assert(action == ResyncAction::Wait);
}
