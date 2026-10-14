// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace robodrummer {

// Derives tempo from incoming MIDI clock (24 pulses per quarter note) using sample timestamps.
class MidiClockTracker {
public:
    static constexpr std::size_t Window = 48;

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }

    void reset() noexcept {
        count_ = 0; head_ = 0; lastTickSample_ = -1; bpm_ = 0.0; running_ = false; startPending_ = false;
    }

    // sampleTime is a monotonically increasing sample counter (block start + offset).
    void tick(long long sampleTime) noexcept {
        if (lastTickSample_ >= 0) {
            const double interval = static_cast<double>(sampleTime - lastTickSample_) / sampleRate_;
            // 24 ppqn: 20-400 BPM corresponds to 0.00625-0.125 s per tick; ignore anything outside.
            if (interval > 0.004 && interval < 0.14) {
                intervals_[head_] = interval;
                head_ = (head_ + 1) % Window;
                count_ = std::min(count_ + 1, Window);
                double sum = 0.0;
                for (std::size_t i = 0; i < count_; ++i) sum += intervals_[i];
                const double average = sum / static_cast<double>(count_);
                if (count_ >= 6) bpm_ = 60.0 / (average * 24.0);
            } else if (interval >= 0.14) {
                count_ = 0; head_ = 0; bpm_ = 0.0;
            }
        }
        lastTickSample_ = sampleTime;
    }

    void start() noexcept { running_ = true; startPending_ = true; }
    void continueClock() noexcept { running_ = true; }
    void stop() noexcept { running_ = false; }

    // True once per MIDI Start message; the engine uses it to re-align the bar phase.
    [[nodiscard]] bool consumeStart() noexcept { const bool v = startPending_; startPending_ = false; return v; }
    [[nodiscard]] bool running() const noexcept { return running_; }

    // A clock is considered present while ticks keep arriving (within half a second of sampleTime).
    [[nodiscard]] bool valid(long long sampleTime) const noexcept {
        return bpm_ > 0.0 && lastTickSample_ >= 0 &&
               static_cast<double>(sampleTime - lastTickSample_) / sampleRate_ < 0.5;
    }
    [[nodiscard]] double bpm() const noexcept { return std::clamp(bpm_, 0.0, 400.0); }

private:
    double sampleRate_{48000.0};
    std::array<double, Window> intervals_{};
    std::size_t count_{0};
    std::size_t head_{0};
    long long lastTickSample_{-1};
    double bpm_{0.0};
    bool running_{false};
    bool startPending_{false};
};

} // namespace robodrummer
