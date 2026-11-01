#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cassert>

// Seed the reachable long-silence boundary without a multi-hour test or a
// production-only testing API. The detector itself still processes the input.
#define private public
#include "analysis/OnsetDetector.h"
#undef private

int main() {
    using namespace robodrummer;
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        AdaptiveOnsetDetector detector;
        detector.prepare(rate);
        std::array<float, 1024> audio{};
        std::array<OnsetEvent, 8> onsets{};

        detector.samplesSinceOnset_ = std::numeric_limits<int>::max();
        assert(detector.processBlock(audio.data(), 1, onsets.data(), onsets.size()) == 0);
        assert(detector.samplesSinceOnset_ >= detector.refractorySamples_);

        detector.reset();
        assert(detector.processBlock(audio.data(), static_cast<int>(audio.size()), onsets.data(), onsets.size()) == 0);
        assert(detector.samplesSinceOnset_ == detector.refractorySamples_);

        // An attack after arbitrarily long silence is still eligible immediately.
        for (std::size_t i = 0; i < audio.size(); ++i)
            audio[i] = 0.9f * static_cast<float>(std::exp(-static_cast<double>(i) / (rate * 0.005)));
        assert(detector.processBlock(audio.data(), static_cast<int>(audio.size()), onsets.data(), onsets.size()) > 0);
    }
}
