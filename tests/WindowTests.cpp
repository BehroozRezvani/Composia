#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/NativeControl.hpp>
#include "support/TestSupport.hpp"
#include <functional>
#include <stdexcept>
#include <string_view>
#include <vector>

// Window and application lifetime: several top-level windows, closed windows and parents, the
// message loop's exit code, and errors escaping message handlers.
using namespace composia;
using testing::require;

namespace {
constexpr UINT actionMessage = WM_APP + 1;
const HRESULT invalidHandle = HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

class TestWindow final : public Window {
public:
    explicit TestWindow(Application& app) : Window(app, L"Window lifetime test", 320, 240) {}
    std::function<void()> action;
    std::function<void()> timer;
    std::function<void()> paint;

private:
    void on_paint() override { if (paint) { paint(); } }
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
        if (message == actionMessage && action) { action(); return 0; }
        if (message == WM_TIMER && timer) { timer(); return 0; }
        return std::nullopt;
    }
};

int closed_window(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
        require(!window.hwnd() && window.dpi() == 0 && window.scale() == 1.0f, "Destroyed HWND was retained");
        require(!window.focused() && !window.enabled() && !window.hovered(), "A destroyed window reported live state");
        const auto rejects = [&](auto&& operation) { return testing::rejects(invalidHandle, operation); };
        require(rejects([&] { window.invalidate(); }), "Closed window accepted invalidation");
        require(rejects([&] { window.invalidate({0, 0, 10, 10}); }), "Closed window accepted area invalidation");
        require(rejects([&] { window.show(); }), "Closed window accepted showing");
        require(rejects([&] { window.set_bounds({0, 0, 100, 100}); }), "Closed window accepted bounds");
        require(rejects([&] { (void)window.client_pixels(); }), "Closed window returned client pixels");
        require(rejects([&] { (void)window.client_bounds(); }), "Closed window returned client bounds");
        require(rejects([&] { window.focus(); }), "Closed window accepted focus");
        require(rejects([&] { window.set_enabled(false); }), "Closed window accepted an enabled-state change");
        require(rejects([&] { window.capture_pointer(); }), "Closed window captured the pointer");
        window.release_pointer();  // Harmless without a capture.
        require(app.run() == 0, "A closed window kept the application running");
    }
    app.close();
    return 0;
}

int closed_parent(const testing::Options&) {
    Application app{true};
    {
        TestWindow parent{app};
        Button button{parent, L"Child"};
        require(!button.top_level() && GetParent(button.hwnd()) == parent.hwnd(), "Button was not created as a child");
        require(&button.application() == &app, "The child does not report its application");
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(parent.hwnd()));
        require(!parent.hwnd() && !button.hwnd() && !button.enabled(), "Parent destruction left a live child");
        const auto rejects = [&](auto&& operation) { return testing::rejects(invalidHandle, operation); };
        require(rejects([&] { button.invalidate(); }), "Destroyed child accepted invalidation");
        require(rejects([&] { button.set_enabled(true); }), "Destroyed child accepted an enabled-state change");
        require(rejects([&] { button.invoke(); }), "Destroyed child accepted invocation");
        require(rejects([&] { Button orphan{parent, L"Orphan"}; }), "Button accepted a destroyed parent");
        require(app.run() == 0, "Rejected child creation left a top-level window");
    }
    app.close();
    return 0;
}

int secondary_close(const testing::Options&) {
    Application app{true};
    TestWindow primary{app};
    TestWindow secondary{app};
    require(primary.top_level() && secondary.top_level(), "Windows without a parent are not top-level");
    bool primaryClosed = false;
    primary.timer = [&] {
        KillTimer(primary.hwnd(), 1);
        primaryClosed = true;
        DestroyWindow(primary.hwnd());
    };
    THROW_LAST_ERROR_IF(SetTimer(primary.hwnd(), 1, 40, nullptr) == 0);
    THROW_IF_WIN32_BOOL_FALSE(PostMessageW(secondary.hwnd(), WM_CLOSE, 0, 0));
    require(app.run() == 0, "Unexpected exit code");
    require(primaryClosed, "Closing a secondary window terminated the application");
    return 0;
}

int secondary_error(const testing::Options&) {
    Application app{true};
    TestWindow primary{app};
    TestWindow secondary{app};
    secondary.action = [] { throw std::runtime_error("secondary callback error"); };
    THROW_IF_WIN32_BOOL_FALSE(PostMessageW(secondary.hwnd(), actionMessage, 0, 0));
    bool propagated = false;
    try { (void)app.run(); }
    catch (const std::runtime_error& error) { propagated = std::string_view(error.what()) == "secondary callback error"; }
    require(propagated, "The secondary window's exception was not propagated");
    return 0;
}

// WM_QUIT ends the loop with its exit code while windows are still open.
int quit_code(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        window.action = [] { PostQuitMessage(7); };
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), actionMessage, 0, 0));
        require(app.run() == 7, "run() did not return the WM_QUIT exit code");
        require(window.hwnd() != nullptr, "WM_QUIT destroyed the window");
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), WM_CLOSE, 0, 0));
        require(app.run() == 0, "The loop did not resume after WM_QUIT");
    }
    app.close();
    return 0;
}

// An exception from a handler that ran outside the loop, such as a paint forced by UpdateWindow,
// stays with the window for rethrow_callback_error, and reaches the application's run().
int callback_rethrow(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        window.rethrow_callback_error();  // Nothing failed yet.
        window.paint = [] { throw std::runtime_error("paint failure"); };
        window.show(SW_SHOWNOACTIVATE);
        window.invalidate();
        UpdateWindow(window.hwnd());
        const auto rethrows = [&] {
            try { window.rethrow_callback_error(); }
            catch (const std::runtime_error& error) { return std::string_view{error.what()} == "paint failure"; }
            return false;
        };
        require(rethrows() && rethrows(), "The window did not keep its handler's exception");
        window.paint = {};
        require(testing::throws<std::runtime_error>([&] { (void)app.run(); }), "The handler's exception did not reach run()");
        DestroyWindow(window.hwnd());
    }
    app.close();
    return 0;
}
}

// Key presses reach the previews of the target window and its ancestors, innermost first,
// before the dialog manager and the control; a consumed key produces no character.
int key_preview(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        Window pane{app, L"Pane", 200, 100, window.hwnd()};
        NativeControl edit{pane, L"EDIT", WS_VISIBLE | WS_TABSTOP};
        std::vector<std::pair<char, unsigned>> seen;
        bool control{};
        auto inner = pane.on_key_preview([&](const KeyEvent& key) {
            seen.emplace_back('p', key.key);
            return key.key == 'A';
        });
        const auto text = [&] {
            wchar_t buffer[16]{};
            GetWindowTextW(edit.hwnd(), buffer, 16);
            return std::wstring{buffer};
        };
        auto outer = window.on_key_preview([&](const KeyEvent& key) {
            seen.emplace_back('w', key.key);
            if (key.key != 'N') { return false; }
            control = key.control;
            return true;
        });
        const auto press = [&](unsigned key) {
            const auto scan = static_cast<LPARAM>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
            THROW_IF_WIN32_BOOL_FALSE(PostMessageW(edit.hwnd(), WM_KEYDOWN, key, 1 | (scan << 16)));
        };
        const auto hold_control = [](bool held) {
            BYTE keys[256]{};
            THROW_IF_WIN32_BOOL_FALSE(GetKeyboardState(keys));
            keys[VK_CONTROL] = held ? 0x80 : 0;
            THROW_IF_WIN32_BOOL_FALSE(SetKeyboardState(keys));
        };
        int step{};
        window.action = [&] {
            if (step++ == 0) { hold_control(false); return; }
            // TranslateMessage queues the character behind this message; look after it arrives.
            if (step == 2) { THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), actionMessage, 0, 0)); return; }
            require(text() == L"b", "A consumed key produced a character, or a free one none");
            THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
        };
        window.show(SW_SHOWNOACTIVATE);
        hold_control(true);
        press('N');
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), actionMessage, 0, 0));
        press('A');
        press('B');
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), actionMessage, 0, 0));
        require(app.run() == 0, "Message loop");
        const std::vector<std::pair<char, unsigned>> expected{{'p', 'N'}, {'w', 'N'}, {'p', 'A'}, {'p', 'B'}, {'w', 'B'}};
        require(seen == expected, "Previews ran out of order, or after a consuming handler");
        require(control, "The preview did not report the held Ctrl key");
        hold_control(false);
    }
    app.close();
    return 0;
}

// A dialog made with its owner and style at creation, centred on its owner, which a ModalScope
// disables while it is open.
int dialog_options(const testing::Options&) {
    Application app;
    {
        TestWindow owner{app};
        owner.show(SW_SHOWNORMAL);
        Window::Options options;
        options.style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        options.extendedStyle = WS_EX_TOOLWINDOW;
        options.owner = owner.hwnd();
        Window dialog{app, L"Dialog", 160, 100, options};
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(dialog.hwnd(), GWL_STYLE));
        const auto extended = static_cast<DWORD>(GetWindowLongPtrW(dialog.hwnd(), GWL_EXSTYLE));
        require(dialog.top_level() && GetWindow(dialog.hwnd(), GW_OWNER) == owner.hwnd(), "The owner was not set at creation");
        require((style & WS_POPUP) && !(style & WS_THICKFRAME) && (extended & WS_EX_TOOLWINDOW) && (extended & WS_EX_CONTROLPARENT),
            "The styles were not the ones asked for, with what the window needs");
        dialog.center_on(owner.hwnd());
        RECT a{}, b{};
        GetWindowRect(owner.hwnd(), &a);
        GetWindowRect(dialog.hwnd(), &b);
        const auto middle = [](const RECT& r) { return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; };
        require(std::abs(middle(a).x - middle(b).x) <= 1 && std::abs(middle(a).y - middle(b).y) <= 1, "center_on did not centre the dialog");
        {
            ModalScope modal{owner.hwnd()};
            require(modal.active() && !IsWindowEnabled(owner.hwnd()), "ModalScope did not disable the owner");
            ModalScope moved = std::move(modal);
            require(!modal.active() && moved.active() && !IsWindowEnabled(owner.hwnd()), "Moving the scope enabled the owner");
        }
        require(IsWindowEnabled(owner.hwnd()) != FALSE, "The scope's end left the owner disabled");
        EnableWindow(owner.hwnd(), FALSE);
        {
            ModalScope modal{owner.hwnd()};
            require(!modal.active(), "A scope took over an owner disabled already");
        }
        require(!IsWindowEnabled(owner.hwnd()), "A scope enabled an owner it had not disabled");
        EnableWindow(owner.hwnd(), TRUE);
        // A child keeps WS_CHILD whatever style it is given.
        Window::Options childOptions;
        childOptions.style = WS_VISIBLE;
        Window child{app, L"Child", 20, 20, childOptions, owner.hwnd()};
        require(!child.top_level() && (GetWindowLongPtrW(child.hwnd(), GWL_STYLE) & WS_CHILD), "A child lost WS_CHILD");
    }
    app.close();
    return 0;
}

// A window retired from inside its own handler lives until that handler returns, a modal loop it
// ran included; a window retired at any other time goes at once; close() takes those left.
int retire(const testing::Options&) {
    Application app{true};
    struct Watched final : Window {
        explicit Watched(Application& application, bool& destroyed) : Window(application, L"Retired", 100, 100), destroyed_(destroyed) {}
        ~Watched() override { destroyed_ = true; }
        std::function<void()> action;
        bool& destroyed_;
        std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
            if (message == actionMessage && action) { action(); return 0; }
            return std::nullopt;
        }
    };
    bool destroyed{};
    auto window = std::make_unique<Watched>(app, destroyed);
    auto* raw = window.get();
    bool aliveAfterRetire{}, dispatching{};
    raw->action = [&] {
        dispatching = raw->dispatching();
        app.retire(std::move(window));
        // A nested loop, as a message box would run: the window must outlive it.
        SendMessageW(raw->hwnd(), WM_NULL, 0, 0);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { DispatchMessageW(&message); }
        aliveAfterRetire = !destroyed && raw->hwnd() != nullptr;
    };
    SendMessageW(raw->hwnd(), actionMessage, 0, 0);
    require(dispatching && aliveAfterRetire, "A window retired by its own handler did not outlive the handler");
    require(destroyed, "A retired window was not destroyed once its handler returned");

    bool idleDestroyed{};
    auto idle = std::make_unique<Watched>(app, idleDestroyed);
    require(!idle->dispatching(), "An idle window said it was dispatching");
    app.retire(std::move(idle));
    require(idleDestroyed, "A window retired while idle was not destroyed at once");

    // A window still waiting when the application closes goes with it.
    bool lateDestroyed{};
    auto late = std::make_unique<Watched>(app, lateDestroyed);
    auto* lateRaw = late.get();
    lateRaw->action = [&] {
        app.retire(std::move(late));
        app.close();
    };
    SendMessageW(lateRaw->hwnd(), actionMessage, 0, 0);
    require(lateDestroyed, "close() did not destroy a window that was waiting to be retired");
    return 0;
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"retire", retire},
        {"dialog-options", dialog_options},
        {"closed", closed_window},
        {"closed-parent", closed_parent},
        {"secondary-close", secondary_close},
        {"secondary-error", secondary_error},
        {"quit-code", quit_code},
        {"callback-rethrow", callback_rethrow},
        {"key-preview", key_preview},
    });
}
