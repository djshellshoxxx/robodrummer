#include "plugin/HostTransportAdapter.h"
#include <cassert>
int main() {
    using namespace robodrummer;
    auto missing = makeTransportSnapshot(true, 0.0, false, 0.0, false, 0, 0);
    assert(missing.playing);
    assert(!missing.validTempo && missing.bpm == 120.0);
    assert(!missing.validPpq && missing.ppqPosition == 0.0);
    assert(missing.numerator == 4 && missing.denominator == 4);
    auto normal = makeTransportSnapshot(true, 120.0, true, 16.0, true, 4, 4);
    assert(normal.validTempo && normal.bpm == 120.0);
    assert(normal.validPpq && normal.ppqPosition == 16.0);
    auto odd = makeTransportSnapshot(false, 95.0, true, 3.5, true, 7, 8);
    assert(odd.numerator == 7 && odd.denominator == 8);
}
