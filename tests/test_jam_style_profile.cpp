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

    // Regression: the plugin must use the full style mapping, not the old
    // six-style fallback.  All appended host parameter values must resolve.
    for (int i = 0; i < JamStyleCount; ++i) {
        const auto selection = static_cast<JamStyle>(i);
        assert(jamStyleName(selection)[0] != '\0');
        const auto groove = styleForJamStyle(selection);
        assert(groove.hatHitsPerBeat > 0);
    }
    assert(styleForJamStyle(JamStyle::Reggae).oneDrop);
    assert(styleForJamStyle(JamStyle::Disco).fourOnFloor);
    assert(styleForJamStyle(JamStyle::HalfTime).halfTimeSnare);
    assert(styleForJamStyle(JamStyle::JazzSwing).jazzRide);
}
