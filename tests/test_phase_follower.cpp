#include "analysis/PhaseFollower.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    PhaseFollower follower;
    RhythmState guitar;
    guitar.locked = true;
    guitar.beatPhase = 0.12;
    guitar.tempoConfidence = 0.9f;
    guitar.beatConfidence = 0.9f;

    auto result = follower.update(0.08, guitar, 120.0, 1.0f, FollowResponse::Balanced, 48000.0);
    assert(result.correctionSamples > 0);
    assert(result.correctionSamples <= 192); // 4 ms at 48 kHz
    assert(!result.hardResyncRecommended);

    guitar.beatPhase = 0.82;
    result = follower.update(0.10, guitar, 120.0, 1.0f, FollowResponse::Balanced, 48000.0);
    assert(result.correctionSamples < 0); // shortest wrapped route is backwards
    assert(!result.hardResyncRecommended);

    guitar.beatPhase = 0.48;
    result = follower.update(0.10, guitar, 120.0, 1.0f, FollowResponse::Balanced, 48000.0);
    assert(result.correctionSamples == 0);
    assert(result.hardResyncRecommended);

    guitar.locked = false;
    result = follower.update(0.10, guitar, 120.0, 1.0f, FollowResponse::Balanced, 48000.0);
    assert(result.correctionSamples == 0);
    assert(!result.hardResyncRecommended);
}
