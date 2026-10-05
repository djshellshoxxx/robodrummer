#include "core/JamStyleProfile.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    const auto rock = JamStyleProfile::forStyle(JamStyle::Rock);
    const auto blues = JamStyleProfile::forStyle(JamStyle::Blues);
    const auto funk = JamStyleProfile::forStyle(JamStyle::Funk);
    const auto punk = JamStyleProfile::forStyle(JamStyle::Punk);

    assert(rock.phraseBars >= 4);
    assert(blues.phraseBars >= rock.phraseBars);
    assert(funk.fillBias < rock.fillBias);
    assert(punk.fillBias > blues.fillBias);
    assert(punk.minBarsBeforeFill <= rock.minBarsBeforeFill);

    const auto rockSettings = rock.toBrainSettings();
    assert(rockSettings.phraseBars == rock.phraseBars);
    assert(rockSettings.fillBias == rock.fillBias);
}
