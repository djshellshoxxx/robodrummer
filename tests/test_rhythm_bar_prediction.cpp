#include "analysis/RhythmAnalyzer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct Hit { long long sample; float amplitude; };

class Fixture {
public:
    Fixture(int meter, int rate, int blockSize) : sampleRate(rate), blockSize_(blockSize), buffer_(blockSize), tail_(rate / 5) {
        analyzer.prepare(rate);
        analyzer.setMeterNumerator(meter);
        for (std::size_t i = 0; i < tail_.size(); ++i) {
            const double t = static_cast<double>(i) / rate;
            tail_[i] = static_cast<float>(std::exp(-t / 0.05) * std::sin(6.2831853 * 196.0 * t)
                + 0.3 * std::exp(-t / 0.004) * std::sin(6.2831853 * 1800.0 * t));
        }
    }

    void addBeats(double start, double period, int beats, int meter, float accent = 0.9f) {
        for (int beat = 0; beat < beats; ++beat)
            hits_.push_back({static_cast<long long>(std::llround((start + beat * period) * sampleRate)), beat % meter == 0 ? accent : 0.35f});
    }

    robodrummer::RhythmState advanceTo(double seconds, bool expectNoOnsets = false) {
        const auto target = static_cast<long long>(std::llround(seconds * sampleRate));
        while (cursor_ < target) {
            const int count = static_cast<int>(std::min<long long>(blockSize_, target - cursor_));
            for (int i = 0; i < count; ++i) {
                const auto sample = cursor_ + i;
                while (hit_ + 1 < hits_.size() && hits_[hit_ + 1].sample <= sample) ++hit_;
                const auto age = hits_.empty() ? -1LL : sample - hits_[hit_].sample;
                buffer_[static_cast<std::size_t>(i)] = age >= 0 && age < static_cast<long long>(tail_.size())
                    ? hits_[hit_].amplitude * tail_[static_cast<std::size_t>(age)] : 0.0f;
            }
            analyzer.processBlock(buffer_.data(), count);
            if (expectNoOnsets) assert(analyzer.lastBlockOnsetCount() == 0);
            cursor_ += count;
        }
        return analyzer.state();
    }

    int sampleRate;
    robodrummer::LiveRhythmAnalyzer analyzer;
private:
    int blockSize_;
    std::vector<float> buffer_, tail_;
    std::vector<Hit> hits_;
    std::size_t hit_{0};
    long long cursor_{0};
};

void checkRest(int meter, int rate, int blockSize) {
    Fixture fixture(meter, rate, blockSize);
    const int trainingBeats = meter * std::max(8, (64 + meter - 1) / meter);
    const double restStart = trainingBeats * 0.5;
    const double resume = restStart + meter; // Two full bars of rest at 120 BPM.
    fixture.addBeats(0.0, 0.5, trainingBeats + 1, meter);
    fixture.addBeats(resume, 0.5, meter * 8 + 3, meter);
    const auto start = fixture.advanceTo(restStart + 0.25);
    assert(start.locked && std::abs(start.tempoBpm - 120.0) < 1.0);
    // The 4/4 audit fixture establishes a trusted real downbeat. Other meters
    // test projection from their inferred origin, independently of acquisition quality.
    if (meter == 4) assert(start.beatInBar == 1 && start.downbeatConfidence > 0.35f);

    for (int beat = 1; beat < meter * 2; ++beat) {
        const auto state = fixture.advanceTo(restStart + beat * 0.5 + 0.25, true);
        const int expected = (start.beatInBar - 1 + beat) % meter + 1;
        if (state.beatInBar != expected)
            std::fprintf(stderr, "meter %d rate %d block %d rest beat %d: got %d expected %d\n",
                         meter, rate, blockSize, beat, state.beatInBar, expected);
        assert(state.beatInBar == expected);
        assert(state.downbeatConfidence == start.downbeatConfidence); // Prediction supplies no accent evidence.
    }
    assert(!fixture.analyzer.state().locked);
    const auto reacquired = fixture.advanceTo(resume + meter * 4.0 + 1.25);
    assert(reacquired.locked && std::abs(reacquired.tempoBpm - 120.0) < 1.0);
    assert(reacquired.beatInBar == (start.beatInBar - 1 + 2) % meter + 1);
}

void checkProjectionDoesNotObserve(int meter) {
    robodrummer::DownbeatTracker tracker;
    tracker.reset(meter);
    const int downbeatSlot = meter - 1;
    for (int beat = 0; beat < meter * 12; ++beat)
        tracker.observeBeatAt(beat, beat % meter == downbeatSlot ? 1.0f : 0.2f);
    const auto observed = tracker.state();
    for (long long beat = -meter * 2; beat < meter * 3; ++beat) {
        const auto predicted = tracker.stateAt(beat);
        const auto relative = observed.currentBeatInBar - 1 + beat - (meter * 12 - 1);
        const int expected = static_cast<int>((relative % meter + meter) % meter) + 1;
        assert(predicted.currentBeatInBar == expected);
        assert(predicted.nextBeatInBar == expected % meter + 1);
        assert(predicted.downbeatConfidence == observed.downbeatConfidence);
        assert(tracker.state().currentBeatInBar == observed.currentBeatInBar);
    }
}

void checkRestAfterTempoChange() {
    constexpr int meter = 4;
    Fixture fixture(meter, 12000, 512);
    fixture.addBeats(0.0, 0.5, 64, meter);
    fixture.addBeats(32.0, 0.6, 81, meter);
    const double restStart = 32.0 + 80 * 0.6;
    const auto start = fixture.advanceTo(restStart + 0.3);
    assert(start.locked && std::abs(start.tempoBpm - 100.0) < 1.0);
    for (int beat = 1; beat <= 8; ++beat) {
        const auto state = fixture.advanceTo(restStart + beat * 0.6 + 0.3, true);
        assert(state.beatInBar == (start.beatInBar - 1 + beat) % meter + 1);
        assert(state.downbeatConfidence == start.downbeatConfidence);
    }
}

void checkOriginAcquisition() {
    // The default origin must not retain a high confidence that was actually
    // earned by another slot. Every possible stable accent origin is learnable.
    for (int meter = 2; meter <= 12; ++meter) {
        for (int origin = 0; origin < meter; ++origin) {
            robodrummer::DownbeatTracker tracker;
            tracker.reset(meter);
            for (int beat = 0; beat < meter * 100; ++beat)
                tracker.observeBeatAt(beat, beat % meter == origin ? 1.0f : 0.2f);
            const auto state = tracker.stateAt(origin);
            if (state.currentBeatInBar != 1)
                std::fprintf(stderr, "origin acquisition meter %d origin %d: got beat %d confidence %.3f\n",
                             meter, origin, state.currentBeatInBar, state.downbeatConfidence);
            assert(state.currentBeatInBar == 1 && state.downbeatConfidence > 0.55f);
        }
    }
}

void checkLargeMeterWaveform(int meter) {
    Fixture fixture(meter, 12000, 512);
    const int beats = meter * 100;
    // Deliberately strong contrast clears the same fixed acquisition threshold
    // at every meter despite the onset detector's adaptive accent normalization.
    fixture.addBeats(0.0, 0.5, beats + 1, meter, 3.0f);
    const auto state = fixture.advanceTo(beats * 0.5 + 0.25);
    assert(state.locked && std::abs(state.tempoBpm - 120.0) < 1.0);
    if (state.beatInBar != 1 || state.downbeatConfidence <= 0.45f)
        std::fprintf(stderr, "waveform origin meter %d: got beat %d confidence %.3f\n",
                     meter, state.beatInBar, state.downbeatConfidence);
    assert(state.beatInBar == 1 && state.downbeatConfidence > 0.45f);
}

void checkMeterTransition() {
    Fixture fixture(4, 12000, 256);
    fixture.addBeats(0.0, 0.5, 68, 4);
    fixture.addBeats(34.0, 0.5, 7 * 16 + 1, 7, 3.0f);
    const auto before = fixture.advanceTo(33.75);
    assert(before.locked && before.meterNumerator == 4);
    fixture.analyzer.setMeterNumerator(7);
    assert(fixture.analyzer.state().downbeatConfidence == 0.0f);
    const auto after = fixture.advanceTo(34.0 + 7 * 8.0 + 0.25);
    assert(after.locked && after.meterNumerator == 7);
    assert(after.beatInBar == 1 && after.downbeatConfidence > 0.45f);
}
}

int main() {
    checkOriginAcquisition();
    for (int meter = 2; meter <= 12; ++meter) {
        checkRest(meter, 12000, 256);
        checkProjectionDoesNotObserve(meter);
    }
    for (int block : {64, 512, 2048}) {
        checkRest(4, 48000, block);
        checkRest(7, 48000, block);
    }
    checkRestAfterTempoChange();
    for (int meter : {10, 11, 12}) checkLargeMeterWaveform(meter);
    checkMeterTransition();
}
