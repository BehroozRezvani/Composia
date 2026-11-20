#include "Editor.hpp"
#include <composia/Application.hpp>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    try {
        composia::Application app;
        int code{};
        {
            editor::Editor window{app};
            int argc{};
            wil::unique_hlocal_ptr<wchar_t*> args{CommandLineToArgvW(GetCommandLineW(), &argc)};
            THROW_LAST_ERROR_IF(!args);
            if (argc == 2) { window.open(args.get()[1]); }
            window.show(show);
            code = app.run();
        }
        app.close();
        return code;
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "Composia Editor", MB_OK | MB_ICONERROR);
        return 1;
    }
}
