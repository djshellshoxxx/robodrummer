#include "core/PracticeMode.h"
#include "core/SectionSequencer.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

using namespace robodrummer;

namespace {

// Frozen ordinary-playback reference: the pre-seek implementation advanced one
// bar at a time, including continuing the counter in manual and final sections.
struct OrdinaryReference {
    std::array<SectionDefinition, 6> sections;
    std::size_t index{0};
    int bars{0};

    void advance() {
        ++bars;
        if (sections[index].autoAdvance && bars >= sections[index].bars && index + 1 < sections.size()) {
            ++index;
            bars = 0;
        }
    }
};

void testOrdinaryPlaybackReference() {
    for (int manualAt = -1; manualAt < 6; ++manualAt) {
        std::array<SectionDefinition, 6> sections;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            auto& section = sections[i];
            section.style = static_cast<JamStyle>(i);
            section.bars = static_cast<int>(i % 3) + 1;
            section.autoAdvance = static_cast<int>(i) != manualAt;
            section.timeFeel = static_cast<SectionTimeFeel>(i % 3);
            section.transition = static_cast<SectionTransition>(i % 4);
            section.intensityTarget = static_cast<float>(i) / 6.0f;
        }
        for (int before = 0; before < 24; ++before) {
            for (int distance = 0; distance < 65; ++distance) {
                SectionSequencer<6> seq;
                for (const auto& section : sections) assert(seq.add(section));
                OrdinaryReference reference{sections};
                for (int i = 0; i < before; ++i) {
                    reference.advance();
                    (void)seq.advanceBar();
                }
                const auto originalIndex = reference.index;
                for (int i = 0; i < distance; ++i) reference.advance();
                const auto result = seq.advanceBars(static_cast<std::uint64_t>(distance));
                assert(seq.currentSectionIndex() == reference.index);
                assert(seq.barsIntoSection() == reference.bars);
                assert(result.sectionChanged == (reference.index != originalIndex));
                assert(result.enteredIndex == reference.index);
                assert(result.enteredStyle == sections[reference.index].style);
                assert(seq.current().timeFeel == sections[reference.index].timeFeel);
                assert(seq.current().transition == sections[reference.index].transition);
                assert(seq.current().intensityTarget == sections[reference.index].intensityTarget);
            }
        }
    }
}

void testLargeDistancesAndHoldingSections() {
    constexpr auto enormous = std::numeric_limits<std::uint64_t>::max();
    constexpr auto maxBars = std::numeric_limits<int>::max();

    SectionSequencer<6> seq;
    assert(seq.add({JamStyle::Rock, 3, 0.5f, true}));
    assert(seq.add({JamStyle::Funk, 2, 0.7f, false}));
    assert(seq.add({JamStyle::Blues, 1, 0.4f, true}));
    auto result = seq.advanceBars(enormous);
    assert(result.sectionChanged && result.enteredIndex == 1);
    assert(seq.current().style == JamStyle::Funk);
    assert(seq.barsIntoSection() == maxBars);
    result = seq.advanceBars(enormous);
    assert(!result.sectionChanged && result.enteredIndex == 1);
    assert(seq.barsIntoSection() == maxBars);
    assert(seq.next());
    assert(seq.barsIntoSection() == 0);
    result = seq.advanceBars(enormous);
    assert(!result.sectionChanged && result.enteredIndex == 2);
    assert(seq.barsIntoSection() == maxBars);
    assert(seq.previous());
    assert(seq.currentSectionIndex() == 1 && seq.barsIntoSection() == 0);

    // Every section can itself have INT_MAX bars; the accumulated distance is
    // not an int, and a near-UINT64_MAX seek still visits at most 16 sections.
    SectionSequencer<16> longSong;
    for (int i = 0; i < 16; ++i) assert(longSong.add({JamStyle::Rock, maxBars, 0.5f, true}));
    result = longSong.advanceBars(static_cast<std::uint64_t>(maxBars) - 1);
    assert(!result.sectionChanged && longSong.barsIntoSection() == maxBars - 1);
    result = longSong.advanceBars(2);
    assert(result.sectionChanged && longSong.currentSectionIndex() == 1 && longSong.barsIntoSection() == 1);
    result = longSong.advanceBars(enormous);
    assert(result.sectionChanged && longSong.currentSectionIndex() == 15 && longSong.barsIntoSection() == maxBars);

    SectionSequencer<0> empty;
    assert(!empty.advanceBars(enormous).sectionChanged);
    assert(empty.barsIntoSection() == 0);
}

void testEditsAndPracticeRewind() {
    SectionSequencer<6> seq;
    SectionDefinition first{JamStyle::Rock, 8, 0.5f, true};
    assert(seq.add(first));
    SectionDefinition second{JamStyle::Funk, 4, 0.7f, true};
    second.timeFeel = SectionTimeFeel::HalfTime;
    second.transition = SectionTransition::Fill;
    assert(seq.add(second));
    assert(seq.add({JamStyle::Blues, 3, 0.4f, true}));
    (void)seq.advanceBars(7);
    first.bars = 2;
    assert(seq.set(0, first));
    auto result = seq.advanceBars(0);
    assert(!result.sectionChanged && seq.barsIntoSection() == 7);
    result = seq.advanceBars(2);
    assert(result.sectionChanged && seq.currentSectionIndex() == 1 && seq.barsIntoSection() == 1);
    const auto loop = sectionLoopRange(seq, 1, 2, 3);
    assert(loop.valid && loop.startBar == 2 && loop.lengthBars == 7);
    const auto savedIndex = seq.currentSectionIndex();
    const auto savedBars = seq.barsIntoSection();
    (void)seq.advanceBars(5);
    assert(seq.currentSectionIndex() == 2 && seq.barsIntoSection() == 2);
    // The practice adapter restores its snapshot before rebasing the host bar;
    // seek acceleration must not turn a practice rewind into a restart.
    seq.restorePosition(savedIndex, savedBars);
    assert(seq.currentSectionIndex() == 1 && seq.barsIntoSection() == 1);
    result = seq.advanceBars(1);
    assert(!result.sectionChanged && seq.barsIntoSection() == 2);
    assert(seq.current().timeFeel == SectionTimeFeel::HalfTime);
    assert(seq.current().transition == SectionTransition::Fill);
}

} // namespace

int main() {
    testOrdinaryPlaybackReference();
    testLargeDistancesAndHoldingSections();
    testEditsAndPracticeRewind();
}
