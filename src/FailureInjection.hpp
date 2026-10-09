#pragma once

// Failure points for the recovery tests, compiled in only for the composia-testing library.
namespace composia::detail {
enum class FailurePoint { none, hardwareDevice, deviceCreated, removalRegistered, compositionSwitch };
#ifdef COMPOSIA_TESTING
inline thread_local FailurePoint failurePoint = FailurePoint::none;
// Throws when the test armed this point.
inline void checkpoint(FailurePoint point) {
    if (failurePoint == point) {
        failurePoint = FailurePoint::none;
        throw winrt::hresult_error(E_OUTOFMEMORY);
    }
}
// The operation's result, or DXGI_ERROR_UNSUPPORTED when the test armed this point.
inline HRESULT injected(FailurePoint point, HRESULT actual) {
    if (failurePoint != point) { return actual; }
    failurePoint = FailurePoint::none;
    return DXGI_ERROR_UNSUPPORTED;
}
#else
inline void checkpoint(FailurePoint) noexcept {}
inline HRESULT injected(FailurePoint, HRESULT actual) noexcept { return actual; }
#endif
}
