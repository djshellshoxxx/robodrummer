#include "analysis/RhythmAnalyzer.h"
#include <cassert>
#include <cmath>
#include <vector>

int main() {
    using namespace robodrummer;
    constexpr int sr = 1000;
    LiveRhythmAnalyzer analyzer;
    analyzer.prepare(sr);

    std::vector<float> block(100, 0.0f);
    int absolute = 0;
    for (int blockIndex = 0; blockIndex < 100; ++blockIndex) {
        std::fill(block.begin(), block.end(), 0.0f);
        for (int i = 0; i < static_cast<int>(block.size()); ++i) {
            const int s = absolute + i;
            const int phase = s % 500;
            if (phase >= 0 && phase < 18)
                block[static_cast<std::size_t>(i)] = 0.9f * static_cast<float>(std::exp(-0.20 * phase));
            const int off = (s - 250) % 2000;
            if (off >= 0 && off < 12)
                block[static_cast<std::size_t>(i)] += 0.22f * static_cast<float>(std::exp(-0.25 * off));
        }
        analyzer.processBlock(block.data(), static_cast<int>(block.size()));
        absolute += static_cast<int>(block.size());
    }

    const auto state = analyzer.state();
    assert(std::abs(state.tempoBpm - 120.0) < 2.5);
    assert(state.tempoConfidence > 0.25f);
    assert(state.beatConfidence > 0.20f);
    assert(state.locked);
    assert(state.predictedNextBeatSeconds > 10.0);
    assert(state.predictedNextBeatSeconds < 10.6);

    std::vector<float> silence(2000, 0.0f);
    analyzer.processBlock(silence.data(), static_cast<int>(silence.size()));
    const auto afterSilence = analyzer.state();
    assert(afterSilence.beatConfidence < state.beatConfidence);

    // Long silence releases the tracker lock.
    std::vector<float> longSilence(48000, 0.0f);
    for (int i = 0; i < 10; ++i)
        analyzer.processBlock(longSilence.data(), static_cast<int>(longSilence.size()));
    assert(!analyzer.state().locked);
}
