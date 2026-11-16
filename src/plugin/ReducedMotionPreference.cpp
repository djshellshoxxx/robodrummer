#include "ReducedMotionPreference.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif defined(__APPLE__)
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

namespace rdui {

std::optional<bool> queryPlatformReducedMotion() noexcept {
#if defined(_WIN32)
    BOOL animation = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) != FALSE)
        return animation == FALSE;
#elif defined(__APPLE__)
    const auto workspaceClass = reinterpret_cast<id>(objc_getClass("NSWorkspace"));
    if (workspaceClass == nullptr) return {};
    const auto shared = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(workspaceClass, sel_registerName("sharedWorkspace"));
    if (shared == nullptr) return {};
    const auto preference = sel_registerName("accessibilityDisplayShouldReduceMotion");
    if (!reinterpret_cast<BOOL (*)(id, SEL, SEL)>(objc_msgSend)(shared, sel_registerName("respondsToSelector:"), preference)) return {};
    return reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(shared, preference) != static_cast<BOOL>(0);
#endif
    return {};
}

} // namespace rdui
