// Kept apart from PluginHarness.cpp so <windows.h> never meets JUCE's names.
#include "CrashDiagnostics.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace rdtest {
namespace {
[[noreturn]] void onTerminate() noexcept {
    std::fflush(stdout);
    std::fputs("harness: std::terminate (uncaught exception or noexcept violation)\n", stderr);
    std::fflush(stderr);
    std::abort();
}
#if defined(_WIN32)
LONG WINAPI onUnhandled(EXCEPTION_POINTERS* info) {
    std::fflush(stdout);
    std::fprintf(stderr, "harness: unhandled native exception 0x%08lx at %p\n",
                 static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                 info->ExceptionRecord->ExceptionAddress);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;   // keep the original exit status
}
#endif
} // namespace

void installCrashDiagnostics() noexcept {
    std::set_terminate(onTerminate);
#if defined(_WIN32)
    ULONG reserve = 64 * 1024;   // stack left for the filter after a stack overflow
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(onUnhandled);
#endif
}
} // namespace rdtest
