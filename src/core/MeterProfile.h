#pragma once
#include <array>
#include <algorithm>

namespace robodrummer {

struct MeterProfile {
    static constexpr int MaxBeats = 12;
    static constexpr int MaxGroups = 4;

    bool valid{false};
    int numerator{4};
    int denominator{4};
    int groupCount{0};
    std::array<int, MaxGroups> groupStarts{};
    std::array<bool, MaxBeats> backbeat{};
    std::array<bool, MaxBeats> kickAnchor{};

    [[nodiscard]] static MeterProfile forMeter(int numerator, int denominator) noexcept {
        MeterProfile p;
        p.numerator = std::clamp(numerator, 2, MaxBeats);
        p.denominator = (denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16)
            ? denominator : 4;
        p.valid = true;

        auto addGroup = [&](int beat) {
            if (p.groupCount >= MaxGroups || beat < 0 || beat >= p.numerator) return;
            p.groupStarts[static_cast<std::size_t>(p.groupCount++)] = beat;
            p.kickAnchor[static_cast<std::size_t>(beat)] = true;
        };
        auto addBackbeat = [&](int beat) {
            if (beat >= 0 && beat < p.numerator)
                p.backbeat[static_cast<std::size_t>(beat)] = true;
        };

        if (p.numerator == 3 && p.denominator == 4) {
            addGroup(0);
            addBackbeat(1);
            addBackbeat(2);
        } else if (p.numerator == 6 && p.denominator == 8) {
            addGroup(0);
            addGroup(3);
            addBackbeat(3);
        } else if (p.numerator == 5) {
            // Default asymmetrical grouping: 3 + 2.
            addGroup(0);
            addGroup(3);
            addBackbeat(3);
        } else if (p.numerator == 7) {
            // Default asymmetrical grouping: 2 + 2 + 3.
            addGroup(0);
            addGroup(2);
            addGroup(4);
            addBackbeat(4);
        } else if (p.numerator == 4) {
            addGroup(0);
            addGroup(2);
            addBackbeat(1);
            addBackbeat(3);
        } else {
            // Generic fallback: accent the start and midpoint, with a conservative
            // backbeat on the midpoint when possible.
            addGroup(0);
            if (p.numerator >= 4)
                addGroup(p.numerator / 2);
            if (p.numerator >= 3)
                addBackbeat(p.numerator / 2);
        }

        return p;
    }
};

} // namespace robodrummer
