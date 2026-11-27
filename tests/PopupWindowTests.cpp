#include <composia/PopupWindow.hpp>
#include <composia/ElementAccessibility.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include "PopupPlacement.hpp"
#include "support/TestSupport.hpp"
#include <UIAutomation.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace composia;
using testing::require;
using Reason = PopupWindow::CloseReason;
using Below = PopupWindow::Below;

namespace {

bool same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

void flush(Window& window) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    window.rethrow_callback_error();
}

bool pump_until(const std::function<bool()>& condition, std::chrono::milliseconds limit = std::chrono::milliseconds{2000}) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end) { return false; }
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
    return true;
}

RECT work_at(POINT point) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcWork;
}

std::shared_ptr<Element> block(float width, float height) {
    auto element = std::make_shared<Element>();
    element->set_preferred_size({width, height});
    return element;
}

// Where a popup goes, worked out without windows.
int placement(const testing::Options&) {
    using detail::PopupRequest;
    using detail::place_popup;
    const RECT work{0, 0, 1000, 800};
    PopupRequest at;
    at.point = {100, 100};
    at.size = {200, 150};
    at.work = work;
    require(same(place_popup(at), {100, 100, 300, 250}), "A popup at a point was not below and to its right");
    at.point = {900, 700};
    require(same(place_popup(at), {700, 550, 900, 700}), "A popup near the corner was not flipped left and up");
    at.point = {100, 100};
    at.size = {2000, 2000};
    require(same(place_popup(at), {0, 0, 1000, 800}), "A popup larger than the work area was not kept to it");

    PopupRequest below;
    below.anchor = RECT{100, 100, 180, 130};
    below.size = {200, 150};
    below.gap = 4;
    below.work = work;
    require(same(place_popup(below), {100, 134, 300, 284}), "A popup was not below its anchor, past the gap");
    below.anchor = RECT{100, 700, 180, 730};
    require(same(place_popup(below), {100, 546, 300, 696}), "A popup with no room below did not go above");
    below.anchor = RECT{100, 300, 180, 330};
    below.size = {200, 900};
    require(same(place_popup(below), {100, 334, 300, 800}), "A popup with no room either side was not shortened below");
    below.size = {200, 150};
    below.anchor = RECT{100, 100, 180, 130};
    below.below = Below::match_width;
    require(same(place_popup(below), {100, 134, 180, 284}), "A popup did not take its anchor's width");

    below.below = Below::trailing_inside_owner;
    below.ownerArea = RECT{50, 50, 450, 450};
    below.anchor = RECT{300, 100, 400, 130};
    require(same(place_popup(below), {200, 134, 400, 284}), "A trailing popup did not end at its anchor's right edge");
    below.size = {600, 600};
    require(same(place_popup(below), {50, 134, 450, 450}), "A trailing popup was not kept inside its owner");

    PopupRequest scaled;
    scaled.size = {100, 50};
    scaled.work = work;
    scaled.dpi = 144;
    require(same(place_popup(scaled), {0, 0, 150, 75}), "A popup's size was not scaled to the monitor");
    scaled.anchor = RECT{0, 0, 10, 10};
    scaled.gap = 4;
    require(same(place_popup(scaled), {0, 16, 150, 91}), "A popup's gap was not scaled to the monitor");
    return 0;
}

// A popup that leaves the keyboard with its owner: owned, hidden at first, placed where the
// placement says, painted with its background, and never in the foreground.
int showing(const testing::Options& options) {
    Application app{options.warp};
    {
        ElementWindow owner{app, L"Owner", 300, 200, std::make_shared<Element>()};
        owner.show(SW_SHOWNORMAL);
        flush(owner);
        PopupWindow::Options settings;
        settings.activation = PopupWindow::Activation::keep_owner_focus;
        settings.background = 0x204060;
        PopupWindow popup{owner, block(120, 80), L"Choices", settings};
        require(!popup.is_open() && !IsWindowVisible(popup.hwnd()), "A popup was shown when it was made");
        require(GetWindow(popup.hwnd(), GW_OWNER) == owner.hwnd(), "A popup was not owned by its owner's window");
        const auto style = GetWindowLongPtrW(popup.hwnd(), GWL_STYLE), extended = GetWindowLongPtrW(popup.hwnd(), GWL_EXSTYLE);
        require((style & WS_POPUP) && (extended & WS_EX_NOACTIVATE) && (extended & WS_EX_TOPMOST) && (extended & WS_EX_TOOLWINDOW),
            "A popup that keeps its owner's focus lacked its styles");
        const auto active = GetActiveWindow();
        RECT ownerRect{};
        GetWindowRect(owner.hwnd(), &ownerRect);
        const RECT anchor{ownerRect.left + 10, ownerRect.top + 40, ownerRect.left + 110, ownerRect.top + 70};
        popup.show_below(anchor, {120, 80});
        flush(popup);
        require(popup.is_open() && IsWindowVisible(popup.hwnd()), "show_below did not show the popup");
        detail::PopupRequest expected;
        expected.anchor = anchor;
        expected.size = {120, 80};
        expected.gap = 4;
        expected.work = work_at({anchor.left, anchor.bottom});
        expected.dpi = PopupWindow::dpi_at({anchor.left, anchor.bottom});
        RECT placed{};
        GetWindowRect(popup.hwnd(), &placed);
        require(same(placed, detail::place_popup(expected)), "The popup was not where its placement put it");
        require(active == owner.hwnd() && GetActiveWindow() == owner.hwnd(), "A popup that keeps its owner's focus took the activation");
        const auto pixel = testing::surface_pixel(app, popup.composition_target().surface(), 5, 5);
        require(testing::matches(pixel, 0x204060), "The popup's background was not painted under its content");
        popup.set_background(0x406020);
        flush(popup);
        UpdateWindow(popup.hwnd());
        require(testing::matches(testing::surface_pixel(app, popup.composition_target().surface(), 5, 5), 0x406020),
            "A new background was not painted");
        popup.close();
        require(!popup.is_open() && !IsWindowVisible(popup.hwnd()), "close did not hide the popup");
    }
    app.close();
    return 0;
}

// Each way a popup closes, once per showing, with its reason; the popups it owns close first.
int closing(const testing::Options& options) {
    Application app{options.warp};
    {
        ElementWindow owner{app, L"Owner", 300, 200, std::make_shared<Element>()};
        owner.show(SW_SHOWNORMAL);
        flush(owner);
        PopupWindow::Options keep;
        keep.activation = PopupWindow::Activation::keep_owner_focus;
        PopupWindow popup{owner, block(100, 60), L"Choices", keep};
        std::vector<Reason> reasons;
        auto watch = popup.on_closed([&](Reason reason) { reasons.push_back(reason); });
        RECT ownerRect{};
        GetWindowRect(owner.hwnd(), &ownerRect);
        const POINT at{ownerRect.left + 20, ownerRect.top + 40};
        popup.show_at(at, {100, 60});
        popup.close();
        popup.close();
        require(reasons == std::vector<Reason>{Reason::requested}, "close did not close once");
        popup.show_at(at, {100, 60});
        SendMessageW(popup.hwnd(), WM_CANCELMODE, 0, 0);
        popup.show_at(at, {100, 60});
        SendMessageW(popup.hwnd(), WM_COMMAND, IDCANCEL, 0);
        popup.show_at(at, {100, 60});
        SendMessageW(popup.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        require(reasons == std::vector<Reason>{Reason::requested, Reason::escape, Reason::escape, Reason::escape} && !popup.is_open(),
            "WM_CANCELMODE, IDCANCEL or Escape did not close the popup");

        // A popup that takes the keyboard closes after losing the activation, unless it went to a
        // popup of its own.
        PopupWindow menu{owner, block(100, 60), L"Menu"};
        std::vector<Reason> menuReasons;
        auto watchMenu = menu.on_closed([&](Reason reason) { menuReasons.push_back(reason); });
        menu.show_at(at, {100, 60});
        PopupWindow submenu{menu, block(100, 60), L"Submenu", keep};
        std::vector<std::wstring> order;
        auto watchSub = submenu.on_closed([&](Reason) { order.push_back(L"submenu"); });
        auto watchOrder = menu.on_closed([&](Reason) { order.push_back(L"menu"); });
        submenu.show_at({at.x + 100, at.y}, {100, 60});
        SendMessageW(menu.hwnd(), WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(submenu.hwnd()));
        flush(menu);
        require(menu.is_open(), "Activating a popup of its own closed the menu");
        SendMessageW(menu.hwnd(), WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(owner.hwnd()));
        require(menu.is_open(), "The menu closed during the activation change");
        require(pump_until([&] { return !menu.is_open() && !submenu.is_open(); }), "Losing the activation did not close the menu");
        require(menuReasons == std::vector<Reason>{Reason::deactivated}, "The menu did not report its deactivation");
        // Closing a popup closes the popups it owns first, which hiding it would leave on screen.
        menu.show_at(at, {100, 60});
        submenu.show_at({at.x + 100, at.y}, {100, 60});
        order.clear();
        menu.close();
        require(order == std::vector<std::wstring>{L"submenu", L"menu"} && !submenu.is_open() && !IsWindowVisible(submenu.hwnd()),
            "A popup the menu owns did not close before it"); 

        // Minimizing the owner closes the popup, which does not come back with it.
        popup.show_at(at, {100, 60});
        ShowWindow(owner.hwnd(), SW_MINIMIZE);
        require(!popup.is_open() && reasons.back() == Reason::owner_changed, "Minimizing the owner did not close the popup at once");
        flush(owner);
        ShowWindow(owner.hwnd(), SW_RESTORE);
        flush(owner);
        require(!IsWindowVisible(popup.hwnd()), "The popup came back with its owner");

        // An owner destroyed while its popup is open takes the popup's window with it.
        auto doomed = std::make_unique<ElementWindow>(app, L"Doomed", 200, 100, std::make_shared<Element>());
        doomed->show(SW_SHOWNOACTIVATE);
        auto orphan = std::make_unique<PopupWindow>(*doomed, block(50, 50), L"Orphan", keep);
        std::vector<Reason> orphanReasons;
        auto watchOrphan = orphan->on_closed([&](Reason reason) { orphanReasons.push_back(reason); });
        orphan->show_at(at, {50, 50});
        doomed.reset();
        require(orphanReasons == std::vector<Reason>{Reason::destroyed} && !orphan->hwnd() && !orphan->is_open(),
            "Destroying the owner did not close the popup as destroyed");
        orphan.reset();
    }
    app.close();
    return 0;
}

// Counts the presses, releases and wheel turns that reach it, and what a popup said at the press.
class Clicker : public Element {
public:
    Clicker() { set_preferred_size({100, 30}); }
    unsigned downs{}, ups{}, wheels{};
    std::function<void()> onDown;
private:
    bool on_pointer(const PointerEvent& e) override {
        if (e.kind == PointerEvent::Kind::down) { ++downs; if (onDown) { onDown(); } }
        if (e.kind == PointerEvent::Kind::up) { ++ups; }
        if (e.kind == PointerEvent::Kind::wheel) { ++wheels; }
        return true;
    }
};

// An owner with two buttons, A over B, and a way to press them as the mouse does.
struct Stage {
    Application& app;
    std::shared_ptr<Element> root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    std::shared_ptr<Clicker> a = std::make_shared<Clicker>(), b = std::make_shared<Clicker>();
    std::unique_ptr<ElementWindow> owner;
    explicit Stage(Application& application) : app(application) {
        root->add(a);
        root->add(b);
        owner = std::make_unique<ElementWindow>(app, L"Owner", 300, 200, root);
        owner->show(SW_SHOWNORMAL);
        flush(*owner);
    }
    [[nodiscard]] LPARAM at(float x, float y) const {
        return MAKELPARAM(static_cast<int>(x * owner->scale()), static_cast<int>(y * owner->scale()));
    }
    // A's place on the screen.
    [[nodiscard]] RECT anchor() const {
        RECT rect{0, 0, static_cast<LONG>(100 * owner->scale()), static_cast<LONG>(30 * owner->scale())};
        MapWindowPoints(owner->hwnd(), nullptr, reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    }
    void click(HWND target, LPARAM where) const {
        PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, where);
        PostMessageW(target, WM_LBUTTONUP, 0, where);
    }
};

PopupWindow::Options keeping() {
    PopupWindow::Options options;
    options.activation = PopupWindow::Activation::keep_owner_focus;
    return options;
}

// A press or the wheel outside a popup closes it after the press reaches its target; inside,
// it stays.
int dismissal(const testing::Options& options) {
    Application app{options.warp};
    {
        Stage stage{app};
        auto content = std::make_shared<Clicker>();
        PopupWindow popup{*stage.owner, content, L"Choices", keeping()};
        std::vector<Reason> reasons;
        auto watch = popup.on_closed([&](Reason reason) { reasons.push_back(reason); });
        const auto anchor = stage.anchor();
        popup.show_below(anchor, {100, 60});
        stage.click(stage.owner->hwnd(), stage.at(10, 45));
        require(pump_until([&] { return !popup.is_open(); }), "A press outside did not close the popup");
        require(reasons.back() == Reason::outside_press && stage.b->downs == 1 && stage.b->ups == 1,
            "The press outside did not reach its target, or closed the popup for another reason");
        require(!detail::popup_hooks_installed(true) && !detail::popup_hooks_installed(false), "The hooks stayed after the popup closed");

        popup.show_below(anchor, {100, 60});
        require(detail::popup_hooks_installed(true) && detail::popup_hooks_installed(false), "An open popup did not watch presses");
        stage.click(popup.hwnd(), MAKELPARAM(5, 5));
        pump_until([] { return false; }, std::chrono::milliseconds{100});
        require(popup.is_open() && content->downs == 1, "A press inside closed the popup or did not reach it");

        // Presses on the frame and the wheel outside close it too.
        RECT ownerRect{};
        GetWindowRect(stage.owner->hwnd(), &ownerRect);
        PostMessageW(stage.owner->hwnd(), WM_NCLBUTTONDOWN, HTNOWHERE, MAKELPARAM(ownerRect.left + 2, ownerRect.top + 2));
        require(pump_until([&] { return !popup.is_open(); }), "A press on the owner's frame did not close the popup");
        popup.show_below(anchor, {100, 60});
        PostMessageW(stage.owner->hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(ownerRect.right - 5, ownerRect.bottom - 5));
        require(pump_until([&] { return !popup.is_open(); }), "The wheel outside did not close the popup");
        // The wheel over the popup is the popup's, wherever Windows sent it.
        popup.show_below(anchor, {100, 60});
        RECT popupRect{};
        GetWindowRect(popup.hwnd(), &popupRect);
        PostMessageW(stage.owner->hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(popupRect.left + 5, popupRect.top + 5));
        require(pump_until([&] { return content->wheels == 1; }) && popup.is_open(), "The wheel over the popup did not reach it, or closed it");

        // A press in another window, or sent rather than posted, closes it as well.
        ElementWindow other{app, L"Other", 100, 100, std::make_shared<Clicker>()};
        other.show(SW_SHOWNOACTIVATE);
        stage.click(other.hwnd(), MAKELPARAM(5, 5));
        require(pump_until([&] { return !popup.is_open(); }), "A press in another window did not close the popup");
        popup.show_below(anchor, {100, 60});
        SendMessageW(stage.owner->hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, stage.at(10, 45));
        SendMessageW(stage.owner->hwnd(), WM_LBUTTONUP, 0, stage.at(10, 45));
        require(pump_until([&] { return !popup.is_open(); }), "A sent press did not close the popup");

        // A press in one popup closes another it has nothing to do with.
        PopupWindow card{*stage.owner, std::make_shared<Clicker>(), L"Card", keeping()};
        popup.show_below(anchor, {100, 60});
        card.show_at({anchor.right + 200, anchor.bottom}, {80, 40});
        stage.click(card.hwnd(), MAKELPARAM(5, 5));
        require(pump_until([&] { return !popup.is_open(); }) && card.is_open(), "A press in an unrelated popup did not close the other");
        card.close();
        // Shown while the application has no active window, it closes rather than float over others.
        SetActiveWindow(nullptr);
        if (!GetActiveWindow()) {
            popup.show_below(anchor, {100, 60});
            require(pump_until([&] { return !popup.is_open(); }) && reasons.back() == Reason::deactivated,
                "A popup shown in the background stayed open");
            SetActiveWindow(stage.owner->hwnd());
        }

        auto sticky = keeping();
        sticky.lightDismiss = false;
        PopupWindow pinned{*stage.owner, std::make_shared<Clicker>(), L"Pinned", sticky};
        pinned.show_below(anchor, {100, 60});
        stage.click(stage.owner->hwnd(), stage.at(10, 45));
        pump_until([] { return false; }, std::chrono::milliseconds{100});
        require(pinned.is_open(), "A popup without light dismissal closed at a press outside");
        pinned.close();
    }
    app.close();
    return 0;
}

// A press on the rectangle a popup was shown below closes it and, by default, reaches nothing,
// so the button there does not open it again; closed_by_anchor_press holds until the next press.
int anchor(const testing::Options& options) {
    Application app{options.warp};
    {
        Stage stage{app};
        PopupWindow popup{*stage.owner, std::make_shared<Clicker>(), L"Choices", keeping()};
        std::vector<Reason> reasons;
        auto watch = popup.on_closed([&](Reason reason) { reasons.push_back(reason); });
        popup.show_below(stage.anchor(), {100, 60});
        stage.click(stage.owner->hwnd(), stage.at(10, 10));
        require(pump_until([&] { return !popup.is_open(); }), "A press on the anchor did not close the popup");
        require(reasons.back() == Reason::anchor_press && stage.a->downs == 0 && popup.closed_by_anchor_press(),
            "The anchor press was not consumed or not reported");
        require(detail::popup_hooks_installed(false) && !detail::popup_hooks_installed(true),
            "Holding closed_by_anchor_press kept the wrong hooks");
        stage.click(stage.owner->hwnd(), stage.at(10, 45));
        pump_until([] { return false; }, std::chrono::milliseconds{50});
        require(!popup.closed_by_anchor_press() && !detail::popup_hooks_installed(false), "The next press did not end closed_by_anchor_press");

        // Without consuming it, the button sees the press and can tell the popup closed for it.
        auto passing = keeping();
        passing.consumeAnchorPress = false;
        PopupWindow menu{*stage.owner, std::make_shared<Clicker>(), L"Menu", passing};
        bool sawClose{};
        stage.a->onDown = [&] { sawClose = menu.closed_by_anchor_press(); };
        menu.show_below(stage.anchor(), {100, 60});
        stage.click(stage.owner->hwnd(), stage.at(10, 10));
        require(pump_until([&] { return !menu.is_open(); }), "A press on the anchor did not close the popup");
        require(stage.a->downs == 1 && sawClose, "The anchor did not get the press, or could not tell the popup closed for it");
        // A popup destroyed while it holds the flag lets go of the hook.
        auto doomed = std::make_unique<PopupWindow>(*stage.owner, std::make_shared<Clicker>(), L"Doomed", keeping());
        doomed->show_below(stage.anchor(), {100, 60});
        stage.click(stage.owner->hwnd(), stage.at(10, 10));
        pump_until([&] { return !doomed->is_open(); });
        doomed.reset();
        require(!detail::popup_hooks_installed(false), "A popup destroyed while it held closed_by_anchor_press kept the hook");
    }
    app.close();
    return 0;
}

// A press inside a popup's own popup keeps both; inside the outer one, only the inner closes;
// outside both, both close, the inner first.
int nested(const testing::Options& options) {
    Application app{options.warp};
    {
        Stage stage{app};
        PopupWindow outer{*stage.owner, std::make_shared<Clicker>(), L"Outer", keeping()};
        PopupWindow inner{outer, std::make_shared<Clicker>(), L"Inner", keeping()};
        std::vector<std::wstring> order;
        auto watchOuter = outer.on_closed([&](Reason) { order.push_back(L"outer"); });
        auto watchInner = inner.on_closed([&](Reason) { order.push_back(L"inner"); });
        RECT ownerRect{};
        GetWindowRect(stage.owner->hwnd(), &ownerRect);
        outer.show_at({ownerRect.left + 150, ownerRect.top + 40}, {80, 60});
        inner.show_at({ownerRect.left + 240, ownerRect.top + 40}, {80, 60});
        stage.click(inner.hwnd(), MAKELPARAM(5, 5));
        pump_until([] { return false; }, std::chrono::milliseconds{100});
        require(outer.is_open() && inner.is_open(), "A press in the inner popup closed one of them");
        stage.click(outer.hwnd(), MAKELPARAM(5, 5));
        require(pump_until([&] { return !inner.is_open(); }) && outer.is_open(), "A press in the outer popup did not close only the inner");
        inner.show_at({ownerRect.left + 240, ownerRect.top + 40}, {80, 60});
        order.clear();
        stage.click(stage.owner->hwnd(), stage.at(10, 45));
        require(pump_until([&] { return !outer.is_open() && !inner.is_open(); }), "A press outside did not close both");
        require(order == std::vector<std::wstring>{L"inner", L"outer"}, "The inner popup did not close first");
    }
    app.close();
    return 0;
}

// The owner moving, deactivating, being disabled or entering a modal state closes its popup.
int owner(const testing::Options& options) {
    Application app{options.warp};
    {
        Stage stage{app};
        PopupWindow popup{*stage.owner, std::make_shared<Clicker>(), L"Choices", keeping()};
        std::vector<Reason> reasons;
        auto watch = popup.on_closed([&](Reason reason) { reasons.push_back(reason); });
        const auto owner = stage.owner->hwnd();
        const auto show = [&] { popup.show_below(stage.anchor(), {100, 60}); };
        show();
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        SetWindowPos(owner, nullptr, ownerRect.left + 10, ownerRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        require(pump_until([&] { return !popup.is_open(); }) && reasons.back() == Reason::owner_changed, "Moving the owner did not close the popup");
        show();
        SendMessageW(owner, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(popup.hwnd()));
        pump_until([] { return false; }, std::chrono::milliseconds{50});
        require(popup.is_open(), "The owner's activation going to the popup closed it");
        SendMessageW(owner, WM_ACTIVATE, WA_INACTIVE, 0);
        require(pump_until([&] { return !popup.is_open(); }) && reasons.back() == Reason::deactivated, "The owner deactivating did not close the popup");
        show();
        SendMessageW(owner, WM_ENABLE, FALSE, 0);
        require(pump_until([&] { return !popup.is_open(); }) && reasons.back() == Reason::owner_changed, "Disabling the owner did not close the popup");
        show();
        SendMessageW(owner, WM_SHOWWINDOW, FALSE, 0);
        require(pump_until([&] { return !popup.is_open(); }) && reasons.back() == Reason::owner_changed, "Hiding the owner did not close the popup");
        show();
        SendMessageW(owner, WM_CANCELMODE, 0, 0);
        require(pump_until([&] { return !popup.is_open(); }) && reasons.back() == Reason::owner_changed, "WM_CANCELMODE to the owner did not close the popup");
    }
    app.close();
    return 0;
}

class Handler : public IUIAutomationEventHandler {
public:
    wil::unique_event opened{wil::EventOptions::ManualReset}, closed{wil::EventOptions::ManualReset};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) noexcept override {
        if (id == __uuidof(IUnknown) || id == __uuidof(IUIAutomationEventHandler)) {
            *out = static_cast<IUIAutomationEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --references; }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID id) noexcept override {
        if (id == UIA_MenuOpenedEventId) { opened.SetEvent(); }
        if (id == UIA_MenuClosedEventId) { closed.SetEvent(); }
        return S_OK;
    }
    std::atomic<ULONG> references{1};
};

// A menu popup tells UI Automation clients when it opens and closes.
int automation(const testing::Options& options) {
    Application app{options.warp};
    ElementWindow owner{app, L"Owner", 300, 200, std::make_shared<Element>()};
    owner.show(SW_SHOWNORMAL);
    auto items = std::make_shared<Element>();
    items->set_accessibility({.name = L"Actions", .controlType = UIA_MenuControlTypeId});
    items->set_preferred_size({100, 60});
    PopupWindow::Options settings;
    settings.activation = PopupWindow::Activation::keep_owner_focus;
    settings.menu = true;
    PopupWindow popup{owner, items, L"Actions", settings};
    const auto hwnd = popup.hwnd();
    RECT ownerRect{};
    GetWindowRect(owner.hwnd(), &ownerRect);
    const POINT at{ownerRect.left + 20, ownerRect.top + 40};
    std::exception_ptr error;
    Handler handler;
    std::jthread inspector([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto uninit = wil::scope_exit([] { winrt::uninit_apartment(); });
            wil::com_ptr<IUIAutomation> client;
            THROW_IF_FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(client.put())));
            wil::com_ptr<IUIAutomationElement> element;
            THROW_IF_FAILED(client->ElementFromHandle(hwnd, element.put()));
            THROW_IF_FAILED(client->AddAutomationEventHandler(UIA_MenuOpenedEventId, element.get(), TreeScope_Subtree, nullptr, &handler));
            THROW_IF_FAILED(client->AddAutomationEventHandler(UIA_MenuClosedEventId, element.get(), TreeScope_Subtree, nullptr, &handler));
            app.post([&] { popup.show_at(at, {100, 60}); });
            require(WaitForSingleObject(handler.opened.get(), 5000) == WAIT_OBJECT_0, "No MenuOpened when the menu showed");
            app.post([&] { popup.close(); });
            require(WaitForSingleObject(handler.closed.get(), 5000) == WAIT_OBJECT_0, "No MenuClosed when the menu closed");
            THROW_IF_FAILED(client->RemoveAllEventHandlers());
        } catch (...) { error = std::current_exception(); }
        PostMessageW(owner.hwnd(), WM_CLOSE, 0, 0);
    });
    app.run();
    inspector.join();
    if (error) { std::rethrow_exception(error); }
    return 0;
}

}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"placement", placement}, {"showing", showing}, {"closing", closing}, {"automation", automation},
        {"dismissal", dismissal}, {"anchor", anchor}, {"nested", nested}, {"owner", owner}});
}
