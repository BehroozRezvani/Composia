#pragma once

#include <composia/GraphicsDevice.hpp>
#include <composia/Window.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace composia {

class Application {
public:
    explicit Application(bool forceWarp = false);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();
    void render(const std::function<void()>& draw);
    bool post(std::function<void()> callback);
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
