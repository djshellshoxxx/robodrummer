#include "audio/KitImportPolicy.h"
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using namespace robodrummer;
    for (int instrument = 0; instrument < 16; ++instrument)
        for (int layer = 0; layer < 4; ++layer)
            for (int rr = 0; rr < 4; ++rr)
                assert(kitSlotIndex({static_cast<DrumInstrument>(instrument), layer, rr}) ==
                       static_cast<std::size_t>(instrument * 16 + layer * 4 + rr));
    assert(!kitSlotIndex({DrumInstrument::Kick, 4, 0}));
    assert(!kitSlotIndex({DrumInstrument::Kick, 0, 4}));
    assert(!kitSlotIndex({DrumInstrument::Kick, -1, 0}));
    const auto plan = planKitRead(std::numeric_limits<std::int64_t>::max(), 192000.0, 48000.0, 2);
    assert(plan && plan->sourceFrames == 3840000 && plan->targetFrames == 960000);
    assert(plan->sourceBytes == 30720000 && plan->targetBytes == 7680000);
    assert(!planKitRead(32, 0, 48000, 2));
    assert(!planKitRead(32, INFINITY, 48000, 2));
    assert(!planKitRead(32, 48000, 48000, 0));
    assert(!planKitRead(32, 48000, 48000, 3));
    KitImportBudget budget(1000, 100);
    assert(budget.fits(400, 500)); // exactly at the peak cap
    assert(!budget.fits(401, 500));
    budget.retain(500);
    assert(budget.retainedBytes() == 600);
    assert(!budget.fits(400, 1));
    assert(!budget.fits(std::numeric_limits<std::size_t>::max(), 1));
    // A whole oversized folder is modeled without allocating its decoded audio.
    KitImportBudget large(kMaxKitDecodedBytes, 1024 * 1024);
    unsigned accepted = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        if (!large.fits(plan->sourceBytes, plan->targetBytes)) continue;
        large.retain(plan->targetBytes);
        ++accepted;
    }
    assert(accepted > 0 && accepted < 32);
    assert(large.retainedBytes() < kMaxKitDecodedBytes);
}
