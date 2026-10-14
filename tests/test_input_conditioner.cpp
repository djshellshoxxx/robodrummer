#include "analysis/CalibrationAnalyzer.h"
#include "analysis/InputConditioner.h"
#include <array>
#include <cassert>
#include <cmath>
#include <vector>
namespace {
std::vector<float> sine(double freq, double amp, int n, double sr = 48000.0) {
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(6.283185307179586 * freq * i / sr));
    return v;
}
float rmsTail(const std::vector<float>& v) {
    double s = 0; const std::size_t from = v.size() / 2;
    for (std::size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
    return static_cast<float>(std::sqrt(s / (v.size() - from)));
}
}
int main() {
    using namespace robodrummer;
    const int n = 48000;
    std::vector<float> out(n);
    InputConditioner c; c.prepare(48000.0);

    // Channel selection.
    auto left = sine(440.0, 0.5, n); std::vector<float> right(n, 0.0f);
    c.setMode(InputMode::LeftOnly); c.process(left.data(), right.data(), out.data(), n);
    const float leftRms = rmsTail(out);
    c.reset(); c.setMode(InputMode::RightOnly); c.process(left.data(), right.data(), out.data(), n);
    assert(rmsTail(out) < 1.0e-4f && leftRms > 0.3f);
    c.reset(); c.setMode(InputMode::AutoSum); c.process(left.data(), right.data(), out.data(), n);
    assert(std::abs(rmsTail(out) - leftRms * 0.5f) < 0.01f); // stereo summed to mono
    c.reset(); c.process(left.data(), nullptr, out.data(), n); // mono input is used as is
    assert(std::abs(rmsTail(out) - leftRms) < 0.01f);

    // DC offset and rumble are removed; the guitar band is preserved.
    std::vector<float> dc(n, 0.4f);
    c.reset(); c.setMode(InputMode::LeftOnly); c.process(dc.data(), nullptr, out.data(), n);
    assert(rmsTail(out) < 1.0e-3f);
    auto rumble = sine(20.0, 0.5, n);
    c.reset(); c.process(rumble.data(), nullptr, out.data(), n);
    assert(rmsTail(out) < 0.12f);
    auto guitar = sine(200.0, 0.5, n);
    c.reset(); c.process(guitar.data(), nullptr, out.data(), n);
    assert(rmsTail(out) > 0.30f);

    // Manual gain.
    c.reset(); c.setGainDb(6.0f); c.process(guitar.data(), nullptr, out.data(), n);
    assert(rmsTail(out) > 0.60f);
    c.setGainDb(0.0f);

    // Gate closes below the threshold, passes loud playing.
    auto quiet = sine(200.0, 0.002, n);
    c.reset(); c.setGateThresholdDb(-40.0f); c.process(quiet.data(), nullptr, out.data(), n);
    assert(rmsTail(out) < 1.0e-4f && !c.gateOpen());
    c.reset(); c.process(guitar.data(), nullptr, out.data(), n);
    assert(rmsTail(out) > 0.30f && c.gateOpen());
    c.setGateEnabled(false); c.reset(); c.process(quiet.data(), nullptr, out.data(), n);
    assert(rmsTail(out) > 0.001f);
    c.setGateEnabled(true); c.setGateThresholdDb(-90.0f);

    // Auto gain brings a quiet signal up (slowly) and never blows up.
    c.reset(); c.setAutoGain(true);
    auto lowGuitar = sine(200.0, 0.05, n * 20);
    std::vector<float> longOut(lowGuitar.size());
    c.process(lowGuitar.data(), nullptr, longOut.data(), static_cast<int>(lowGuitar.size()));
    assert(rmsTail(longOut) > 0.05f * 0.7071f * 1.5f && rmsTail(longOut) < 0.5f);
    c.setAutoGain(false);

    // Non-finite input is neutralised.
    std::vector<float> bad(512, std::nanf(""));
    c.reset(); c.process(bad.data(), nullptr, out.data(), 512);
    for (int i = 0; i < 512; ++i) assert(std::isfinite(out[static_cast<std::size_t>(i)]));

    // Calibration: noise floor + bursts of playing.
    CalibrationAnalyzer cal; cal.start(48000.0, 4.0);
    assert(cal.running());
    std::uint32_t rng = 1;
    bool done = false;
    int pos = 0;
    while (!done) {
        std::array<float, 480> block{};
        for (auto& s : block) {
            rng = rng * 1664525u + 1013904223u;
            const float noise = (static_cast<float>(rng >> 8) / 8388608.0f - 1.0f) * 0.001f;
            const int beat = (pos / 24000);
            const int inBeat = pos % 24000;
            const float hit = inBeat < 6000 ? 0.4f * std::exp(-inBeat / 1500.0f) * std::sin(inBeat * 0.05f) : 0.0f;
            s = noise + (beat >= 0 ? hit : 0.0f);
            ++pos;
        }
        done = cal.process(block.data(), 480);
    }
    const auto r = cal.result();
    assert(!cal.running() && r.valid);
    assert(r.noiseFloorDb < -55.0f);
    assert(r.loudRmsDb > r.noiseFloorDb + 20.0f);
    assert(r.onsetsPerSecond > 1.0f && r.onsetsPerSecond < 4.0f);
    assert(r.suggestedGateDb > r.noiseFloorDb && r.suggestedGateDb <= -30.0f);
    assert(r.peakDb > -12.0f && r.peakDb < 0.0f);
    assert(std::abs(cal.progress() - 1.0f) < 1.0e-6f);
}
