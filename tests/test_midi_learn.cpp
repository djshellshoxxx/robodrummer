#include "core/MidiLearn.h"
#include <cassert>
#include <cmath>

int main() {
    using robodrummer::MidiLearn;
    MidiLearn m;
    assert(m.state() == MidiLearn::State::Idle && m.mappingCount() == 0);
    assert(!m.handleCc(1, 74, 64).handled);

    // Learn.
    assert(m.arm(3, 1000));
    assert(m.state() == MidiLearn::State::Armed && m.armedSlot() == 3);
    auto r = m.handleCc(2, 74, 127);
    assert(r.learned && r.slot == 3 && r.learn.cc == 74 && r.learn.channel == 2);
    assert(m.state() == MidiLearn::State::Idle);
    assert(m.slotFor(2, 74) == 3 && m.ccForSlot(3) == 74 && m.channelForSlot(3) == 2);
    r = m.handleCc(2, 74, 127);
    assert(r.handled && !r.learned && r.slot == 3 && r.value == 1.0f);
    assert(std::abs(MidiLearn::normalize(64) - 64.0f / 127.0f) < 1e-6f);
    assert(MidiLearn::normalize(0) == 0.0f);
    assert(!m.handleCc(1, 74, 10).handled); // other channel

    // Cancel and timeout.
    m.arm(1, 0);
    m.cancel();
    assert(m.state() == MidiLearn::State::Idle && !m.handleCc(1, 20, 1).learned);
    m.arm(1, 100);
    assert(!m.tick(50));
    assert(m.tick(60) && m.timedOut() && m.state() == MidiLearn::State::Idle);
    assert(!m.handleCc(1, 20, 1).learned);
    m.arm(1, 0); // no timeout
    assert(!m.tick(1000000) && m.state() == MidiLearn::State::Armed);
    m.cancel();

    // Conflict: CC re-learned for another slot -> newest wins, old slot unlinked.
    m.arm(5, 0);
    r = m.handleCc(2, 74, 1);
    assert(r.learned && r.learn.displacedSlot == 3);
    assert(m.slotFor(2, 74) == 5 && m.ccForSlot(3) == -1 && m.mappingCount() == 1);
    // Slot re-learned with new CC replaces old CC.
    m.arm(5, 0);
    r = m.handleCc(1, 7, 1);
    assert(r.learn.replacedCc == 74 && m.slotFor(2, 74) == -1 && m.slotFor(1, 7) == 5 && m.mappingCount() == 1);

    // Unlink.
    m.unlinkSlot(5);
    assert(m.mappingCount() == 0 && m.slotFor(1, 7) == -1);
    m.link(1, 7, 2);
    m.unlinkCc(1, 7);
    assert(m.mappingCount() == 0);

    // Invalid input ignored.
    assert(!m.arm(64, 0) && !m.arm(-1, 0));
    assert(!m.handleCc(0, 5, 5).handled && !m.handleCc(1, 128, 5).handled && !m.handleCc(1, 5, 128).handled);
    assert(!m.link(17, 0, 0) && !m.link(1, 0, 64));

    // Expression default (off by default; user mapping wins).
    assert(!m.handleCc(1, 11, 100).handled);
    m.setExpressionDefault(true, 9);
    r = m.handleCc(4, 11, 127);
    assert(r.handled && r.slot == 9 && r.value == 1.0f);
    m.link(4, 11, 12);
    assert(m.handleCc(4, 11, 127).slot == 12);
    assert(m.handleCc(5, 11, 127).slot == 9);

    // Serialization.
    m.link(1, 74, 3);
    const auto text = m.serialize();
    MidiLearn n;
    assert(n.deserialize(text));
    assert(n.slotFor(1, 74) == 3 && n.slotFor(4, 11) == 12 && n.expressionDefaultEnabled() && n.expressionSlot() == 9);
    assert(n.serialize() == text);
    MidiLearn bad;
    bad.link(1, 1, 1);
    assert(!bad.deserialize("1:2>3;1:2>4"));
    assert(!bad.deserialize("17:2>3"));
    assert(!bad.deserialize("1:2>64"));
    assert(!bad.deserialize("garbage"));
    assert(!bad.deserialize("1:2>3;1:5>3"));
    assert(bad.slotFor(1, 1) == 1); // unchanged on failure
    assert(bad.deserialize("") && bad.mappingCount() == 0);
    return 0;
}
