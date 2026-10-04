#include "MediaDemoWindow.hpp"
#include <shellapi.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    try {
        spdlog::set_default_logger(spdlog::basic_logger_mt("composia-media", "composia-media.log", true));
        spdlog::flush_on(spdlog::level::info);
        int argc{};
        const auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        THROW_LAST_ERROR_IF_NULL(argv);
        const auto cleanup = wil::scope_exit([&] { LocalFree(argv); });
        bool warp{};
        std::filesystem::path video;
        for (int index = 1; index < argc; ++index) {
            if (std::wstring_view{argv[index]} == L"--warp") { warp = true; }
            else { video = argv[index]; }
        }
        composia::Application app{warp};
        int result{};
        {
            MediaDemoWindow window{app};
            if (!video.empty()) { window.load_video(video); }
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const winrt::hresult_error& error) { spdlog::error("event=fatal message={}", winrt::to_string(error.message())); }
    catch (const std::exception& error) { spdlog::error("event=fatal message={}", error.what()); }
    MessageBoxW(nullptr, L"The media demo could not continue. See composia-media.log for details.", L"Composia", MB_OK | MB_ICONERROR);
    return 1;
}
