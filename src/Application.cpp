#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
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
        controller_ = winrt::Windows::UI::Composition::Core::CompositorController{};
        compositor_ = controller_.Compositor();
        commitNeeded_ = controller_.CommitNeeded([this, state = std::weak_ptr{callbacks_}](const auto&, const auto&) {
            // Framework requests can arrive off-thread; composition is committed on the UI queue.
            if (const auto callbacks = state.lock()) {
                (void)enqueue(callbacks, [this] { if (!closing_ && !closed_) { commit(); } });
            }
        });
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

void Application::retire(std::unique_ptr<Window> window) {
    verify_thread();
    if (!window || !window->dispatching()) { return; }   // Destroyed as it leaves.
    window->retiring_ = true;
    retired_.push_back(std::move(window));
}

void Application::release_retired(Window& window) noexcept {
    const auto found = std::ranges::find_if(retired_, [&](const auto& held) { return held.get() == &window; });
    if (found == retired_.end()) { return; }
    auto doomed = std::move(*found);
    retired_.erase(found);
    try { doomed.reset(); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}

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
    // Retired windows are no longer anyone's to destroy.
    while (!retired_.empty()) {
        auto doomed = std::move(retired_.back());
        retired_.pop_back();
    }
    if (!windows_.empty()) { throw std::logic_error("Destroy window objects before closing Application"); }
    if (closing_) { throw std::logic_error("Application shutdown is already in progress"); }
    closing_ = true;
    const auto clearClosing = wil::scope_exit([&] { closing_ = false; });
    if (callbacks_) { callbacks_->accepting.store(false); }
    if (controller_ && commitNeeded_.value) { controller_.CommitNeeded(std::exchange(commitNeeded_, {})); }
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
    if (controller_) { controller_.Close(); controller_ = nullptr; compositor_ = nullptr; }
    graphics_.reset();
    dispatcher_ = nullptr;
    closed_ = true;
    detail::log(LogLevel::info, "event=application_shutdown");
    rethrow_callback_error();
}

void Application::commit() {
    verify_thread();
    if (closing_ || closed_) { throw std::logic_error("Application is closing"); }
    if (!updateDepth_) {
        controller_.Commit();
        graphics_->committed();
    }
}

void Application::update(Window& root, const std::function<void()>& layout) {
    verify_thread();
    if (closing_ || closed_) { throw std::logic_error("Application is closing"); }
    if (!updateDepth_ && std::ranges::any_of(windows_, [](const Window* window) { return window->painting_; })) {
        throw std::logic_error("Start layout updates from input or resize handlers, not painting");
    }
    THROW_HR_IF(E_INVALIDARG, &root.application() != this);
    (void)root.client_pixels();
    const auto hwnd = root.hwnd();
    if (std::find(updateRoots_.begin(), updateRoots_.end(), hwnd) == updateRoots_.end()) { updateRoots_.push_back(hwnd); }
    ++updateDepth_;
    auto leave = wil::scope_exit([&] {
        if (--updateDepth_ == 0) { updateRoots_.clear(); }
    });
    layout();
    if (root.hwnd()) { root.invalidate(); }
    if (updateDepth_ != 1) { return; }
    // Keep commits deferred while synchronous paints call back into the application.
    for (unsigned pass = 0; !updateRoots_.empty(); ++pass) {
        if (pass == 16) { throw std::logic_error("Layout invalidated itself during every paint"); }
        const auto roots = std::exchange(updateRoots_, {});
        for (const auto handle : roots) {
            if (!IsWindow(handle) || GetWindowThreadProcessId(handle, nullptr) != thread_) { continue; }
            paint_window(handle);
            struct Context { Application* app; std::exception_ptr error; } context{this, {}};
            EnumChildWindows(handle, [](HWND child, LPARAM data) -> BOOL {
                const auto context = reinterpret_cast<Context*>(data);
                try {
                    if (GetWindowThreadProcessId(child, nullptr) == GetCurrentThreadId() && IsWindowVisible(child)) { context->app->paint_window(child); }
                    return TRUE;
                } catch (...) { context->error = std::current_exception(); return FALSE; }
            }, reinterpret_cast<LPARAM>(&context));
            if (context.error) { std::rethrow_exception(context.error); }
            rethrow_callback_error();
            const bool pending = std::ranges::any_of(windows_, [handle](const Window* window) {
                const auto hwnd = window->hwnd();
                return window->paintPending_ && !window->painting_ && hwnd && IsWindowVisible(hwnd) &&
                    (hwnd == handle || IsChild(handle, hwnd)) && CompositionWindowTarget::hosts_children(hwnd);
            });
            if (pending && std::find(updateRoots_.begin(), updateRoots_.end(), handle) == updateRoots_.end()) { updateRoots_.push_back(handle); }
        }
    }
    leave.release();
    --updateDepth_;
    commit();
}

void Application::paint_window(HWND handle) {
    UpdateWindow(handle);
    rethrow_callback_error();
    // Win32 can defer a transparent child's WM_PAINT behind its siblings. Its composition
    // surface still needs its pending content before the shared tree is committed.
    if (!CompositionWindowTarget::hosts_children(handle) || !IsWindowVisible(handle)) { return; }
    const auto found = std::ranges::find_if(windows_, [handle](const Window* window) { return window->hwnd() == handle; });
    if (found == windows_.end()) { return; }
    const auto window = *found;
    if (!window->paintPending_ || window->painting_) { return; }
    RECT region{};
    if (GetUpdateRect(handle, &region, FALSE)) { window->paintRect_ = region; }
    ValidateRect(handle, nullptr);
    window->paintPending_ = false;
    window->painting_ = true;
    const auto finished = wil::scope_exit([&] {
        if (std::find(windows_.begin(), windows_.end(), window) != windows_.end()) {
            window->paintRect_.reset();
            window->painting_ = false;
        }
    });
    window->on_paint();
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

Window* Application::top_window(HWND hwnd) const noexcept {
    const auto root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    if (!root) { return nullptr; }
    const auto found = std::ranges::find_if(windows_, [root](const Window* window) { return window->top_level() && window->hwnd() == root; });
    return found == windows_.end() ? nullptr : *found;
}

void Application::focus_cue(HWND target, bool visible) noexcept {
    auto* top = top_window(target);
    if (!top || top->focusVisible_ == visible) { return; }
    top->focusVisible_ = visible;
    const auto root = top->hwnd();
    // The windows the change reaches may come and go as they repaint.
    const auto windows = windows_;
    for (auto* window : windows) {
        if (std::ranges::find(windows_, window) == windows_.end() || !window->hwnd()) { continue; }
        if (GetAncestor(window->hwnd(), GA_ROOT) != root) { continue; }
        try { window->on_focus_visible(visible); }
        catch (...) { report_error(std::current_exception()); }
    }
}

void Application::note_focus_cue(const MSG& message) noexcept {
    switch (message.message) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        focus_cue(message.hwnd, true);
        break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK:
        focus_cue(message.hwnd, false);
        break;
    default: break;
    }
}

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

bool Application::preview_key(const MSG& message) {
    std::vector<Window*> chain;
    for (auto hwnd = message.hwnd; hwnd; hwnd = GetParent(hwnd)) {
        const auto found = std::ranges::find(windows_, hwnd, &Window::hwnd);
        if (found != windows_.end()) { chain.push_back(*found); }
        if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) { break; }
    }
    const auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
    const KeyEvent key{static_cast<unsigned>(message.wParam), true, down(VK_SHIFT), down(VK_CONTROL),
        down(VK_MENU), message.lParam};
    for (auto* window : chain) {
        // A handler can destroy windows, this one included.
        if (std::ranges::find(windows_, window) != windows_.end() && window->hwnd() && window->preview_key(key)) {
            return true;
        }
    }
    return false;
}

int Application::run() {
    verify_thread();
    if (closing_ || closed_) { throw std::logic_error("Application is closing"); }
    for (;;) {
        rethrow_callback_error();
        if (!has_windows()) { return 0; }
        commit();
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
            // Keys and presses in native controls count for the focus cue too.
            note_focus_cue(message);
            // Key previews come first, so an accelerator works wherever the focus is.
            bool handled = (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) && preview_key(message);
            for (const auto window : windows_) {
                if (handled) { break; }
                const auto root = window->hwnd();
                if (window->top_level() && root && (message.hwnd == root || IsChild(root, message.hwnd))) {
                    // A retained control can receive keyboard input on the dialog root itself.
                    // IsDialogMessage handles that HWND as a dialog even when it wants text.
                    if (root == message.hwnd && (message.message == WM_KEYDOWN ||
                        message.message == WM_KEYUP || message.message == WM_CHAR)) {
                        const auto code = SendMessageW(root, WM_GETDLGCODE, message.wParam,
                            reinterpret_cast<LPARAM>(&message));
                        const auto wants = message.wParam == VK_TAB ? DLGC_WANTTAB :
                            (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE) ? DLGC_WANTMESSAGE :
                            DLGC_WANTCHARS;
                        if (code & (wants | DLGC_WANTMESSAGE)) { break; }
                    }
                    handled = IsDialogMessageW(root, &message) != FALSE;
                    break;
                }
            }
            if (!handled) { TranslateMessage(&message); DispatchMessageW(&message); }
            commit();
            remember_focus();
            rethrow_callback_error();
        }
    }
}

}
