#include "core/JamStyleProfile.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    const auto rock = JamStyleProfile::forStyle(JamStyle::Rock);
    const auto blues = JamStyleProfile::forStyle(JamStyle::Blues);
    const auto funk = JamStyleProfile::forStyle(JamStyle::Funk);
    const auto punk = JamStyleProfile::forStyle(JamStyle::Punk);
    const auto metal = JamStyleProfile::forStyle(JamStyle::Metal);
    const auto reggae = JamStyleProfile::forStyle(JamStyle::Reggae);
    const auto disco = JamStyleProfile::forStyle(JamStyle::Disco);
    const auto experimental = JamStyleProfile::forStyle(JamStyle::Experimental);

    assert(JamStyleCount == 21);
    assert(rock.phraseBars >= 4);
    assert(blues.phraseBars >= rock.phraseBars);
    assert(funk.fillBias < rock.fillBias);
    assert(punk.fillBias > blues.fillBias);
    assert(punk.minBarsBeforeFill <= rock.minBarsBeforeFill);
    assert(metal.fillBias >= punk.fillBias);
    assert(reggae.minBarsBeforeFill >= rock.minBarsBeforeFill);
    assert(disco.phraseBars == 4);
    assert(experimental.fillBias > rock.fillBias);

    const auto rockSettings = rock.toBrainSettings();
    assert(rockSettings.phraseBars == rock.phraseBars);
    assert(rockSettings.fillBias == rock.fillBias);
}
