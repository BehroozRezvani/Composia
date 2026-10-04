#pragma once

namespace composia::detail {
enum class FailurePoint { none, deviceCreated, removalRegistered, compositionSwitch };
#ifdef COMPOSIA_TESTING
inline thread_local FailurePoint failurePoint = FailurePoint::none;
inline void checkpoint(FailurePoint point) {
    if (failurePoint == point) {
        failurePoint = FailurePoint::none;
        throw winrt::hresult_error(E_OUTOFMEMORY);
    }
}
#else
inline void checkpoint(FailurePoint) noexcept {}
#endif
}
