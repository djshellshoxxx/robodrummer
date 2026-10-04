#include "analysis/DynamicsFollower.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;

    const auto fixed = DynamicsFollower::blend(0.4f, 0.9f, 1.0f, 1.0f, LeadershipMode::DrummerLeads);
    assert(std::abs(fixed - 0.4f) < 1.0e-6f);

    const auto hybrid = DynamicsFollower::blend(0.4f, 0.8f, 0.5f, 0.5f, LeadershipMode::Hybrid);
    assert(std::abs(hybrid - 0.5f) < 1.0e-6f);

    const auto guitar = DynamicsFollower::blend(0.2f, 0.9f, 1.0f, 1.0f, LeadershipMode::GuitaristLeads);
    assert(std::abs(guitar - 0.9f) < 1.0e-6f);

    const auto noAuthority = DynamicsFollower::blend(0.6f, 0.1f, 1.0f, 0.0f, LeadershipMode::Hybrid);
    assert(std::abs(noAuthority - 0.6f) < 1.0e-6f);
}
