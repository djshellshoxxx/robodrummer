#pragma once
#include <optional>

namespace rdui {

// An explicit saved choice always wins. A failed or unsupported OS query is off.
[[nodiscard]] inline bool resolveReducedMotionPreference(std::optional<bool> saved,
                                                         std::optional<bool> platform) noexcept {
    return saved.value_or(platform.value_or(false));
}

// Message-thread initialization only. No settings writes or global overrides.
[[nodiscard]] std::optional<bool> queryPlatformReducedMotion() noexcept;

} // namespace rdui
