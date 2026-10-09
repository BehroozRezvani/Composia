#include "MediaDemoWindow.hpp"
#include "../DemoLog.hpp"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    try {
        demo::open_log(L"composia-media.log");
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
    } catch (...) { demo::log_fatal(); }
    MessageBoxW(nullptr, L"The media demo could not continue. See composia-media.log for details.", L"Composia", MB_OK | MB_ICONERROR);
    return 1;
}
