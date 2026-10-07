#pragma once
#include "core/JamStyleProfile.h"
#include <algorithm>
#include <array>
#include <cstddef>

namespace robodrummer {

struct SectionDefinition {
    JamStyle style{JamStyle::Rock};
    int bars{4};
    float intensityTarget{0.5f};
    bool autoAdvance{true};
};

struct SectionAdvanceResult {
    bool sectionChanged{false};
    JamStyle enteredStyle{JamStyle::Rock};
    std::size_t enteredIndex{0};
};

template <std::size_t Capacity>
class SectionSequencer {
public:
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t currentSectionIndex() const noexcept { return currentIndex_; }
    [[nodiscard]] int barsIntoSection() const noexcept { return barsIntoSection_; }

    [[nodiscard]] const SectionDefinition& section(std::size_t index) const noexcept {
        static constexpr SectionDefinition fallback{};
        return index < count_ ? sections_[index] : fallback;
    }

    [[nodiscard]] bool set(std::size_t index, SectionDefinition section) noexcept {
        if (index >= count_) return false;
        section.bars = std::max(1, section.bars);
        section.intensityTarget = std::clamp(section.intensityTarget, 0.0f, 1.0f);
        sections_[index] = section;
        return true;
    }

    [[nodiscard]] bool add(SectionDefinition section) noexcept {
        if (count_ >= Capacity) return false;
        section.bars = std::max(1, section.bars);
        section.intensityTarget = std::clamp(section.intensityTarget, 0.0f, 1.0f);
        sections_[count_++] = section;
        return true;
    }

    void clear() noexcept {
        count_ = 0;
        currentIndex_ = 0;
        barsIntoSection_ = 0;
    }

    void reset() noexcept {
        currentIndex_ = 0;
        barsIntoSection_ = 0;
    }

    [[nodiscard]] const SectionDefinition& current() const noexcept {
        static constexpr SectionDefinition fallback{};
        return count_ == 0 ? fallback : sections_[currentIndex_];
    }

    [[nodiscard]] SectionAdvanceResult advanceBar() noexcept {
        SectionAdvanceResult result;
        result.enteredStyle = current().style;
        result.enteredIndex = currentIndex_;
        if (count_ == 0) return result;

        ++barsIntoSection_;
        const auto& section = sections_[currentIndex_];
        if (!section.autoAdvance || barsIntoSection_ < section.bars)
            return result;

        if (currentIndex_ + 1 < count_) {
            ++currentIndex_;
            barsIntoSection_ = 0;
            result.sectionChanged = true;
            result.enteredStyle = sections_[currentIndex_].style;
            result.enteredIndex = currentIndex_;
        }
        return result;
    }

    // Restores a playback position after the section list was rebuilt; clamps to the new list.
    void restorePosition(std::size_t index, int barsIntoSection) noexcept {
        if (count_ == 0) { reset(); return; }
        currentIndex_ = std::min(index, count_ - 1);
        barsIntoSection_ = std::clamp(barsIntoSection, 0, std::max(0, sections_[currentIndex_].bars - 1));
    }

    [[nodiscard]] bool next() noexcept {
        if (count_ == 0 || currentIndex_ + 1 >= count_) return false;
        ++currentIndex_;
        barsIntoSection_ = 0;
        return true;
    }

    [[nodiscard]] bool previous() noexcept {
        if (count_ == 0 || currentIndex_ == 0) return false;
        --currentIndex_;
        barsIntoSection_ = 0;
        return true;
    }

private:
    std::array<SectionDefinition, Capacity> sections_{};
    std::size_t count_{0};
    std::size_t currentIndex_{0};
    int barsIntoSection_{0};
};

} // namespace robodrummer
