#pragma once
namespace robodrummer {
struct HostTransportSnapshot {
    bool playing{false};
    double bpm{120.0};
    double ppqPosition{0.0};
    int numerator{4};
    int denominator{4};
    bool validTempo{false};
    bool validPpq{false};
};
inline HostTransportSnapshot makeTransportSnapshot(bool playing, double bpm, bool hasBpm, double ppq, bool hasPpq, int num, int den) noexcept {
    HostTransportSnapshot s;
    s.playing = playing;
    s.validTempo = hasBpm && bpm > 0.0;
    s.bpm = s.validTempo ? bpm : 120.0;
    s.validPpq = hasPpq;
    s.ppqPosition = hasPpq ? ppq : 0.0;
    s.numerator = num > 0 ? num : 4;
    s.denominator = den > 0 ? den : 4;
    return s;
}
}
