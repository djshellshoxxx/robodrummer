#include "core/SectionSequencer.h"
#include <cassert>
#include <limits>

int main() {
    using namespace robodrummer;

    SectionSequencer<8> seq;
    assert(seq.empty());

    assert(seq.add({JamStyle::Rock, 2, 0.55f, true}));
    assert(seq.add({JamStyle::Funk, 1, 0.70f, true}));
    assert(seq.add({JamStyle::Blues, 4, 0.45f, false}));
    assert(seq.size() == 3);
    assert(seq.set(1, {JamStyle::Metal, 2, 0.80f, true}));
    assert(seq.section(1).style == JamStyle::Metal);
    assert(seq.section(1).bars == 2);
    assert(!seq.set(7, {JamStyle::Rock, 1, 0.5f, true}));
    assert(seq.set(1, {JamStyle::Funk, 1, 0.70f, true}));

    seq.reset();
    assert(seq.current().style == JamStyle::Rock);
    assert(seq.currentSectionIndex() == 0);

    auto step = seq.advanceBar();
    assert(!step.sectionChanged);
    assert(seq.currentSectionIndex() == 0);

    step = seq.advanceBar();
    assert(step.sectionChanged);
    assert(step.enteredStyle == JamStyle::Funk);
    assert(seq.currentSectionIndex() == 1);

    step = seq.advanceBar();
    assert(step.sectionChanged);
    assert(seq.current().style == JamStyle::Blues);

    // A non-auto-advance section should remain active.
    for (int i = 0; i < 8; ++i)
        step = seq.advanceBar();
    assert(seq.currentSectionIndex() == 2);

    // Manual navigation is always available for MIDI intervention.
    assert(seq.previous());
    assert(seq.current().style == JamStyle::Funk);
    assert(seq.next());
    assert(seq.current().style == JamStyle::Blues);

    seq.reset();
    assert(seq.next());
    seq.restorePosition(1, 2);
    assert(seq.currentSectionIndex() == 1);
    seq.restorePosition(99, 99);
    assert(seq.currentSectionIndex() == seq.size() - 1);
    assert(seq.barsIntoSection() == seq.current().bars - 1);

    seq.clear();
    assert(seq.empty());

    // A held manual/final section can run indefinitely, including after a large
    // host seek. Its public int counter must saturate instead of overflowing.
    assert(seq.add({JamStyle::Rock, std::numeric_limits<int>::max(), 0.5f, false}));
    seq.restorePosition(0, std::numeric_limits<int>::max() - 1);
    (void)seq.advanceBar();
    assert(seq.barsIntoSection() == std::numeric_limits<int>::max());
    (void)seq.advanceBar();
    assert(seq.barsIntoSection() == std::numeric_limits<int>::max());
}
