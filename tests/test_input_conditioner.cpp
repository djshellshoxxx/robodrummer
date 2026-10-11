#include "analysis/InputConditioner.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main() {
    using namespace robodrummer;
    const double sr = 48000.0;
    const int n = 48000;
    std::vector<float> l(n), r(n), out(n);
    for (int i = 0; i < n; ++i) {
        l[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 440.0 * i / sr) + 0.3f; // DC offset
        r[i] = 0.0f;
    }
    InputConditioner c;
    c.prepare(sr, 512);
    const float* ch[2] = {l.data(), r.data()};
    c.process(ch, 2, n, out.data());
    double mean = 0; for (int i = n / 2; i < n; ++i) mean += out[i]; mean /= n / 2;
    assert(std::abs(mean) < 0.02);
    assert(c.peakDb() > -9.0f && c.peakDb() < -4.0f);

    // Right mode on silent right channel -> silence
    c.reset(); c.setChannelMode(InputChannelMode::Right);
    c.process(ch, 2, n, out.data());
    for (int i = n / 2; i < n; ++i) assert(std::abs(out[i]) < 1e-4f);

    // Trim +6 dB doubles level
    c.reset(); c.setChannelMode(InputChannelMode::Left); c.setTrimDb(100.0f);
    assert(c.trimDb() == 24.0f);
    c.setTrimDb(0.0f);

    // Gate closes on quiet signal and opens on loud
    c.reset(); c.setGateEnabled(true); c.setGateThresholdDb(-40.0f); c.setGateHoldSeconds(0.0f);
    std::vector<float> quiet(n, 0.0f);
    for (int i = 0; i < n; ++i) quiet[i] = 0.002f * std::sin(2.0 * 3.14159265358979 * 440.0 * i / sr);
    const float* q[1] = {quiet.data()};
    c.process(q, 1, n, out.data());
    assert(!c.gateOpen());
    assert(std::abs(out[n - 1]) < 0.0005f);
    c.process(ch, 1, n, out.data());
    assert(c.gateOpen());

    // NaN/Inf sanitized
    c.reset(); c.setGateEnabled(false);
    l[10] = std::numeric_limits<float>::quiet_NaN();
    l[11] = std::numeric_limits<float>::infinity();
    c.process(ch, 2, n, out.data());
    for (int i = 0; i < n; ++i) assert(std::isfinite(out[i]));

    // Null / mono safety
    c.process(nullptr, 0, 64, out.data());
    c.process(ch, 1, 64, out.data());
    return 0;
}
