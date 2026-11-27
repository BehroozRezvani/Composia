#include "../examples/controls/Playback.hpp"
#include "support/TestSupport.hpp"
#include <map>
#include <composia/HostedVisual.hpp>
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
// A renderer receives the tokens its control inherits and repaints when they change; a control
// without a renderer draws nothing.
int tokens(const testing::Options& options) {
    Application app(options.warp);
    namespace ui = composia::controls;
    ui::StyleTokens theme{0xf0f0f0, 0x204060, 0xc03020};
    auto scope = std::make_shared<ui::StyleScope>(theme, ElementLayout{ElementLayout::Kind::stack});
    auto drawn = std::make_shared<ui::Button>(L"Apply", [] {});
    drawn->set_renderer([](ScopedSurfaceDraw& d, const ui::Control& c, ui::State, const ui::StyleTokens& t) {
        d.context()->FillRectangle({0, 0, c.bounds().width, c.bounds().height}, d.solid_brush(t.background));
    });
    auto bare = std::make_shared<ui::Button>(L"Cancel", [] {});
    scope->add(drawn);
    scope->add(bare);
    ElementWindow window(app, L"Style tokens", 200, 100, scope);
    window.show(SW_SHOWNOACTIVATE);
    flush(window);
    auto pixel = [&](float x, float y) {
        return testing::surface_pixel(app, window.composition_target().surface(), LONG(x * window.scale()),
                                      LONG(y * window.scale()));
    };
    require(testing::matches(pixel(10, 10), 0x204060), "The renderer did not receive the scope's tokens");
    require(pixel(10, 40).a == 0, "A control without a renderer drew something");
    theme.background = 0x408020;
    scope->set_tokens(theme);
    flush(window);
    require(testing::matches(pixel(10, 10), 0x408020), "Changing the scope's tokens did not repaint");
    scope->set_tokens(std::nullopt);
    flush(window);
    require(testing::matches(pixel(10, 10), ui::StyleTokens{}.background), "Without a scope the renderer did not get StyleTokens{}");
    return 0;
}
// A separator row draws a line in the border color across its middle, or through its renderer.
int separator_drawing(const testing::Options& options) {
    Application app(options.warp);
    namespace ui = composia::controls;
    ui::StyleTokens theme;
    theme.border = 0xc03020;
    auto scope = std::make_shared<ui::StyleScope>(theme, ElementLayout{ElementLayout::Kind::stack});
    auto items = ui::Items::strings({L"Reply", L"", L"Delete"});
    items.separator = [](std::size_t i) { return i == 1; };
    auto list = std::make_shared<ui::List>(L"Actions", items, 32, 100);
    scope->add(list);
    ElementWindow window(app, L"Separators", 200, 120, scope);
    window.show(SW_SHOWNOACTIVATE);
    flush(window);
    auto pixel = [&](float x, float y) {
        return testing::surface_pixel(app, window.composition_target().surface(), LONG(x * window.scale()),
                                      LONG(y * window.scale()));
    };
    // The separator spans 32 to 41; its line is the DIP at 36.
    require(testing::matches(pixel(100, 36.5f), 0xc03020), "The separator's line was not drawn in the border color");
    list->set_separator_renderer([](ScopedSurfaceDraw& d, std::size_t, layout::Size size, const ui::StyleTokens&) {
        d.context()->FillRectangle({0, 0, size.width, size.height}, d.solid_brush(0x204060));
    });
    flush(window);
    require(testing::matches(pixel(100, 33), 0x204060), "The separator renderer did not draw the separator");
    return 0;
}
// A checked row's renderer gets State::checked, and State::selected only while it is highlighted.
int menu_states(const testing::Options& options) {
    Application app(options.warp);
    namespace ui = composia::controls;
    auto items = ui::Items::strings({L"Reply", L"Wrap lines"});
    items.checked = [](std::size_t i) { return i == 1 ? std::optional<bool>{true} : std::nullopt; };
    auto list = std::make_shared<ui::List>(L"View", items, 32, 100);
    std::map<std::size_t, ui::State> states;
    list->set_row_renderer([&](ScopedSurfaceDraw&, std::size_t i, std::wstring_view, layout::Size, ui::State s, const ui::StyleTokens&) {
        states[i] = s;
    });
    ElementWindow window(app, L"Menu states", 200, 120, list);
    window.show(SW_SHOWNOACTIVATE);
    list->select(0);
    flush(window);
    require(states.contains(1) && states[1].checked && !states[1].selected, "A checked row was painted as selected");
    require(states.contains(0) && states[0].selected && !states[0].checked, "The selected row was painted as checked");
    list->select(1);
    states.clear();
    flush(window);
    require(states[1].checked && states[1].selected, "A checked, selected row lost either state");
    return 0;
}
// The scrollbar draws over the content at the right edge, wider under the pointer, or through
// a renderer that receives its track, thumb and state.
int scrollbar(const testing::Options& options) {
    Application app(options.warp);
    namespace ui = composia::controls;
    auto view = std::make_shared<ui::ScrollView>(L"Messages", layout::Axis::vertical, 100);
    auto content = std::make_shared<ui::Button>(L"Content", [] {});
    content->set_preferred_size({200, 400});
    content->set_renderer([](ScopedSurfaceDraw& d, const ui::Control& c, ui::State, const ui::StyleTokens&) {
        d.context()->FillRectangle({0, 0, c.bounds().width, c.bounds().height}, d.solid_brush(0x204060));
    });
    view->set_content(content);
    ElementWindow window(app, L"Scrollbar", 200, 100, view);
    window.show(SW_SHOWNOACTIVATE);
    flush(window);
    auto pixel = [&](float x, float y) {
        return testing::surface_pixel(app, window.composition_target().surface(), LONG(x * window.scale()),
                                      LONG(y * window.scale()));
    };
    require(testing::matches(pixel(195, 14), 0x204060), "The idle scrollbar was drawn wide");
    SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(LONG(195 * window.scale()), LONG(14 * window.scale())));
    flush(window);
    require(!testing::matches(pixel(195, 14), 0x204060), "The hovered scrollbar did not draw over the content");

    std::optional<ui::ScrollView::Scrollbar> drawn;
    view->set_scrollbar_renderer([&](ScopedSurfaceDraw& d, const ui::ScrollView&, const ui::ScrollView::Scrollbar& bar,
                                     const ui::StyleTokens&) {
        drawn = bar;
        const auto t = bar.thumb;
        d.context()->FillRectangle({t.x, t.y, t.x + t.width, t.y + t.height}, d.solid_brush(0xc03020));
    });
    flush(window);
    // The thumb shows 100 of 400 DIPs of a 96 DIP track: 24 DIPs, from 2 DIPs down.
    require(drawn && drawn->hovered && !drawn->dragging && drawn->thumb.y == 2 && drawn->thumb.height == 24 &&
        drawn->track.height == 96 && testing::matches(pixel(195, 14), 0xc03020), "The scrollbar renderer did not draw the thumb");
    view->set_scroll_offset(300);
    flush(window);
    require(drawn->thumb.y == 74 && testing::matches(pixel(195, 86), 0xc03020), "The thumb did not follow the scroll offset");
    return 0;
}
int overlay(const testing::Options& options) {
    Application app(options.warp);
    namespace ui = composia::controls;
    auto list = std::make_shared<ui::List>(L"Folders",
        ui::Items{10, [](std::size_t i) { return L"Row " + std::to_wstring(i); }}, 24, 72);
    const auto layer = app.compositor().CreateContainerVisual();
    list->set_overlay(std::make_shared<HostedVisual>(layer));
    ElementWindow window(app, L"List overlay", 200, 72, list);
    window.show(SW_SHOWNOACTIVATE);
    flush(window);
    const auto frame = layer.Parent();
    require(frame && frame.IsVisible() && frame.Size().y == 240 && frame.Offset().y == 0, "The overlay did not span the rows");
    const auto third = list->row_bounds(3);
    require(third.y == 72 && third.height == 24 && third.width == list->bounds().width, "row_bounds");
    list->set_scroll_offset(48);
    flush(window);
    const auto clip = frame.Clip().as<composition::InsetClip>();
    require(frame.Offset().y == -48 && clip.TopInset() == 48 && list->row_bounds(3).y == 72,
        "The overlay did not scroll with the rows");
    // Row 3 now starts 24 DIPs down; a press there goes through the overlay to it.
    const auto px = [&](float dips) { return static_cast<LONG>(std::lround(dips * window.scale())); };
    SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(px(10), px(30)));
    SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(px(10), px(30)));
    flush(window);
    require(list->selected() == 3, "The overlay took a press meant for a row");
    list->set_overlay(nullptr);
    flush(window);
    require(!layer.Parent(), "Removing the overlay left it in the window");
    return 0;
}
int fixture(const testing::Options& options) {
    Application app(options.warp);
    playback::Playback panel(app);
    ElementWindow window(app, L"Composia accessibility test", 344, 184, panel.root);
    window.show(SW_SHOWNOACTIVATE);
    require(panel.output->open(), "Could not open playback choices");
    return app.run();
}
}
int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"interaction", interaction}, {"fixture", fixture}, {"tokens", tokens}, {"scrollbar-drawing", scrollbar},
        {"separator-drawing", separator_drawing}, {"menu-states", menu_states},
        {"overlay", overlay}});
}
