// Harness-only crash reporting. MSYS/Cygwin shells map most native Windows
// NTSTATUS crash codes (stack overflow, fail-fast, heap corruption) to exit 127,
// hiding the cause; this prints the real code and flushes stdout first.
#pragma once
namespace rdtest {
void installCrashDiagnostics() noexcept;
}
