#include "DemoWindow.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <string_view>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int showCommand) {
    const std::wstring_view arguments{commandLine};
    const bool smoke = arguments.find(L"--smoke-test") != std::wstring_view::npos;
    const bool warp = arguments.find(L"--warp") != std::wstring_view::npos;
    try {
        spdlog::set_default_logger(spdlog::basic_logger_mt("composia",
            smoke ? (warp ? "smoke-warp.log" : "smoke-desktop.log") : "composia.log", true));
        spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e level=%l %v");
        spdlog::flush_on(spdlog::level::info);
        composia::Application app{warp};
        int result{};
        {
            DemoWindow window{app, smoke};
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const winrt::hresult_error& error) {
        spdlog::critical("event=fatal hresult=0x{:08X} message={}",
            static_cast<unsigned>(error.code().value), winrt::to_string(error.message()));
    } catch (const std::exception& error) {
        spdlog::critical("event=fatal message={}", error.what());
    } catch (...) {
        spdlog::critical("event=fatal message=unknown_exception");
    }
    if (!smoke) {
        MessageBoxW(nullptr, L"Composia could not continue. See composia.log for details.", L"Composia", MB_OK | MB_ICONERROR);
    }
    return 1;
}
