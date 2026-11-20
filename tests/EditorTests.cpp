#include "support/TestSupport.hpp"
#include "../examples/editor/Commands.h"
#include "../examples/editor/Editor.hpp"
#include <fstream>

using composia::testing::require;

int main(int argc, char** argv) {
    return composia::testing::run(argc, argv, {{"document", [](const auto& options) {
        GUID id{};
        THROW_IF_FAILED(CoCreateGuid(&id));
        wchar_t name[40]{};
        StringFromGUID2(id, name, 40);
        const auto path = std::filesystem::temp_directory_path() / (std::wstring(name) + L".txt");
        const auto cleanup = wil::scope_exit([&] { DeleteFileW(path.c_str()); });
        const std::string original = "\xef\xbb\xbf" "Café 東京\nSecond line\n";
        std::ofstream(path, std::ios::binary) << original;
        composia::Application app{options.warp};
        {
            editor::Editor window{app};
            window.open(path);
            window.show(SW_SHOWNOACTIVATE);
            UpdateWindow(window.hwnd());
            require(GetMenuItemCount(GetMenu(window.hwnd())) == 2, "File and Edit menus must be attached");
            auto& field = dynamic_cast<composia::TextBox&>(window.elements().root());
            require(field.text() == L"Café 東京\rSecond line\r", "UTF-8 import or newline handling failed");
            window.focus_element(field);
            SendMessageW(window.hwnd(), WM_CHAR, L'!', 1);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { DispatchMessageW(&message); }
            wchar_t title[128]{};
            GetWindowTextW(window.hwnd(), title, 128);
            require(title[0] == L'*', "Typing must mark the document modified");
            SendMessageW(window.hwnd(), WM_COMMAND, FILE_SAVE, 0);
            std::ifstream saved(path, std::ios::binary);
            std::string bytes{std::istreambuf_iterator<char>{saved}, {}};
            require(bytes == original.substr(0, 3) + "!" + original.substr(3), "Save must retain BOM, Unicode and LF");
            saved.close();
            GetWindowTextW(window.hwnd(), title, 128);
            require(title[0] != L'*', "Successful save must clear the modified flag");
            SendMessageW(window.hwnd(), WM_COMMAND, FILE_NEW, 0);
            require(field.text().empty(), "New must clear the document");
            std::ofstream(path, std::ios::binary) << "\xff";
            require(composia::testing::throws<std::runtime_error>([&] { window.open(path); }), "Invalid UTF-8 accepted");
            require(field.text().empty(), "A failed open changed the document");
        }
        app.close();
        return 0;
    }}});
}
