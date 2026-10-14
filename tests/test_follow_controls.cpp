// SPEC 12: Follow Strength, Tempo Inertia, Beat Correction Speed and Confidence Sensitivity.
#include "analysis/PhaseFollower.h"
#include "analysis/TimingAuthorityController.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;
    RhythmState g{};
    g.tempoBpm = 130.0; g.tempoConfidence = 0.9f; g.beatConfidence = 0.9f; g.locked = true;

    auto settle = [&](TimingAuthoritySettings s, double seconds) {
        TimingAuthorityController c; c.reset(100.0);
        TimingAuthorityState out{};
        for (int i = 0; i < static_cast<int>(seconds * 10); ++i) out = c.update(100.0, g, s, 0.1);
        return out;
    };
    TimingAuthoritySettings base;
    base.mode = LeadershipMode::GuitaristLeads; base.followRangeBpm = 40.0; base.response = FollowResponse::Balanced;

    // Defaults reproduce the preset's behaviour.
    const auto reference = settle(base, 3.0);

    // Follow strength scales the authority and therefore the tempo that is adopted.
    auto half = base; half.followStrength = 0.5f;
    const auto halfOut = settle(half, 30.0);
    const auto fullOut = settle(base, 30.0);
    assert(std::abs(fullOut.outputBpm - 127.0) < 1.0); // authority is 0.9 at 90% confidence
    assert(halfOut.outputBpm > 111.0 && halfOut.outputBpm < 119.0);
    auto none = base; none.followStrength = 0.0f;
    assert(std::abs(settle(none, 10.0).outputBpm - 100.0) < 0.01);

    // Tempo inertia: heavy inertia moves slower than nimble, in the same time.
    auto heavy = base; heavy.tempoInertia = 1.0f;
    auto nimble = base; nimble.tempoInertia = 0.0f;
    const auto heavyOut = settle(heavy, 3.0), nimbleOut = settle(nimble, 3.0);
    assert(heavyOut.outputBpm < reference.outputBpm - 3.0);
    assert(nimbleOut.outputBpm > reference.outputBpm + 5.0);

    // Confidence sensitivity: a weakly confident guitar is trusted more at high sensitivity.
    RhythmState weak = g; weak.tempoConfidence = 0.38f; weak.beatConfidence = 0.38f;
    auto strict = base; strict.confidenceSensitivity = 0.0f;
    auto trusting = base; trusting.confidenceSensitivity = 1.0f;
    auto settleWeak = [&](TimingAuthoritySettings s) { TimingAuthorityController c; c.reset(100.0); TimingAuthorityState o{}; for (int i = 0; i < 300; ++i) o = c.update(100.0, weak, s, 0.1); return o; };
    assert(settleWeak(trusting).effectiveGuitarAuthority > settleWeak(strict).effectiveGuitarAuthority + 0.15f);

    // Beat correction speed scales the phase correction.
    PhaseFollower follower;
    RhythmState phased = g; phased.beatPhase = 0.10;
    const auto slow = follower.update(0.0, phased, 120.0, 1.0f, FollowResponse::Balanced, 48000.0, 0.0f);
    const auto normal = follower.update(0.0, phased, 120.0, 1.0f, FollowResponse::Balanced, 48000.0, 0.5f);
    const auto fast = follower.update(0.0, phased, 120.0, 1.0f, FollowResponse::Balanced, 48000.0, 1.0f);
    assert(std::llabs(slow.correctionSamples) < std::llabs(normal.correctionSamples));
    assert(std::llabs(normal.correctionSamples) < std::llabs(fast.correctionSamples));
    const auto defaulted = follower.update(0.0, phased, 120.0, 1.0f, FollowResponse::Balanced, 48000.0);
    assert(defaulted.correctionSamples == normal.correctionSamples);
}
