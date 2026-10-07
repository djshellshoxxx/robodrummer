#include "analysis/TempoMovement.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    TempoMovementClassifier classifier;
    classifier.reset(120.0);

    assert(classifier.update(120.2, 0.2, 0.1) == TempoMovement::Stable);
    assert(classifier.update(121.0, 0.2, 0.1) == TempoMovement::Jitter);

    classifier.reset(120.0);
    assert(classifier.update(120.4, 1.0, 0.5) == TempoMovement::Drift);
    assert(classifier.update(122.0, 4.0, 0.5) == TempoMovement::Accelerating);
    assert(classifier.update(120.0, -4.0, 0.5) == TempoMovement::Decelerating);

    classifier.reset(120.0);
    assert(classifier.update(144.0, 0.0, 0.1) == TempoMovement::AbruptShift);

    classifier.reset(120.0);
    assert(classifier.update(60.0, 0.0, 0.1) == TempoMovement::HalfTimeReinterpretation);

    classifier.reset(120.0);
    assert(classifier.update(240.0, 0.0, 0.1) == TempoMovement::DoubleTimeReinterpretation);
}
