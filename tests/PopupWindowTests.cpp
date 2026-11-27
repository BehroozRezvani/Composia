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
        owner.show(SW_SHOWNOACTIVATE);
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
        const auto foreground = GetForegroundWindow();
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
        require(GetForegroundWindow() == foreground, "A popup that keeps its owner's focus took the foreground");
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
        require(pump_until([&] { return !menu.is_open(); }), "Losing the activation did not close the menu");
        require(menuReasons == std::vector<Reason>{Reason::deactivated}, "The menu did not report its deactivation");
        require(order == std::vector<std::wstring>{L"submenu", L"menu"} && !submenu.is_open() && !IsWindowVisible(submenu.hwnd()),
            "A popup the menu owns did not close before it");

        // Minimizing the owner closes the popup, which does not come back with it.
        popup.show_at(at, {100, 60});
        ShowWindow(owner.hwnd(), SW_MINIMIZE);
        flush(owner);
        require(!popup.is_open() && reasons.back() == Reason::owner_changed, "Minimizing the owner did not close the popup");
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
    owner.show(SW_SHOWNOACTIVATE);
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
    return testing::run(argc, argv, {{"placement", placement}, {"showing", showing}, {"closing", closing}, {"automation", automation}});
}
