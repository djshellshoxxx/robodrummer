#include "analysis/RhythmAnalyzer.h"
#include "analysis/TimingAuthorityController.h"
#include <cassert>
#include <cmath>

using namespace robodrummer;

static RhythmState locked(double bpm, double vel = 0.0) {
    RhythmState g;
    g.locked = true; g.tempoBpm = bpm; g.tempoVelocity = vel;
    g.tempoConfidence = 0.95f; g.beatConfidence = 0.95f;
    return g;
}

static double runTo(FollowResponse r, double guitarBpm, double seconds) {
    TimingAuthorityController c; c.reset(120.0);
    TimingAuthoritySettings s; s.mode = LeadershipMode::GuitaristLeads; s.response = r; s.followRangeBpm = 40.0;
    double out = 120.0;
    for (int i = 0; i < static_cast<int>(seconds * 20); ++i) out = c.update(120.0, locked(guitarBpm), s, 0.05).outputBpm;
    return out;
}

int main() {
    assert(followResponseProfile(FollowResponse::Balanced).smoothingRateBpmPerSecond == 6.0);
    assert(followResponseProfile(FollowResponse::Balanced).phaseGain == 0.11);
    // Slew ordering.
    double prev = 120.0;
    for (auto r : {FollowResponse::VeryStable, FollowResponse::Stable, FollowResponse::Balanced,
                   FollowResponse::Responsive, FollowResponse::VeryResponsive}) {
        const double v = runTo(r, 150.0, 2.0);
        assert(v >= prev);
        prev = v;
    }
    assert(std::abs(runTo(FollowResponse::Balanced, 150.0, 2.0) - 132.0) < 0.5);

    // Outlier rejection: a single jump is ignored by VeryStable, accepted if persistent.
    {
        TimingAuthorityController c; c.reset(120.0);
        TimingAuthoritySettings s; s.mode = LeadershipMode::GuitaristLeads; s.response = FollowResponse::VeryStable; s.followRangeBpm = 40.0;
        for (int i = 0; i < 40; ++i) (void)c.update(120.0, locked(120.0), s, 0.05);
        double o = c.update(120.0, locked(150.0), s, 0.05).outputBpm;
        assert(o < 121.0);
        for (int i = 0; i < 12; ++i) o = c.update(120.0, locked(150.0), s, 0.05).outputBpm;
        for (int i = 0; i < 400; ++i) o = c.update(120.0, locked(150.0), s, 0.05).outputBpm;
        assert(o > 140.0);
    }

    // Acceleration 118,119,121,123: velocity is positive and Responsive tracks faster than Balanced.
    {
        TimingAuthorityController c; c.reset(118.0);
        TimingAuthoritySettings s; s.mode = LeadershipMode::GuitaristLeads; s.response = FollowResponse::VeryResponsive; s.followRangeBpm = 40.0;
        for (double b : {118.0, 119.0, 121.0, 123.0}) (void)c.update(118.0, locked(b, 1.5), s, 0.5);
        assert(c.smoothedTempoVelocity() > 0.5);
    }
    return 0;
}
