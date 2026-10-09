#include "AppPackager.hpp"
#include "FormPlayerWindow.hpp"
#include <shellapi.h>
#include <string>
#include <string_view>
#include <vector>

// The form player: a generic app whose UI is the design embedded in its own executable by the
// UI builder. It also opens a .cui design given on the command line, and `--validate` loads the
// design and exits with 0 on success without showing a window.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    bool warp{}, validate{};
    std::wstring path;
    int count{};
    if (const auto arguments = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int index = 1; index < count; ++index) {
            const std::wstring_view argument{arguments[index]};
            if (argument == L"--warp") { warp = true; }
            else if (argument == L"--validate") { validate = true; }
            else { path = argument; }
        }
        LocalFree(arguments);
    }
    const auto complain = [&](const std::wstring& message) {
        if (!validate) { MessageBoxW(nullptr, message.c_str(), L"Composia form", MB_OK | MB_ICONERROR); }
        return 1;
    };
    try {
        const auto text = path.empty() ? builder::embedded_design() : builder::read_file(path);
        if (!text) {
            return complain(path.empty() ? L"This player has no form embedded. Build an app from the Composia UI builder, or pass a .cui design on the command line."
                                         : L"Could not read " + path);
        }
        auto document = builder::Document::from_text(*text);
        if (!document) { return complain(path.empty() ? L"The embedded form is not a valid Composia UI design." : path + L" is not a Composia UI design."); }
        if (validate) {
            (void)document->resolve();
            return 0;
        }
        composia::Application app{warp};
        int result{};
        {
            builder::FormPlayerWindow window{app, std::move(*document)};
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const winrt::hresult_error& error) {
        return complain(L"The form could not start: " + std::wstring{error.message()});
    } catch (const std::exception& error) {
        return complain(L"The form could not start: " + builder::widen(error.what()));
    } catch (...) {
        return complain(L"The form could not start.");
    }
}
