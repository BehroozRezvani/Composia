#include "support/TestSupport.hpp"
#include "../examples/editor/Commands.h"
#include "../examples/editor/Editor.hpp"
#include <fstream>

using composia::testing::require;

// SetMenu and GetMenuItemCount also succeed when a composition-only HWND has
// nowhere to paint its native menu. Check the actual label pixels in the frame.
static bool menu_labels_visible(HWND hwnd) {
    RECT frame{};
    THROW_IF_WIN32_BOOL_FALSE(GetWindowRect(hwnd, &frame));
    const wil::unique_hdc_window screen{GetDC(nullptr)};
    const wil::unique_hdc memory{CreateCompatibleDC(screen.get())};
    const wil::unique_hbitmap bitmap{CreateCompatibleBitmap(screen.get(), frame.right - frame.left, frame.bottom - frame.top)};
    require(memory && bitmap, "Could not allocate the editor frame capture");
    const auto selected = wil::SelectObject(memory.get(), bitmap.get());
    THROW_IF_WIN32_BOOL_FALSE(PrintWindow(hwnd, memory.get(), PW_RENDERFULLCONTENT));
    for (UINT item = 0; item < 2; ++item) {
        RECT bounds{};
        THROW_IF_WIN32_BOOL_FALSE(GetMenuItemRect(hwnd, GetMenu(hwnd), item, &bounds));
        OffsetRect(&bounds, -frame.left, -frame.top);
        InflateRect(&bounds, -2, -2);
        const auto background = GetPixel(memory.get(), bounds.left, bounds.top);
        require(background != CLR_INVALID, "Menu item is outside the editor frame");
        unsigned ink{};
        for (LONG y = bounds.top; y < bounds.bottom; ++y) {
            for (LONG x = bounds.left; x < bounds.right; ++x) {
                const auto pixel = GetPixel(memory.get(), x, y);
                if (pixel != CLR_INVALID && pixel != background) { ++ink; }
            }
        }
        if (ink < 8) { return false; }
    }
    return true;
}

static void check_first_menu_frame(composia::Application& app, HWND hwnd) {
    // The non-client frame is composed asynchronously after ShowWindow. Pump
    // queued paints while waiting, as the standalone editor's message loop does.
    const auto deadline = GetTickCount64() + 3000;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { DispatchMessageW(&message); }
        app.commit();
        if (menu_labels_visible(hwnd)) { return; }
        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < deadline);
    require(false, "The editor menu label did not paint");
}

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
            check_first_menu_frame(app, window.hwnd());
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
            std::ofstream(path, std::ios::binary) << "Draft";
            window.open(path);
            PostMessageW(window.hwnd(), WM_CHAR, L'A', 1);
            PostMessageW(window.hwnd(), WM_COMMAND, FILE_SAVE, 0);
            PostQuitMessage(0);
            require(app.run() == 0 && editor::read_document(path).text == L"ADraft",
                "A character routed through the application loop did not reach the saved file");
        }
        app.close();
        return 0;
    }}});
}
