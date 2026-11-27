#include <composia/Timer.hpp>
#include <utility>

namespace composia {

// What a tick needs, shared with the handler so that a tick queued before stop() or the timer's
// destruction finds it stopped or gone.
struct Timer::State {
    std::function<void()> callback;
    bool running{}, repeating{};
};

Timer::Timer(Application& application, std::function<void()> callback)
    : application_(application), state_(std::make_shared<State>()) {
    application_.verify_thread();
    state_->callback = std::move(callback);
    timer_ = application_.dispatcher().DispatcherQueue().CreateTimer();
    tick_ = timer_.Tick(winrt::auto_revoke, [state = std::weak_ptr{state_}, &application](const auto& timer, const auto&) {
        const auto current = state.lock();
        if (!current || !current->running) { return; }
        if (!current->repeating) {
            current->running = false;
            timer.Stop();
        }
        // Held for the call: the callback may destroy the timer.
        const auto callback = current->callback;
        try {
            if (callback) { callback(); }
        } catch (...) {
            application.report_error(std::current_exception());
        }
    });
}

Timer::~Timer() {
    stop();
    tick_.revoke();
    state_.reset();
}

void Timer::start(std::chrono::milliseconds interval, bool repeating) {
    application_.verify_thread();
    timer_.Stop();
    timer_.Interval(interval);
    timer_.IsRepeating(repeating);
    state_->repeating = repeating;
    state_->running = true;
    timer_.Start();
}

void Timer::stop() noexcept {
    if (state_) { state_->running = false; }
    if (timer_) {
        try { timer_.Stop(); } catch (...) {}
    }
}

bool Timer::running() const noexcept { return state_ && state_->running; }

}
