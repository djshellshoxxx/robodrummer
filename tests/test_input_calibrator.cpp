#include "analysis/InputCalibrator.h"
#include <cassert>
#include <cmath>
#include <vector>

static std::vector<float> plucks(double sr, double seconds, double bpm, float amp) {
    std::vector<float> v(static_cast<std::size_t>(sr * seconds), 0.0f);
    const std::size_t step = static_cast<std::size_t>(sr * 60.0 / bpm);
    for (std::size_t s = 0; s < v.size(); s += step)
        for (std::size_t i = 0; i < 6000 && s + i < v.size(); ++i)
            v[s + i] = amp * std::exp(-static_cast<float>(i) / 1500.0f) * std::sin(2.0f * 3.14159265f * 196.0f * static_cast<float>(i) / static_cast<float>(sr));
    return v;
}

int main() {
    using namespace robodrummer;
    const double sr = 48000.0;
    InputCalibrator c;
    c.prepare(sr);
    assert(c.state() == CalibrationState::Idle);

    auto sig = plucks(sr, 6.0, 120.0, 0.1f);
    c.start(5.0f);
    assert(c.state() == CalibrationState::Running);
    for (std::size_t i = 0; i < sig.size(); i += 480) {
        c.process(sig.data() + i, static_cast<int>(std::min<std::size_t>(480, sig.size() - i)));
        if (i == 120000) assert(c.progress() > 0.3f && c.progress() < 0.7f);
    }
    assert(c.state() == CalibrationState::Done);
    assert(c.progress() == 1.0f);
    const auto& r = c.result();
    assert(r.recommendedTrimDb > 0.0f && r.recommendedTrimDb <= 24.0f);
    assert(r.eventsPerSecond > 1.0f && r.eventsPerSecond < 3.0f);
    assert(r.recommendedOnsetSensitivity >= 0.25f && r.recommendedOnsetSensitivity <= 4.0f);
    assert(r.recommendedGateThresholdDb < r.peakDb);

    std::vector<float> quiet(static_cast<std::size_t>(sr * 3), 1.0e-5f);
    c.start(2.0f);
    c.process(quiet.data(), static_cast<int>(quiet.size()));
    assert(c.state() == CalibrationState::FailedTooQuiet);
    return 0;
}
