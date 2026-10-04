#pragma once

#include <composia/GraphicsDevice.hpp>
#include <composia/Window.hpp>
#include <functional>
#include <memory>

namespace composia {

class Application {
public:
    explicit Application(bool forceWarp = false);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(Window&, const std::function<void()>& redraw);
    void render(const std::function<void()>& draw);
    [[nodiscard]] GraphicsDevice& graphics() const noexcept { return *graphics_; }
    [[nodiscard]] const composition::Compositor& compositor() const noexcept { return compositor_; }
    [[nodiscard]] const winrt::Windows::System::DispatcherQueueController& dispatcher() const noexcept { return dispatcher_; }

private:
    void shutdown() noexcept;

    struct Apartment {
        Apartment() { winrt::init_apartment(winrt::apartment_type::single_threaded); }
        ~Apartment() { winrt::uninit_apartment(); }
    } apartment_;
    winrt::Windows::System::DispatcherQueueController dispatcher_{nullptr};
    composition::Compositor compositor_{nullptr};
    std::unique_ptr<GraphicsDevice> graphics_;
};

}
