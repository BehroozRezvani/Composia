#include <composia/Application.hpp>
#include <DispatcherQueue.h>
#include <spdlog/spdlog.h>
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
        OutputDebugStringW(L"Composia shutdown failed; use Application::close() to observe errors.\n");
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

bool Application::post(std::function<void()> callback) {
    const auto state = callbacks_;
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
    spdlog::info("event=application_shutdown");
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
            spdlog::warn("event=draw_device_loss hresult=0x{:08X}", static_cast<unsigned>(error));
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
    return std::ranges::any_of(windows_, [](const Window* window) { return window->hwnd() != nullptr; });
}

void Application::notify_graphics_recreated() {
    const auto snapshot = windows_;
    for (auto window : snapshot) {
        if (std::ranges::find(windows_, window) != windows_.end() && window->hwnd()) {
            window->on_graphics_recreated();
            if (std::ranges::find(windows_, window) != windows_.end() && window->hwnd()) { window->invalidate(); }
        }
    }
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
            spdlog::warn("event=device_removed hresult=0x{:08X}",
                static_cast<unsigned>(graphics().d3d_device()->GetDeviceRemovedReason()));
            graphics().recreate();
        }
        MSG message{};
        for (int dispatched = 0; dispatched != 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++dispatched) {
            if (message.message == WM_QUIT) {
                rethrow_callback_error();
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
            rethrow_callback_error();
        }
    }
}

}
