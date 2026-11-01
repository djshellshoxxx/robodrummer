// Offline trace adapter: production InputConditioner -> LiveRhythmAnalyzer.
// Metadata/manifest/metric ownership stays with check_fixtures.py.
#include "analysis/InputConditioner.h"
#include "analysis/RhythmAnalyzer.h"
#include "../tests/support/WavReader.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
template <typename T> T parse(std::string_view text) {
    T value{};
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    if (!(input >> std::noskipws >> value) || input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Invalid numeric argument");
    return value;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 5 || (argc - 3) % 2 != 0 || argc > 515)
            throw std::runtime_error("usage: fixture_runner WAV BLOCK_SIZE START_SECONDS METER [START_SECONDS METER ...]");
        auto audio = robodrummer::fixtures::readPcmWav(argv[1]);
        const int blockSize = parse<int>(argv[2]);
        if (blockSize != 64 && blockSize != 512 && blockSize != 2048) throw std::runtime_error("Expected block size 64, 512 or 2048");
        struct Meter { double seconds; int numerator; };
        std::vector<Meter> meters;
        for (int i = 3; i < argc; i += 2) {
            Meter meter{parse<double>(argv[i]), parse<int>(argv[i + 1])};
            if (!std::isfinite(meter.seconds) || meter.seconds < 0 || meter.seconds >= static_cast<double>(audio.mono.size()) / audio.sampleRate || meter.numerator < 2 || meter.numerator > 12 ||
                (!meters.empty() && meter.seconds <= meters.back().seconds)) throw std::runtime_error("Invalid meter segment");
            meters.push_back(meter);
        }
        if (meters.front().seconds != 0) throw std::runtime_error("Meter segments must start at zero");
        robodrummer::InputConditioner conditioner;
        robodrummer::LiveRhythmAnalyzer analyzer;
        conditioner.prepare(audio.sampleRate); conditioner.setGateEnabled(false);
        analyzer.prepare(audio.sampleRate);
        std::vector<float> conditioned(static_cast<std::size_t>(blockSize));
        std::size_t segment = 0;
        double previousPrediction = -1;
        std::cout << "time_s,bpm,locked,confidence,predicted_beat_s\n" << std::setprecision(12);
        for (std::size_t start = 0; start < audio.mono.size();) {
            const double startSeconds = static_cast<double>(start) / audio.sampleRate;
            while (segment + 1 < meters.size() && std::llround(meters[segment + 1].seconds * audio.sampleRate) <= static_cast<long long>(start)) ++segment;
            analyzer.setMeterNumerator(meters[segment].numerator);
            auto count = std::min<std::size_t>(blockSize, audio.mono.size() - start);
            if (segment + 1 < meters.size()) {
                const auto boundary = static_cast<std::size_t>(std::llround(meters[segment + 1].seconds * audio.sampleRate));
                if (boundary > start) count = std::min(count, boundary - start);
            }
            conditioner.process(audio.mono.data() + start, nullptr, conditioned.data(), static_cast<int>(count));
            analyzer.processBlock(conditioned.data(), static_cast<int>(count));
            start += count;
            const double endSeconds = static_cast<double>(start) / audio.sampleRate;
            const auto state = analyzer.state();
            if (!std::isfinite(state.tempoBpm) || !std::isfinite(state.tempoConfidence) || !std::isfinite(state.predictedNextBeatSeconds))
                throw std::runtime_error("Nonfinite tracker output");
            std::cout << endSeconds << ',' << state.tempoBpm << ',' << (state.locked ? 1 : 0) << ',' << state.tempoConfidence << ',';
            if (previousPrediction >= startSeconds && previousPrediction < endSeconds) std::cout << previousPrediction;
            std::cout << '\n';
            previousPrediction = state.locked ? state.predictedNextBeatSeconds : -1;
        }
        return std::cout.good() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Fixture runner: " << error.what() << '\n';
        return 1;
    }
}
