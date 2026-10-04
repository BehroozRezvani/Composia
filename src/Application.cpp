#include <composia/Application.hpp>
#include <DispatcherQueue.h>
#include <spdlog/spdlog.h>

namespace composia {

Application::Application(bool forceWarp) {
    const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
    THROW_IF_FAILED(CreateDispatcherQueueController(options,
        reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(dispatcher_))));
    try {
        compositor_ = composition::Compositor{};
        graphics_ = std::make_unique<GraphicsDevice>(compositor_, forceWarp);
    } catch (...) {
        shutdown();
        throw;
    }
}

Application::~Application() { shutdown(); }

void Application::shutdown() noexcept {
    try {
        graphics_.reset();
        if (compositor_) {
            compositor_.Close();
            compositor_ = nullptr;
        }
        if (dispatcher_) {
            const auto shutdown = dispatcher_.ShutdownQueueAsync();
            while (shutdown.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
                const auto wait = MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                THROW_LAST_ERROR_IF(wait == WAIT_FAILED);
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT) {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
            }
            shutdown.GetResults();
        }
        spdlog::info("event=application_shutdown");
    } catch (...) {
        spdlog::error("event=shutdown_failed hresult=0x{:08X}", static_cast<unsigned>(wil::ResultFromCaughtException()));
    }
}

void Application::render(const std::function<void()>& draw) {
    for (int attempt = 0; attempt != 2; ++attempt) {
        try {
            draw();
            return;
        } catch (...) {
            const auto error = wil::ResultFromCaughtException();
            if (attempt != 0 || !graphics().is_device_loss(error)) {
                throw;
            }
            spdlog::warn("event=draw_device_loss hresult=0x{:08X}", static_cast<unsigned>(error));
            graphics().recreate();
        }
    }
}

int Application::run(Window& window, const std::function<void()>& redraw) {
    for (;;) {
        window.rethrow_callback_error();
        const HANDLE handles[]{graphics().removed_event()};
        const auto wait = MsgWaitForMultipleObjectsEx(1, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        THROW_LAST_ERROR_IF(wait == WAIT_FAILED);
        if (wait == WAIT_OBJECT_0) {
            spdlog::warn("event=device_removed hresult=0x{:08X}",
                static_cast<unsigned>(graphics().d3d_device()->GetDeviceRemovedReason()));
            graphics().recreate();
            render(redraw);
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                window.rethrow_callback_error();
                return static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
            window.rethrow_callback_error();
        }
    }
}

}
