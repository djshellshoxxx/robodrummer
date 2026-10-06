#pragma once
#include <algorithm>

namespace robodrummer {

enum class MeterSource {
    Default,
    Host,
    Manual
};

struct MeterSelectionInput {
    bool hostAvailable{false};
    int hostNumerator{4};
    int hostDenominator{4};
    bool manualEnabled{false};
    int manualNumerator{4};
    int manualDenominator{4};
};

struct MeterSelectionResult {
    int numerator{4};
    int denominator{4};
    MeterSource source{MeterSource::Default};
};

class MeterSelection {
public:
    [[nodiscard]] static MeterSelectionResult resolve(const MeterSelectionInput& input) noexcept {
        MeterSelectionResult result;

        if (input.manualEnabled) {
            result.numerator = sanitizeNumerator(input.manualNumerator);
            result.denominator = sanitizeDenominator(input.manualDenominator);
            result.source = MeterSource::Manual;
            return result;
        }

        if (input.hostAvailable) {
            result.numerator = sanitizeNumerator(input.hostNumerator);
            result.denominator = sanitizeDenominator(input.hostDenominator);
            result.source = MeterSource::Host;
            return result;
        }

        return result;
    }

private:
    static int sanitizeNumerator(int value) noexcept {
        return std::clamp(value, 2, 12);
    }

    static int sanitizeDenominator(int value) noexcept {
        switch (value) {
            case 2:
            case 4:
            case 8:
            case 16:
                return value;
            default:
                return 4;
        }
    }
};

} // namespace robodrummer
