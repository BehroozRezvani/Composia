#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <UIAutomation.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
constexpr UINT checkMessage = WM_APP + 55;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

class Host final : public composia::Window {
public:
    explicit Host(composia::Application& app) : Window(app, L"Interaction test", 420, 260) {}
    std::function<void()> check;
private:
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
        if (message == checkMessage && check) { check(); return 0; }
        return std::nullopt;
    }
};

void input() {
    composia::Application app{true};
    {
        Host host{app};
        composia::Button first{host, L"Activate"};
        composia::Button second{host, L"Second"};
        const auto rectangles = composia::layout::stack({24, 24, 320, 160}, std::array{44.0f, 44.0f});
        first.set_bounds(rectangles[0]);
        second.set_bounds(rectangles[1]);
        unsigned clicks{};
        auto listener = first.on_click([&] { ++clicks; });
        wil::com_ptr<IRawElementProviderSimple> provider{first.automation_provider()};
        auto invoke = provider.query<IInvokeProvider>();
        host.show();
        require(app.post([&] {
            constexpr LPARAM inside = MAKELPARAM(12, 12);
            SendMessageW(first.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, inside);
            require(GetCapture() == first.hwnd(), "Pointer down did not capture the mouse");
            SendMessageW(first.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(-10, -10));
            require(clicks == 0 && GetCapture() != first.hwnd(), "Release outside activated the button or retained capture");
            SendMessageW(first.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, inside);
            SetCapture(second.hwnd());
            SendMessageW(first.hwnd(), WM_LBUTTONUP, 0, inside);
            require(clicks == 0, "Losing capture did not cancel a click");
            ReleaseCapture();
            SendMessageW(first.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, inside);
            SendMessageW(first.hwnd(), WM_LBUTTONUP, 0, inside);
            require(clicks == 1, "Pointer activation did not invoke once");
            SetFocus(first.hwnd());
            SendMessageW(first.hwnd(), WM_KEYDOWN, VK_SPACE, 0);
            SendMessageW(first.hwnd(), WM_KEYDOWN, VK_SPACE, 1LL << 30);
            require(clicks == 1, "Space activated before key release");
            SendMessageW(first.hwnd(), WM_KEYUP, VK_SPACE, 0);
            require(clicks == 2, "Space activation did not invoke once");
            SendMessageW(first.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
            SendMessageW(first.hwnd(), WM_KEYDOWN, VK_RETURN, 1LL << 30);
            require(clicks == 3, "Enter auto-repeat invoked more than once");
            SendMessageW(first.hwnd(), WM_KEYDOWN, VK_SPACE, 0);
            SetFocus(second.hwnd());
            SendMessageW(first.hwnd(), WM_KEYUP, VK_SPACE, 0);
            require(clicks == 3, "Focus loss did not cancel keyboard activation");
            first.set_enabled(false);
            require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), "Disabled UIA Invoke was accepted");
            SendMessageW(first.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, inside);
            SendMessageW(first.hwnd(), WM_LBUTTONUP, 0, inside);
            require(clicks == 3, "Disabled pointer activation was accepted");
            first.set_enabled(true);
            EnableWindow(host.hwnd(), FALSE);
            require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), "Disabled parent did not prevent UIA invocation");
            EnableWindow(host.hwnd(), TRUE);
            SetFocus(first.hwnd());
            host.check = [&] {
                require(GetFocus() == second.hwnd(), "Tab navigation did not advance to the next control");
                DestroyWindow(first.hwnd());
                require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "A retained provider invoked a destroyed control");
                PostMessageW(host.hwnd(), WM_CLOSE, 0, 0);
            };
            PostMessageW(first.hwnd(), WM_KEYDOWN, VK_TAB, 0);
            PostMessageW(host.hwnd(), checkMessage, 0, 0);
        }), "Could not schedule input checks");
        require(app.run() == 0, "Interaction loop failed");
    }
    app.close();
}

void accessibility() {
    composia::Application app{true};
    {
        Host host{app};
        composia::Button button{host, L"Accessible action"};
        button.set_bounds({24, 24, 240, 48});
        unsigned clicks{};
        auto connection = button.on_click([&] { ++clicks; });
        host.show();
        SetFocus(button.hwnd());
        const auto buttonHwnd = button.hwnd();
        const auto hostHwnd = host.hwnd();
        std::exception_ptr workerError;
        std::jthread client([&] {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                const auto uninit = wil::scope_exit([] { winrt::uninit_apartment(); });
                wil::com_ptr<IUIAutomation> automation;
                THROW_IF_FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(automation.put())));
                wil::com_ptr<IUIAutomationElement> element;
                THROW_IF_FAILED(automation->ElementFromHandle(buttonHwnd, element.put()));
                wil::unique_bstr name;
                THROW_IF_FAILED(element->get_CurrentName(name.put()));
                require(std::wstring_view{name.get()} == L"Accessible action", "UIA could not discover the button name");
                CONTROLTYPEID type{};
                THROW_IF_FAILED(element->get_CurrentControlType(&type));
                require(type == UIA_ButtonControlTypeId, "UIA reported the wrong control type");
                BOOL focusable{};
                THROW_IF_FAILED(element->get_CurrentIsKeyboardFocusable(&focusable));
                require(focusable != FALSE, "UIA did not expose keyboard focus support");
                wil::com_ptr<IUIAutomationInvokePattern> pattern;
                THROW_IF_FAILED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(pattern.put())));
                THROW_IF_FAILED(pattern->Invoke());
                require(app.post([&] {
                    require(clicks == 1, "UIA Invoke did not execute on the UI thread");
                    PostMessageW(hostHwnd, WM_CLOSE, 0, 0);
                }), "Could not schedule UIA completion check");
            } catch (...) {
                workerError = std::current_exception();
                PostMessageW(hostHwnd, WM_CLOSE, 0, 0);
            }
        });
        require(app.run() == 0, "Accessibility loop failed");
        client.join();
        if (workerError) { std::rethrow_exception(workerError); }
    }
    app.close();
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a test name");
        if (std::string_view{argv[1]} == "input") { input(); }
        else { accessibility(); }
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
