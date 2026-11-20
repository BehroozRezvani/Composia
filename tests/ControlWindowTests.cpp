#include "../examples/controls/Playback.hpp"
#include "support/TestSupport.hpp"
#include <thread>

using namespace composia;
using testing::require;

namespace {
void flush(ElementWindow& window) {
    MSG message{};
    while (PeekMessageW(&message, window.hwnd(), 0xC000, 0xFFFF, PM_REMOVE)) {
        DispatchMessageW(&message);
    }
    UpdateWindow(window.hwnd());
    window.rethrow_callback_error();
}
using Node = wil::com_ptr<IUIAutomationElement>;
Node named(IUIAutomation* client, IUIAutomationElement* parent, const wchar_t* name) {
    VARIANT v{};
    v.vt = VT_BSTR;
    wil::unique_bstr label{SysAllocString(name)};
    v.bstrVal = label.get();
    wil::com_ptr<IUIAutomationCondition> condition;
    THROW_IF_FAILED(client->CreatePropertyCondition(UIA_NamePropertyId, v, condition.put()));
    Node result;
    THROW_IF_FAILED(parent->FindFirst(TreeScope_Descendants, condition.get(), result.put()));
    require(!!result, "Control missing from UI Automation");
    return result;
}
template <class T> wil::com_ptr<T> pattern(IUIAutomationElement* node, PATTERNID id) {
    wil::com_ptr<T> result;
    THROW_IF_FAILED(node->GetCurrentPatternAs(id, IID_PPV_ARGS(result.put())));
    require(!!result, "Missing control pattern");
    return result;
}

int interaction(const testing::Options& options) {
    Application app(options.warp);
    playback::Playback panel(app);
    ElementWindow window(app, L"Playback controls", 344, 184, panel.root);
    window.show(SW_SHOWNOACTIVATE);
    require(panel.output->request_focus(), "Initial focus");
    flush(window);
    auto pixel = [&](float x, float y) {
        return testing::surface_pixel(app, window.composition_target().surface(), LONG(x * window.scale()),
                                      LONG(y * window.scale()));
    };
    require(testing::matches(pixel(28, 55), 0x234043), "Custom dropdown face");
    auto hwnd = window.hwnd();
    SendMessageW(hwnd, WM_KEYDOWN, VK_F4, 1);
    flush(window);
    require(panel.output->expanded() && testing::matches(pixel(10, 84), 0x8ee0c1),
            "Selected popup row did not paint");
    SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 1);
    flush(window);
    require(!panel.output->expanded() && testing::matches(pixel(10, 84), 0x15282a),
            "Popup pixels survived dismissal");
    auto position = [&](float x, float y) {
        return MAKELPARAM(LONG(x * window.scale()), LONG(y * window.scale()));
    };
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, position(120, 105));
    require(GetCapture() == hwnd && panel.volume->state().pressed, "Slider did not acquire HWND capture");
    SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
    require(!panel.volume->state().pressed && GetCapture() != hwnd, "Cancelled drag retained capture");

    std::exception_ptr error;
    std::jthread inspector([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            auto cleanup = wil::scope_exit([] { winrt::uninit_apartment(); });
            wil::com_ptr<IUIAutomation> client;
            THROW_IF_FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(client.put())));
            Node pane;
            THROW_IF_FAILED(client->ElementFromHandle(hwnd, pane.put()));
            auto dropdown = named(client.get(), pane.get(), L"Output device");
            auto expand = pattern<IUIAutomationExpandCollapsePattern>(dropdown.get(), UIA_ExpandCollapsePatternId);
            THROW_IF_FAILED(expand->Expand());
            ExpandCollapseState state{};
            THROW_IF_FAILED(expand->get_CurrentExpandCollapseState(&state));
            require(state == ExpandCollapseState_Expanded, "Expanded state missing");
            auto item = named(client.get(), pane.get(), L"Desk speakers");
            auto choice = pattern<IUIAutomationSelectionItemPattern>(item.get(), UIA_SelectionItemPatternId);
            THROW_IF_FAILED(choice->Select());
            auto value = pattern<IUIAutomationValuePattern>(dropdown.get(), UIA_ValuePatternId);
            wil::unique_bstr selected;
            THROW_IF_FAILED(value->get_CurrentValue(selected.put()));
            require(std::wstring_view(selected.get()) == L"Desk speakers",
                    "Popup selection did not update the trigger");
            THROW_IF_FAILED(expand->Collapse());
            require(FAILED(choice->Select()), "Stale popup item");
            auto toggle = pattern<IUIAutomationTogglePattern>(named(client.get(), pane.get(), L"Mute audio").get(),
                                                              UIA_TogglePatternId);
            THROW_IF_FAILED(toggle->Toggle());
            ToggleState checked{};
            THROW_IF_FAILED(toggle->get_CurrentToggleState(&checked));
            require(checked == ToggleState_On, "Automation toggle did not change state");
        } catch (...) { error = std::current_exception(); }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    });
    app.run();
    inspector.join();
    if (error) {
        std::rethrow_exception(error);
    }
    return 0;
}
}
int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"interaction", interaction}});
}
