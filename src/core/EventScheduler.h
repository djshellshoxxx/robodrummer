#pragma once
#include "DrumEvent.h"
#include <array>
#include <cstddef>
namespace robodrummer {
template <std::size_t Capacity>
class EventScheduler {
public:
    bool push(DrumEvent event) noexcept {
        if (size_ >= Capacity) return false;
        event.sequence = nextSequence_++;
        std::size_t pos = size_;
        while (pos > 0 && events_[pos - 1].sampleOffset > event.sampleOffset) {
            events_[pos] = events_[pos - 1];
            --pos;
        }
        events_[pos] = event;
        ++size_;
        return true;
    }
    void clear() noexcept { size_ = 0; nextSequence_ = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const DrumEvent* begin() const noexcept { return events_.data(); }
    [[nodiscard]] const DrumEvent* end() const noexcept { return events_.data() + size_; }
private:
    std::array<DrumEvent, Capacity> events_{};
    std::size_t size_{0};
    std::uint32_t nextSequence_{0};
};
}
