#include <composia/Application.hpp>
#include <windows.ui.composition.interop.h>
#include "FailureInjection.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

void transaction() {
    composia::Application app{true};
    auto& graphics = app.graphics();
    unsigned notifications{};
    auto connection = graphics.on_recreated([&](auto) { ++notifications; });
    for (auto point : {composia::detail::FailurePoint::deviceCreated,
                      composia::detail::FailurePoint::removalRegistered,
                      composia::detail::FailurePoint::compositionSwitch}) {
        const auto device = graphics.d3d_device();
        const auto d2d = graphics.d2d_device().query<IUnknown>();
        const auto event = graphics.removed_event();
        const auto generation = graphics.generation();
        composia::detail::failurePoint = point;
        bool failed{};
        try { graphics.recreate(); }
        catch (const winrt::hresult_error& error) { failed = error.code() == E_OUTOFMEMORY; }
        require(failed, "Device replacement did not expose the injected failure");
        require(graphics.d3d_device().get() == device.get(), "Failed replacement changed the D3D device");
        require(graphics.removed_event() == event, "Failed replacement changed the removal subscription");
        require(graphics.generation() == generation && notifications == 0, "Failed replacement was published");
        wil::com_ptr<IUnknown> renderingDevice;
        THROW_IF_FAILED(graphics.composition_device().as<ABI::Windows::UI::Composition::ICompositionGraphicsDeviceInterop>()
            ->GetRenderingDevice(renderingDevice.put()));
        require(renderingDevice.query<IUnknown>().get() == d2d.get(), "Composition was switched before commit");
    }
    graphics.recreate();
    require(notifications == 1, "Recovery did not notify its consumer");
    connection.disconnect();
    graphics.recreate();
    require(notifications == 1, "Disconnected recovery callback was invoked");
    app.close();
}

void repeated_recovery() {
    composia::Application app{true};
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
    app.close();
}

void failed_recovery() {
    composia::Application app{true};
    auto generation = app.graphics().generation();
    int attempts{};
    bool failed{};
    try { app.render([&] { ++attempts; throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); }); }
    catch (const winrt::hresult_error& error) { failed = error.code() == DXGI_ERROR_DEVICE_REMOVED; }
    require(failed && attempts == 2 && app.graphics().generation() == generation + 1,
        "Persistent device loss did not stop after one recovery");
    generation = app.graphics().generation();
    composia::detail::failurePoint = composia::detail::FailurePoint::removalRegistered;
    failed = false;
    try { app.render([] { throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); }); }
    catch (const winrt::hresult_error& error) { failed = error.code() == E_OUTOFMEMORY; }
    require(failed && app.graphics().generation() == generation, "Failed recovery corrupted published state");
    failed = false;
    try { app.render([] { throw std::runtime_error("application error"); }); }
    catch (const std::runtime_error& error) { failed = std::string_view{error.what()} == "application error"; }
    require(failed && app.graphics().generation() == generation, "Unrelated exception triggered device recovery");
    app.close();
}

void shutdown() {
    composia::Application app{true};
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
}

void callback_error() {
    composia::Application app{true};
    {
        composia::Window window{app, L"Callback exception test", 320, 240};
        require(app.post([] { throw std::runtime_error("posted callback error"); }), "Could not enqueue callback");
        bool propagated{};
        try { (void)app.run(); }
        catch (const std::runtime_error& error) { propagated = std::string_view{error.what()} == "posted callback error"; }
        require(propagated, "Posted callback error did not reach run()");
    }
    app.close();
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a test name");
        const std::string_view name{argv[1]};
        if (name == "transaction") { transaction(); }
        else if (name == "repeated-recovery") { repeated_recovery(); }
        else if (name == "failed-recovery") { failed_recovery(); }
        else if (name == "shutdown") { shutdown(); }
        else if (name == "callback-error") { callback_error(); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::cerr << winrt::to_string(error.message()) << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
