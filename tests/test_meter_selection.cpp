#include "core/MeterSelection.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    MeterSelectionInput input;
    input.hostAvailable = true;
    input.hostNumerator = 3;
    input.hostDenominator = 4;

    auto result = MeterSelection::resolve(input);
    assert(result.numerator == 3);
    assert(result.denominator == 4);
    assert(result.source == MeterSource::Host);

    input.manualEnabled = true;
    input.manualNumerator = 7;
    input.manualDenominator = 8;
    result = MeterSelection::resolve(input);
    assert(result.numerator == 7);
    assert(result.denominator == 8);
    assert(result.source == MeterSource::Manual);

    input.hostAvailable = false;
    input.manualEnabled = false;
    result = MeterSelection::resolve(input);
    assert(result.numerator == 4);
    assert(result.denominator == 4);
    assert(result.source == MeterSource::Default);

    input.manualEnabled = true;
    input.manualNumerator = 99;
    input.manualDenominator = 3;
    result = MeterSelection::resolve(input);
    assert(result.numerator == 12);
    assert(result.denominator == 4);
}
