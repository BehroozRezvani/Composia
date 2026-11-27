#pragma once

#include <composia/Application.hpp>
#include <winrt/Windows.System.h>
#include <chrono>
#include <functional>
#include <memory>

namespace composia {

// A UI-thread timer on the application's dispatcher queue, for waits such as a delay before
// saving or a tooltip's pause; animation belongs to the compositor instead. The callback runs on
// the UI thread and never after stop() or the timer's destruction, and an exception from it is
// rethrown by Application::run(), as one from a posted callback is. Starting a running timer starts
// its wait over, so one started at each keystroke fires once typing pauses. It sends no WM_TIMER:
// window procedures never see it. Create, start, stop and destroy it on the UI thread.
class Timer {
public:
    Timer(Application& application, std::function<void()> callback);
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // Fires once after interval, or every interval while repeating, until stopped.
    void start(std::chrono::milliseconds interval, bool repeating = false);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    // The dispatcher queue timer underneath, for what this class does not cover.
    [[nodiscard]] const winrt::Windows::System::DispatcherQueueTimer& native() const noexcept { return timer_; }

private:
    struct State;
    Application& application_;
    std::shared_ptr<State> state_;
    winrt::Windows::System::DispatcherQueueTimer timer_{nullptr};
    winrt::Windows::System::DispatcherQueueTimer::Tick_revoker tick_;
};

}
