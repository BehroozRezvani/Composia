#include "MailDemoWindow.hpp"
#include "../DemoLog.hpp"
#include <string_view>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int showCommand) {
    const std::wstring_view arguments{commandLine};
    const bool warp = arguments.find(L"--warp") != std::wstring_view::npos;
    try {
        demo::open_log(L"composia-mail.log");
        composia::Application app{warp};
        int result{};
        {
            MailDemoWindow window{app};
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (...) {
        demo::log_fatal();
    }
    MessageBoxW(nullptr, L"Composia could not continue. See composia-mail.log for details.", L"Composia", MB_OK | MB_ICONERROR);
    return 1;
}
