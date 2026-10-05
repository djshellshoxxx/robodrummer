#include "analysis/PhraseGrooveModifier.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    const Style base = Style::basicRock();

    const auto build = PhraseGrooveModifier::apply(base, PhraseState::Build, 0.8f);
    assert(build.extraKickProbability > base.extraKickProbability);
    assert(build.crashOnSectionStart >= base.crashOnSectionStart);
    assert(build.openHatProbability > base.openHatProbability);

    const auto release = PhraseGrooveModifier::apply(base, PhraseState::Release, 0.8f);
    assert(release.extraKickProbability < base.extraKickProbability);
    assert(release.rideProbability > base.rideProbability);

    const auto brk = PhraseGrooveModifier::apply(base, PhraseState::Break, 1.0f);
    assert(brk.kickBeat1 < base.kickBeat1);
    assert(brk.snareBackbeat < base.snareBackbeat);
    assert(brk.closedHatProbability < base.closedHatProbability);

    const auto stable = PhraseGrooveModifier::apply(base, PhraseState::Stable, 1.0f);
    assert(stable.kickBeat1 == base.kickBeat1);
    assert(stable.extraKickProbability == base.extraKickProbability);
}
