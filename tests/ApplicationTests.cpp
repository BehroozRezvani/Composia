#include <composia/Application.hpp>
#include <composia/Log.hpp>
#include "FailureInjection.hpp"
#include "support/TestSupport.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// The Application and its graphics device: device replacement, with failures injected at the
// points FailureInjection.hpp defines (a replacement either completes or changes nothing), the
// fallback to WARP, shutdown, callback errors, and the UI thread rule.
using namespace composia;
using testing::require;

namespace {
// The Direct2D objects belong together: the context to the device, the device to the factory.
void require_consistent(GraphicsDevice& graphics) {
    wil::com_ptr<ID2D1Device> contextDevice;
    graphics.d2d_context()->GetDevice(contextDevice.put());
    wil::com_ptr<ID2D1Factory> deviceFactory;
    graphics.d2d_device()->GetFactory(deviceFactory.put());
    require(contextDevice.query<IUnknown>().get() == graphics.d2d_device().query<IUnknown>().get(), "The Direct2D context belongs to another device");
    require(deviceFactory.query<IUnknown>().get() == graphics.d2d_factory().query<IUnknown>().get(), "The Direct2D device belongs to another factory");
    require(graphics.d3d_context() && graphics.text_factory() && graphics.composition_device(), "A graphics object is missing");
}

int transaction(const testing::Options&) {
    Application app{true};
    auto& graphics = app.graphics();
    require(graphics.generation() == 1, "The first device is not generation 1");
    require_consistent(graphics);
    unsigned notifications{};
    auto connection = graphics.on_recreated([&](auto) { ++notifications; });
    for (auto point : {detail::FailurePoint::deviceCreated, detail::FailurePoint::removalRegistered, detail::FailurePoint::compositionSwitch}) {
        const auto device = graphics.d3d_device();
        const auto d2d = graphics.d2d_device().query<IUnknown>();
        const auto event = graphics.removed_event();
        const auto generation = graphics.generation();
        detail::failurePoint = point;
        require(testing::rejects(E_OUTOFMEMORY, [&] { graphics.recreate(); }), "Device replacement did not expose the injected failure");
        require(graphics.d3d_device().get() == device.get(), "Failed replacement changed the D3D device");
        require(graphics.removed_event() == event, "Failed replacement changed the removal subscription");
        require(graphics.generation() == generation && notifications == 0, "Failed replacement was published");
        wil::com_ptr<IUnknown> renderingDevice;
        THROW_IF_FAILED(graphics.composition_device().as<ABI::Windows::UI::Composition::ICompositionGraphicsDeviceInterop>()
            ->GetRenderingDevice(renderingDevice.put()));
        require(renderingDevice.query<IUnknown>().get() == d2d.get(), "Composition was switched before commit");
    }
    const auto previousContext = graphics.d2d_context().get();
    graphics.recreate();
    require(notifications == 1 && graphics.generation() == 2, "Recovery did not notify its consumer");
    require(graphics.d2d_context().get() != previousContext, "Recovery kept the old Direct2D context");
    require_consistent(graphics);
    connection.disconnect();
    graphics.recreate();
    require(notifications == 1, "Disconnected recovery callback was invoked");
    app.close();
    return 0;
}

int repeated_recovery(const testing::Options&) {
    Application app{true};
    const auto first = app.graphics().generation();
    unsigned notifications{};
    auto connection = app.graphics().on_recreated([&](auto generation) {
        ++notifications;
        require(generation == first + notifications, "Consumer saw an inconsistent generation");
        require(app.graphics().d3d_device()->GetDeviceRemovedReason() == S_OK, "Replacement device is unhealthy");
    });
    for (int index = 0; index != 8; ++index) {
        int attempts{};
        app.render([&] { if (++attempts == 1) { throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); } });
        require(attempts == 2, "Recovery did not retry drawing exactly once");
    }
    require(notifications == 8, "Repeated recovery lost notifications");
    for (const auto error : {DXGI_ERROR_DEVICE_RESET, DXGI_ERROR_DEVICE_HUNG, D2DERR_RECREATE_TARGET}) {
        int attempts{};
        app.render([&] { if (++attempts == 1) { THROW_HR(error); } });
        require(attempts == 2, "A device loss reported through WIL was not retried");
    }
    require(!app.graphics().is_device_loss(E_FAIL) && !app.graphics().is_device_loss(E_OUTOFMEMORY), "An ordinary error counted as device loss");
    app.close();
    return 0;
}

int failed_recovery(const testing::Options&) {
    Application app{true};
    auto generation = app.graphics().generation();
    int attempts{};
    require(testing::rejects(DXGI_ERROR_DEVICE_REMOVED, [&] { app.render([&] { ++attempts; throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); }); }) &&
        attempts == 2 && app.graphics().generation() == generation + 1, "Persistent device loss did not stop after one recovery");
    generation = app.graphics().generation();
    detail::failurePoint = detail::FailurePoint::removalRegistered;
    require(testing::rejects(E_OUTOFMEMORY, [&] { app.render([] { throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); }); }) &&
        app.graphics().generation() == generation, "Failed recovery corrupted published state");
    bool failed{};
    try { app.render([] { throw std::runtime_error("application error"); }); }
    catch (const std::runtime_error& error) { failed = std::string_view{error.what()} == "application error"; }
    require(failed && app.graphics().generation() == generation, "Unrelated exception triggered device recovery");
    app.close();
    require(testing::throws<std::logic_error>([&] { app.render([] {}); }), "A closed application accepted drawing");
    require(testing::throws<std::logic_error>([&] { (void)app.graphics(); }), "A closed application returned its graphics");
    return 0;
}

// One Application after another on the same thread: after a normal shutdown, and after a failure
// while one was being built, which undoes what was built.
int sequential(const testing::Options&) {
    const auto use = [](Application& app) {
        require(app.graphics().generation() == 1, "A later application did not start");
        {
            Window window{app, L"A later application", 100, 100};
            require(window.hwnd() != nullptr && app.post([] {}), "A later application does not work");
        }
        app.close();
    };
    for (int round = 0; round != 2; ++round) {
        Application app{true};
        use(app);
    }
    detail::failurePoint = detail::FailurePoint::deviceCreated;
    require(testing::rejects(E_OUTOFMEMORY, [] { Application failed{true}; }), "A failure while building the application was not reported");
    Application app{true};
    use(app);
    return 0;
}

// Without a hardware device, the application runs on WARP and reports why.
int hardware_fallback(const testing::Options&) {
    std::vector<std::string> events;
    set_log_handler([&](LogLevel level, std::string_view message) {
        events.push_back((level == LogLevel::warning ? "warning " : "info ") + std::string{message});
    });
    const auto restore = wil::scope_exit([] { set_log_handler(nullptr); });
    detail::failurePoint = detail::FailurePoint::hardwareDevice;
    Application app{false};
    const auto logged = [&](std::string_view text) {
        return std::ranges::any_of(events, [&](const std::string& event) { return event == text; });
    };
    require(logged("warning event=hardware_device_unavailable hresult=0x887A0004 fallback=warp"), "The hardware failure was not reported");
    require(logged("info event=graphics_device_created generation=1 driver=warp"), "The application did not fall back to WARP");
    require(app.graphics().d3d_device()->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0, "The fallback device has no usable feature level");
    app.graphics().recreate();  // Each replacement tries hardware first again.
    require(app.graphics().generation() == 2, "Replacing the fallback device failed");
    app.close();
    return 0;
}

int shutdown(const testing::Options&) {
    Application app{true};
    bool canceledCallbackRan{};
    bool rawCallbackHadGraphics{};
    require(app.post([&] { canceledCallbackRan = true; }), "Could not post application callback");
    require(app.dispatcher().DispatcherQueue().TryEnqueue([&] {
        try { rawCallbackHadGraphics = app.graphics().d3d_device()->GetDeviceRemovedReason() == S_OK; }
        catch (...) {}
    }), "Could not post dispatcher callback");
    app.close();
    require(!canceledCallbackRan, "A canceled application callback ran during shutdown");
    require(rawCallbackHadGraphics, "Graphics were destroyed before dispatcher work drained");
    require(!app.post([] {}), "Closed application accepted work");
    app.close();
    require(testing::throws<std::logic_error>([&] { (void)app.run(); }), "A closed application ran its loop");
    return 0;
}

int callback_error(const testing::Options&) {
    Application app{true};
    {
        Window window{app, L"Callback exception test", 320, 240};
        require(app.post([] { throw std::runtime_error("posted callback error"); }), "Could not enqueue callback");
        bool propagated{};
        try { (void)app.run(); }
        catch (const std::runtime_error& error) { propagated = std::string_view{error.what()} == "posted callback error"; }
        require(propagated, "Posted callback error did not reach run()");
        require(testing::throws<std::logic_error>([&] { app.close(); }), "close() accepted a live window");
    }
    app.close();
    return 0;
}

// Composia's objects belong to the thread that created the Application.
int wrong_thread(const testing::Options&) {
    Application app{true};
    {
        Window window{app, L"Thread test", 320, 240};
        bool rejected{}, posted{};
        std::thread other([&] {
            rejected = testing::throws<std::logic_error>([&] { (void)app.graphics(); }) &&
                testing::throws<std::logic_error>([&] { window.invalidate(); }) &&
                testing::throws<std::logic_error>([&] { Window stray{app, L"Stray", 10, 10}; });
            posted = app.post([&] { DestroyWindow(window.hwnd()); });
        });
        other.join();
        require(rejected, "A call from another thread was accepted");
        require(posted && app.run() == 0, "Work posted from another thread did not run");
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"transaction", transaction},
        {"repeated", repeated_recovery},
        {"failed", failed_recovery},
        {"sequential", sequential},
        {"hardware-fallback", hardware_fallback},
        {"shutdown", shutdown},
        {"callback-error", callback_error},
        {"wrong-thread", wrong_thread},
    });
}
