#pragma once
#include <algorithm>
#include <cmath>

namespace robodrummer {
struct ClockSnapshot {
    double bpm{120.0};
    int numerator{4};
    int denominator{4};
    int beat{1};
    long long bar{1};
    double sampleInBeat{0.0};
};

class MusicalClock {
public:
    void setTempo(double bpm) noexcept { bpm_ = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 20.0, 400.0); }
    void setMeter(int numerator, int denominator) noexcept {
        numerator_ = std::max(1, numerator);
        denominator_ = (denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16) ? denominator : 4;
        beat_ = std::clamp(beat_, 1, numerator_);
    }
    [[nodiscard]] double samplesPerBeat(double sampleRate) const noexcept {
        if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)) return 0.0;
        return sampleRate * 60.0 / bpm_ * (4.0 / static_cast<double>(denominator_));
    }
    void reset() noexcept { beat_ = 1; bar_ = 1; sampleInBeat_ = 0.0; }
    void advance(int samples, double sampleRate) noexcept {
        if (samples <= 0) return;
        const double spb = samplesPerBeat(sampleRate);
        if (spb <= 0.0) return;
        sampleInBeat_ += static_cast<double>(samples);
        while (sampleInBeat_ >= spb) {
            sampleInBeat_ -= spb;
            if (++beat_ > numerator_) { beat_ = 1; ++bar_; }
        }
    }
    [[nodiscard]] ClockSnapshot snapshot() const noexcept { return {bpm_, numerator_, denominator_, beat_, bar_, sampleInBeat_}; }
private:
    double bpm_{120.0};
    int numerator_{4};
    int denominator_{4};
    int beat_{1};
    long long bar_{1};
    double sampleInBeat_{0.0};
};
}
