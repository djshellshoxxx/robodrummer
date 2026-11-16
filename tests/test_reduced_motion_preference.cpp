#include "plugin/ReducedMotionPreference.h"
#include <cassert>

int main() {
    using rdui::resolveReducedMotionPreference;
    assert(!resolveReducedMotionPreference({}, {}));
    assert(resolveReducedMotionPreference({}, true));
    assert(!resolveReducedMotionPreference({}, false));
    assert(!resolveReducedMotionPreference(false, true));
    assert(resolveReducedMotionPreference(true, false));
    assert(resolveReducedMotionPreference(true, {}));
}
