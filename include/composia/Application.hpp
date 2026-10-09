#pragma once

#include <composia/GraphicsDevice.hpp>
#include <composia/Window.hpp>
#include <winrt/Windows.System.h>
#include <functional>
#include <memory>
#include <vector>

namespace composia {

// The UI thread's application object: a single-threaded apartment, a dispatcher queue, the
// compositor, the graphics device, and the message loop that serves every Window. Create one on
// the UI thread before any window; every other Composia call belongs on that thread too, except
// post(). Application and Window operations called from another thread throw std::logic_error.
class Application {
public:
    // forceWarp selects the WARP software device instead of hardware; without it, a machine
    // without a usable hardware device falls back to WARP.
    explicit Application(bool forceWarp = false);
    // Closes the application if close() was not called; errors are then only logged.
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs the message loop until the last top-level window is destroyed, or until WM_QUIT, whose
    // exit code it returns. It recovers from graphics device removal, also while idle, and
    // rethrows the first exception that escaped a window's message handler or a posted callback.
    int run();
    // Runs a drawing operation; if it fails with device loss, replaces the graphics device and
    // runs it once more. Other failures, and a second device loss, propagate.
    void render(const std::function<void()>& draw);
    // Queues a callback on the UI thread from any thread. Returns false once the application is
    // closing. An exception from the callback is rethrown by run() or close().
    bool post(std::function<void()> callback);
    // Shuts down: drops callbacks not yet run, drains the dispatcher queue, then releases the
    // compositor and graphics device, and rethrows a callback error not yet reported. Destroy
    // every Window first. Calling it again does nothing.
    void close();
    [[nodiscard]] GraphicsDevice& graphics() const;
    [[nodiscard]] const composition::Compositor& compositor() const noexcept { return compositor_; }
    [[nodiscard]] const winrt::Windows::System::DispatcherQueueController& dispatcher() const noexcept { return dispatcher_; }

private:
    friend class Window;
    void attach(Window&);
    void detach(Window&) noexcept;
    void report_error(std::exception_ptr) noexcept;
    void rethrow_callback_error();
    bool has_windows() const noexcept;
    void remember_focus() const noexcept;
    void verify_thread() const;
    void notify_graphics_recreated();
    void shutdown() noexcept;

    struct Apartment {
        Apartment() { winrt::init_apartment(winrt::apartment_type::single_threaded); }
        ~Apartment() { winrt::uninit_apartment(); }
    } apartment_;
    winrt::Windows::System::DispatcherQueueController dispatcher_{nullptr};
    composition::Compositor compositor_{nullptr};
    std::unique_ptr<GraphicsDevice> graphics_;
    std::vector<Window*> windows_;
    std::exception_ptr callbackError_;
    struct CallbackState;
    std::shared_ptr<CallbackState> callbacks_;
    Connection graphicsConnection_;
    winrt::Windows::Foundation::IAsyncAction shutdownAction_{nullptr};
    DWORD thread_ = GetCurrentThreadId();
    bool closing_{};
    bool closed_{};
};

}
