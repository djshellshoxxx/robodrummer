#include "core/SectionSequencer.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    SectionSequencer<8> seq;
    assert(seq.empty());

    assert(seq.add({JamStyle::Rock, 2, 0.55f, true}));
    assert(seq.add({JamStyle::Funk, 1, 0.70f, true}));
    assert(seq.add({JamStyle::Blues, 4, 0.45f, false}));
    assert(seq.size() == 3);

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

    seq.clear();
    assert(seq.empty());
}
