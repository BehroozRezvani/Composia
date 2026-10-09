#include <composia/Application.hpp>
#include "Logging.hpp"
#include <DispatcherQueue.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace composia {

struct Application::CallbackState {
    std::atomic_bool accepting{true};
    winrt::Windows::System::DispatcherQueue queue{nullptr};
    std::exception_ptr error;
};

// UISettings reports color and text size changes, which no window message announces reliably.
struct Application::AppearanceWatch {
    winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
    winrt::Windows::UI::ViewManagement::UISettings::ColorValuesChanged_revoker colors;
    winrt::Windows::UI::ViewManagement::UISettings::TextScaleFactorChanged_revoker textScale;
};

Application::Apartment::Apartment() { winrt::init_apartment(winrt::apartment_type::single_threaded); }

// Leaving the thread's last apartment can unload COM servers, so C++/WinRT's cached activation
// factories go first; a later Application on this thread would otherwise use factories from
// unloaded code.
Application::Apartment::~Apartment() {
    winrt::clear_factory_cache();
    winrt::uninit_apartment();
}

Application::Application(bool forceWarp) {
    const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
    THROW_IF_FAILED(CreateDispatcherQueueController(options,
        reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(dispatcher_))));
    try {
        callbacks_ = std::make_shared<CallbackState>();
        callbacks_->queue = dispatcher_.DispatcherQueue();
        compositor_ = composition::Compositor{};
        graphics_ = std::make_unique<GraphicsDevice>(compositor_, forceWarp);
        graphicsConnection_ = graphics_->on_recreated([this](auto) { notify_graphics_recreated(); });
        appearance_ = Appearance::current();
        watch_appearance();
    } catch (...) {
        shutdown();
        throw;
    }
}

Application::~Application() { shutdown(); }

void Application::shutdown() noexcept {
    try {
        close();
    } catch (...) {
        detail::log(LogLevel::warning, "event=application_shutdown_failed detail=call_close_to_observe_errors");
    }
}

void Application::verify_thread() const {
    if (GetCurrentThreadId() != thread_) { throw std::logic_error("Composia operation requires the UI thread"); }
}

GraphicsDevice& Application::graphics() const {
    verify_thread();
    if (!graphics_) { throw std::logic_error("Application graphics are closed"); }
    return *graphics_;
}

bool Application::post(std::function<void()> callback) { return enqueue(callbacks_, std::move(callback)); }

bool Application::enqueue(const std::shared_ptr<CallbackState>& state, std::function<void()> callback) {
    if (!state || !state->accepting.load()) { return false; }
    return state->queue.TryEnqueue([state, callback = std::move(callback)] {
        if (!state->accepting.load()) { return; }
        try { callback(); }
        catch (...) { if (!state->error) { state->error = std::current_exception(); } }
    });
}

void Application::close() {
    verify_thread();
    if (closed_) { return; }
    if (!windows_.empty()) { throw std::logic_error("Destroy window objects before closing Application"); }
    if (closing_) { throw std::logic_error("Application shutdown is already in progress"); }
    closing_ = true;
    const auto clearClosing = wil::scope_exit([&] { closing_ = false; });
    if (callbacks_) { callbacks_->accepting.store(false); }
    appearanceWatch_.reset();
    std::optional<WPARAM> quitCode;
    const auto restoreQuit = wil::scope_exit([&] { if (quitCode) { PostQuitMessage(static_cast<int>(*quitCode)); } });
    if (dispatcher_) {
        if (!shutdownAction_) { shutdownAction_ = dispatcher_.ShutdownQueueAsync(); }
        while (shutdownAction_.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
            const auto wait = MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            THROW_LAST_ERROR_IF(wait == WAIT_FAILED);
            MSG message{};
            for (int count = 0; count != 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
                if (message.message == WM_QUIT) { quitCode = message.wParam; }
                else { TranslateMessage(&message); DispatchMessageW(&message); }
            }
        }
        shutdownAction_.GetResults();
    }
    graphicsConnection_.disconnect();
    if (compositor_) { compositor_.Close(); compositor_ = nullptr; }
    graphics_.reset();
    dispatcher_ = nullptr;
    closed_ = true;
    detail::log(LogLevel::info, "event=application_shutdown");
    rethrow_callback_error();
}

void Application::render(const std::function<void()>& draw) {
    verify_thread();
    if (closing_ || closed_) { throw std::logic_error("Application is closing"); }
    for (int attempt = 0; attempt != 2; ++attempt) {
        try {
            draw();
            return;
        } catch (...) {
            HRESULT error{};
            try { throw; }
            catch (const winrt::hresult_error& exception) { error = exception.code(); }
            catch (const wil::ResultException& exception) { error = exception.GetErrorCode(); }
            catch (...) { throw; }
            if (attempt != 0 || !graphics().is_device_loss(error)) {
                throw;
            }
            detail::log(LogLevel::warning, "event=draw_device_loss hresult=" + detail::hex(error));
            graphics().recreate();
        }
    }
}

void Application::attach(Window& window) {
    verify_thread();
    if (closing_ || closed_) { throw std::logic_error("Application is closing"); }
    windows_.push_back(&window);
}
void Application::detach(Window& window) noexcept { std::erase(windows_, &window); }

void Application::report_error(std::exception_ptr error) noexcept {
    if (!callbackError_) { callbackError_ = std::move(error); }
}

void Application::rethrow_callback_error() {
    if (callbackError_) { std::rethrow_exception(std::exchange(callbackError_, nullptr)); }
    if (callbacks_ && callbacks_->error) { std::rethrow_exception(std::exchange(callbacks_->error, nullptr)); }
}

bool Application::has_windows() const noexcept {
    return std::ranges::any_of(windows_, [](const Window* window) { return window->top_level() && window->hwnd(); });
}

// Records which window inside each top-level window has the focus, so activation can return it.
void Application::remember_focus() const noexcept {
    const auto focus = GetFocus();
    if (!focus) { return; }
    const auto root = GetAncestor(focus, GA_ROOT);
    for (const auto window : windows_) {
        if (window->top_level() && window->hwnd() == root) {
            window->remember_focus(focus);
            return;
        }
    }
}

void Application::notify_graphics_recreated() {
    const auto snapshot = windows_;
    std::exception_ptr firstError;
    for (auto window : snapshot) {
        if (std::ranges::find(windows_, window) != windows_.end() && window->hwnd()) {
            try {
                window->on_graphics_recreated();
                if (std::ranges::find(windows_, window) != windows_.end() && window->hwnd()) { window->invalidate(); }
            } catch (...) { if (!firstError) { firstError = std::current_exception(); } }
        }
    }
    if (firstError) { std::rethrow_exception(firstError); }
}

// UISettings raises its events on a worker thread; the refresh runs on the UI thread, while the
// application still accepts callbacks.
void Application::watch_appearance() noexcept {
    try {
        auto watch = std::make_unique<AppearanceWatch>();
        watch->settings = winrt::Windows::UI::ViewManagement::UISettings{};
        const auto changed = [state = std::weak_ptr{callbacks_}, this](const auto&, const auto&) {
            if (const auto alive = state.lock()) { (void)enqueue(alive, [this] { refresh_appearance(); }); }
        };
        watch->colors = watch->settings.ColorValuesChanged(winrt::auto_revoke, changed);
        watch->textScale = watch->settings.TextScaleFactorChanged(winrt::auto_revoke, changed);
        appearanceWatch_ = std::move(watch);
    } catch (...) {
        detail::log(LogLevel::warning, "event=appearance_events_unavailable");
    }
}

// Reads the appearance again and, when it changed, tells every window and then the subscribers.
void Application::refresh_appearance() {
    if (closing_ || closed_) { return; }
    const auto now = Appearance::current();
    if (now == appearance_) { return; }
    appearance_ = now;
    detail::log(LogLevel::info, std::string{"event=appearance_changed dark="} + (now.dark ? "1" : "0") +
        " high_contrast=" + (now.highContrast ? "1" : "0"));
    const auto snapshot = windows_;
    std::exception_ptr firstError;
    const auto live = [&](Window* window) { return std::ranges::find(windows_, window) != windows_.end() && window->hwnd(); };
    for (auto window : snapshot) {
        if (!live(window)) { continue; }
        try {
            window->on_appearance_changed();
            if (live(window)) { window->invalidate(); }
        } catch (...) { if (!firstError) { firstError = std::current_exception(); } }
    }
    try { appearanceChanged_.emit(appearance_); }
    catch (...) { if (!firstError) { firstError = std::current_exception(); } }
    if (firstError) { std::rethrow_exception(firstError); }
}

int Application::run() {
    verify_thread();
    if (closing_ || closed_) { throw std::logic_error("Application is closing"); }
    for (;;) {
        rethrow_callback_error();
        if (!has_windows()) { return 0; }
        const HANDLE handles[]{graphics().removed_event()};
        const auto wait = MsgWaitForMultipleObjectsEx(1, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        THROW_LAST_ERROR_IF(wait == WAIT_FAILED);
        if (wait == WAIT_OBJECT_0) {
            detail::log(LogLevel::warning, "event=device_removed hresult=" + detail::hex(graphics().d3d_device()->GetDeviceRemovedReason()));
            graphics().recreate();
        }
        MSG message{};
        for (int dispatched = 0; dispatched != 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++dispatched) {
            if (message.message == WM_QUIT) {
                rethrow_callback_error();
                return static_cast<int>(message.wParam);
            }
            bool handled{};
            for (const auto window : windows_) {
                const auto root = window->hwnd();
                if (window->top_level() && root && (message.hwnd == root || IsChild(root, message.hwnd))) {
                    handled = IsDialogMessageW(root, &message) != FALSE;
                    break;
                }
            }
            if (!handled) { TranslateMessage(&message); DispatchMessageW(&message); }
            remember_focus();
            rethrow_callback_error();
        }
    }
}

}
