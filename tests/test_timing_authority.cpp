#include "analysis/TimingAuthorityController.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;
    TimingAuthorityController c;
    c.reset(120.0);
    RhythmState g{};
    g.tempoBpm = 132.0;
    g.tempoConfidence = 0.95f;
    g.beatConfidence = 0.90f;
    g.locked = true;

    TimingAuthoritySettings s;
    s.mode = LeadershipMode::DrummerLeads;
    auto out = c.update(120.0, g, s, 0.1);
    assert(std::abs(out.outputBpm - 120.0) < 0.01);
    assert(!out.usingGuitarTiming);

    c.reset(120.0);
    s.mode = LeadershipMode::GuitaristLeads;
    s.followRangeBpm = 20.0;
    s.response = FollowResponse::VeryResponsive;
    for (int i = 0; i < 10; ++i) out = c.update(120.0, g, s, 0.1);
    assert(out.outputBpm > 130.0 && out.outputBpm <= 132.1);
    assert(out.effectiveGuitarAuthority > 0.85f);

    c.reset(120.0);
    s.mode = LeadershipMode::Hybrid;
    s.leadership = 0.5f;
    s.response = FollowResponse::VeryResponsive;
    for (int i = 0; i < 10; ++i) out = c.update(120.0, g, s, 0.1);
    assert(out.outputBpm > 124.0 && out.outputBpm < 127.0);

    const double trusted = out.outputBpm;
    g.tempoBpm = 70.0;
    g.tempoConfidence = 0.15f;
    g.beatConfidence = 0.10f;
    g.locked = false;
    out = c.update(120.0, g, s, 0.1);
    assert(std::abs(out.outputBpm - trusted) < 2.5);
    assert(out.effectiveGuitarAuthority == 0.0f);

    c.reset(120.0);
    g.tempoBpm = 180.0;
    g.tempoConfidence = 0.95f;
    g.beatConfidence = 0.95f;
    g.locked = true;
    s.mode = LeadershipMode::GuitaristLeads;
    s.followRangeBpm = 10.0;
    for (int i = 0; i < 20; ++i) out = c.update(120.0, g, s, 0.1);
    assert(out.outputBpm <= 130.01);

    // Follow Strength scales guitar timing authority independently of leadership.
    c.reset(120.0);
    g.tempoBpm = 132.0;
    g.tempoConfidence = 0.95f;
    g.beatConfidence = 0.95f;
    g.locked = true;
    s.mode = LeadershipMode::GuitaristLeads;
    s.followRangeBpm = 20.0;
    s.response = FollowResponse::VeryResponsive;
    s.followStrength = 0.25f;
    for (int i = 0; i < 10; ++i) out = c.update(120.0, g, s, 0.1);
    assert(out.effectiveGuitarAuthority < 0.30f);

    // High inertia should move substantially less than low inertia over the same time.
    TimingAuthorityController lowInertia;
    TimingAuthorityController highInertia;
    lowInertia.reset(120.0);
    highInertia.reset(120.0);
    s.followStrength = 1.0f;
    s.tempoInertia = 0.0f;
    auto fast = lowInertia.update(120.0, g, s, 0.1);
    s.tempoInertia = 1.0f;
    auto slow = highInertia.update(120.0, g, s, 0.1);
    assert(fast.outputBpm - 120.0 > (slow.outputBpm - 120.0) * 2.0);

    // Higher confidence sensitivity rejects marginal evidence more strongly.
    g.tempoConfidence = 0.55f;
    g.beatConfidence = 0.55f;
    s.tempoInertia = 0.5f;
    s.confidenceSensitivity = 0.0f;
    c.reset(120.0);
    const auto permissive = c.update(120.0, g, s, 0.1);
    s.confidenceSensitivity = 1.0f;
    c.reset(120.0);
    const auto strict = c.update(120.0, g, s, 0.1);
    assert(permissive.effectiveGuitarAuthority > strict.effectiveGuitarAuthority);
}
