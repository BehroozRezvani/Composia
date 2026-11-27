#include <composia/PopupWindow.hpp>
#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include "ElementAutomation.hpp"
#include "Logging.hpp"
#include "PopupPlacement.hpp"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace composia {

namespace detail {
RECT place_popup(const PopupRequest& request) noexcept {
    using Below = PopupWindow::Below;
    const auto pixels = [&](float dips) {
        return static_cast<LONG>(std::ceil(std::max(0.0f, dips) * static_cast<float>(request.dpi) / 96.0f));
    };
    const bool inside = request.anchor && request.below == Below::trailing_inside_owner && request.ownerArea;
    const RECT area = inside ? *request.ownerArea : request.work;
    const LONG areaWidth = std::max(0L, area.right - area.left), areaHeight = std::max(0L, area.bottom - area.top);
    LONG width = request.anchor && request.below == Below::match_width ? request.anchor->right - request.anchor->left
                                                                         : pixels(request.size.width);
    LONG height = pixels(request.size.height);
    width = std::clamp(width, 0L, areaWidth);
    height = std::clamp(height, 0L, areaHeight);
    const auto clampX = [&](LONG x) { return std::clamp(x, area.left, std::max(area.left, area.right - width)); };
    const auto clampY = [&](LONG y) { return std::clamp(y, area.top, std::max(area.top, area.bottom - height)); };
    if (!request.anchor) {
        const auto& point = request.point;
        const LONG x = clampX(point.x + width > area.right ? point.x - width : point.x);
        const LONG y = clampY(point.y + height > area.bottom ? point.y - height : point.y);
        return {x, y, x + width, y + height};
    }
    const auto& anchor = *request.anchor;
    const LONG gap = pixels(request.gap);
    const LONG x = clampX(request.below == Below::trailing_inside_owner ? anchor.right - width : anchor.left);
    const LONG below = std::max(0L, area.bottom - anchor.bottom - gap), above = std::max(0L, anchor.top - gap - area.top);
    const bool down = height <= below || below >= above;
    height = std::min(height, down ? below : above);
    LONG y = down ? anchor.bottom + gap : anchor.top - gap - height;
    if (inside) { y = clampY(y); }
    return {x, y, x + width, y + height};
}
}

namespace {
bool is_press(UINT message) noexcept {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK: case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN: case WM_NCMBUTTONDBLCLK: case WM_NCXBUTTONDOWN: case WM_NCXBUTTONDBLCLK:
        return true;
    default: return false;
    }
}

bool is_wheel(UINT message) noexcept { return message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL; }

// Where a press or the wheel happened on the screen: client presses carry client coordinates,
// the rest screen coordinates.
POINT screen_point(HWND target, UINT message, LPARAM lparam) noexcept {
    POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
    const bool nonclient = message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK;
    if (!nonclient && !is_wheel(message) && target) { ClientToScreen(target, &point); }
    return point;
}

LRESULT CALLBACK message_hook(int code, WPARAM removal, LPARAM data) noexcept;
LRESULT CALLBACK sent_hook(int code, WPARAM sentByThread, LPARAM data) noexcept;
}

// The popups on this thread: all of them, the open ones in the order they opened, and those that
// hold closed_by_anchor_press until the next press. It watches presses and the wheel, posted or
// sent, and the owners' messages, through hooks it keeps only while they are needed. The hooks
// never close anything themselves: they post the closes.
class PopupTracker {
public:
    static PopupTracker& get() noexcept {
        static thread_local PopupTracker tracker;
        return tracker;
    }
    [[nodiscard]] bool is_popup(HWND hwnd) const noexcept {
        return hwnd && std::ranges::any_of(all, [hwnd](const PopupWindow* popup) { return popup->hwnd() == hwnd; });
    }
    void opened(PopupWindow& popup) noexcept {
        std::erase(open, &popup);
        open.push_back(&popup);
        update_hooks();
    }
    void closed(PopupWindow& popup) noexcept {
        std::erase(open, &popup);
        update_hooks();
    }
    void forget(PopupWindow& popup) noexcept {
        std::erase(open, &popup);
        std::erase(holding, &popup);
        std::erase(all, &popup);
        update_hooks();
    }
    // A press or the wheel: closes the open popups it is outside of, newest first, up to the one
    // it is in. Returns true to keep the message from its target.
    bool input(HWND target, UINT message, WPARAM wparam, LPARAM lparam, bool consumable) noexcept {
        const bool press = is_press(message), wheel = is_wheel(message);
        if (!press && !wheel) { return false; }
        if (press) {
            // closed_by_anchor_press holds until the next press.
            for (auto* popup : std::exchange(holding, {})) { popup->anchorPressed_ = false; }
        }
        const auto point = screen_point(target, message, lparam);
        bool consume = false;
        const auto snapshot = open;
        for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
            auto* popup = *it;
            if (std::ranges::find(open, popup) == open.end() || !popup->hwnd()) { continue; }
            if (popup->contains(target)) { break; }
            RECT bounds{};
            if (wheel && GetWindowRect(popup->hwnd(), &bounds) && PtInRect(&bounds, point)) {
                // The wheel goes to the focus window when inactive windows do not scroll: over the
                // popup, it is the popup's.
                if (consumable) {
                    PostMessageW(popup->hwnd(), message, wparam, lparam);
                    consume = true;
                }
                break;
            }
            if (!popup->options_.lightDismiss) { continue; }
            if (press && popup->anchor_ && PtInRect(&*popup->anchor_, point) && GetAncestor(target, GA_ROOT) == popup->owner_) {
                popup->anchorPressed_ = true;
                if (std::ranges::find(holding, popup) == holding.end()) { holding.push_back(popup); }
                popup->close_later(PopupWindow::CloseReason::anchor_press);
                consume = consume || (consumable && popup->options_.consumeAnchorPress);
            } else {
                popup->close_later(PopupWindow::CloseReason::outside_press);
            }
        }
        update_hooks();
        return consume;
    }
    // A message sent to a window: an open popup's owner moving, deactivating, being disabled or
    // entering a modal loop closes the popup.
    void owner_message(HWND target, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        if (message != WM_ACTIVATE && message != WM_ACTIVATEAPP && message != WM_CANCELMODE && message != WM_ENABLE &&
            message != WM_ENTERSIZEMOVE && message != WM_WINDOWPOSCHANGED) { return; }
        using enum PopupWindow::CloseReason;
        for (auto* popup : open) {
            if (popup->owner_ != target) { continue; }
            switch (message) {
            case WM_ACTIVATE:
                if (LOWORD(wparam) == WA_INACTIVE && !popup->contains(reinterpret_cast<HWND>(lparam))) { popup->close_later(deactivated); }
                break;
            case WM_ACTIVATEAPP:
                if (!wparam) { popup->close_later(deactivated); }
                break;
            case WM_ENABLE:
                if (!wparam) { popup->close_later(owner_changed); }
                break;
            case WM_WINDOWPOSCHANGED: {
                RECT now{};
                if (GetWindowRect(target, &now) && !EqualRect(&now, &popup->ownerRect_)) { popup->close_later(owner_changed); }
                break;
            }
            default:
                popup->close_later(owner_changed);
                break;
            }
        }
    }
    [[nodiscard]] bool hooked(bool sent) const noexcept { return sent ? sentHook_.is_valid() : messageHook_.is_valid(); }

    std::vector<PopupWindow*> all, open, holding;

private:
    // The message hook while a popup is open or holds closed_by_anchor_press; the sent-message
    // hook, which every SendMessage on the thread passes, only while one is open.
    void update_hooks() noexcept {
        const bool messages = !open.empty() || !holding.empty(), sent = !open.empty();
        if (messages && !messageHook_) { messageHook_.reset(SetWindowsHookExW(WH_GETMESSAGE, message_hook, nullptr, GetCurrentThreadId())); }
        if (!messages) { messageHook_.reset(); }
        if (sent && !sentHook_) { sentHook_.reset(SetWindowsHookExW(WH_CALLWNDPROC, sent_hook, nullptr, GetCurrentThreadId())); }
        if (!sent) { sentHook_.reset(); }
    }
    wil::unique_hhook messageHook_, sentHook_;
};

namespace {
// Presses and the wheel as they are taken from the queue, real or posted: a consumed one becomes
// WM_NULL.
LRESULT CALLBACK message_hook(int code, WPARAM removal, LPARAM data) noexcept {
    if (code == HC_ACTION && removal == PM_REMOVE) {
        auto& message = *reinterpret_cast<MSG*>(data);
        if (PopupTracker::get().input(message.hwnd, message.message, message.wParam, message.lParam, true)) { message.message = WM_NULL; }
    }
    return CallNextHookEx(nullptr, code, removal, data);
}

// Sent presses, which cannot be kept from their target, and the owners' messages.
LRESULT CALLBACK sent_hook(int code, WPARAM sentByThread, LPARAM data) noexcept {
    if (code == HC_ACTION) {
        const auto& message = *reinterpret_cast<const CWPSTRUCT*>(data);
        auto& tracker = PopupTracker::get();
        (void)tracker.input(message.hwnd, message.message, message.wParam, message.lParam, false);
        tracker.owner_message(message.hwnd, message.message, message.wParam, message.lParam);
    }
    return CallNextHookEx(nullptr, code, sentByThread, data);
}
}

namespace detail {
bool popup_hooks_installed(bool sentMessages) noexcept { return PopupTracker::get().hooked(sentMessages); }
}

class PopupWindow::Surface final : public Element {
public:
    explicit Surface(std::uint32_t color) : color_(color) {}
    void set_color(std::uint32_t color) {
        if (color == color_) { return; }
        color_ = color;
        invalidate();
    }

private:
    // The tree clears to transparent, which would show what is behind the popup.
    void paint(ScopedSurfaceDraw& draw) override {
        const auto b = bounds();
        draw.context()->FillRectangle({0, 0, b.width, b.height}, draw.solid_brush(color_));
    }
    std::uint32_t color_;
};

namespace {
Window::Options popup_options(const Window& owner, PopupWindow::Activation activation) {
    THROW_HR_IF_NULL(E_INVALIDARG, owner.hwnd());
    Window::Options options;
    options.style = WS_POPUP | WS_CLIPCHILDREN;
    // A popup that leaves the keyboard with its owner never activates, and stays above the owner
    // without being in the foreground.
    options.extendedStyle = WS_EX_TOOLWINDOW |
        (activation == PopupWindow::Activation::keep_owner_focus ? WS_EX_NOACTIVATE | WS_EX_TOPMOST : 0);
    options.owner = GetAncestor(owner.hwnd(), GA_ROOT);
    return options;
}

RECT work_area(POINT point) noexcept {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info)) {
        return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    return info.rcWork;
}

int rank(PopupWindow::CloseReason reason) noexcept {
    using enum PopupWindow::CloseReason;
    switch (reason) {
    case deactivated: return 1;
    case owner_changed: return 2;
    case outside_press: return 3;
    case anchor_press: return 4;
    default: return 5;
    }
}
}

PopupWindow::PopupWindow(Window& owner, std::shared_ptr<Element> content, std::wstring_view name)
    : PopupWindow(owner, std::move(content), name, Options{}) {}

PopupWindow::PopupWindow(Window& owner, std::shared_ptr<Element> content, std::wstring_view name, const Options& options)
    : PopupWindow(owner, std::make_shared<Surface>(options.background), name, options) {
    set_content(std::move(content));
}

PopupWindow::PopupWindow(Window& owner, std::shared_ptr<Surface> root, std::wstring_view name, const Options& options)
    : ElementWindow(owner.application(), name, 200, 100, root, popup_options(owner, options.activation)),
      owner_(GetAncestor(owner.hwnd(), GA_ROOT)), options_(options), surface_(root.get()) {
    PopupTracker::get().all.push_back(this);
    // Windows 11 rounds the corners; a one-pixel frame extension gives the window its shadow.
    // Earlier versions ignore what they do not support.
    const DWM_WINDOW_CORNER_PREFERENCE corners = options.corners == Corners::round ? DWMWCP_ROUND
        : options.corners == Corners::round_small ? DWMWCP_ROUNDSMALL : DWMWCP_DONOTROUND;
    (void)DwmSetWindowAttribute(hwnd(), DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const MARGINS margins{0, 0, 0, 1};
    (void)DwmExtendFrameIntoClientArea(hwnd(), &margins);
}

PopupWindow::~PopupWindow() {
    *alive_ = false;
    // The owner may be going too, so nothing is handed back to it.
    if (open_) {
        open_ = false;
        raise_menu_event(false);
    }
    PopupTracker::get().forget(*this);
}

UINT PopupWindow::dpi_at(POINT screen) noexcept {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y))) { return GetDpiForSystem(); }
    return x;
}

RECT PopupWindow::owner_area(POINT screen) const {
    const auto work = work_area(screen);
    RECT bounds{}, visible{};
    if (!owner_ || !GetClientRect(owner_, &bounds)) { return work; }
    MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
    return IntersectRect(&visible, &bounds, &work) ? visible : work;
}

void PopupWindow::set_content(std::shared_ptr<Element> content) {
    while (!surface_->children().empty()) { surface_->remove(*surface_->children().back()); }
    if (content) { surface_->add(std::move(content)); }
}

void PopupWindow::set_background(std::uint32_t rgb) {
    options_.background = rgb;
    surface_->set_color(rgb);
}

void PopupWindow::set_dark(bool dark) {
    // The border and shadow follow; a system without dark frames keeps the light ones.
    try { set_dark_title_bar(dark); } catch (...) { detail::log(LogLevel::warning, "event=popup_dark_frame_failed"); }
    set_dark_tooltips(dark);
}

void PopupWindow::show_at(POINT screen, layout::Size size) {
    detail::PopupRequest request;
    request.point = screen;
    request.size = size;
    request.gap = options_.gap;
    request.work = work_area(screen);
    request.dpi = dpi_at(screen);
    anchor_.reset();
    place(detail::place_popup(request));
}

void PopupWindow::show_below(const RECT& anchor, layout::Size size, Below below) {
    const POINT at{anchor.left, anchor.bottom};
    detail::PopupRequest request;
    request.anchor = anchor;
    request.below = below;
    request.size = size;
    request.gap = options_.gap;
    request.work = work_area(at);
    request.dpi = dpi_at(at);
    if (below == Below::trailing_inside_owner) { request.ownerArea = owner_area(at); }
    anchor_ = anchor;
    place(detail::place_popup(request));
}

void PopupWindow::place(RECT bounds) {
    const auto handle = require_hwnd();
    if (!IsWindow(owner_)) { THROW_WIN32(ERROR_INVALID_WINDOW_HANDLE); }
    const bool take = takes_focus();
    open_ = true;
    pending_.reset();
    ++presentation_;
    GetWindowRect(owner_, &ownerRect_);
    PopupTracker::get().opened(*this);
    {
        // Moving onto a monitor with another scale must keep the size worked out for it.
        placing_ = true;
        const auto placed = wil::scope_exit([this] { placing_ = false; });
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(handle, take ? HWND_TOP : HWND_TOPMOST, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_SHOWWINDOW | (take ? 0 : SWP_NOACTIVATE)));
    }
    if (take) {
        SetForegroundWindow(handle);
        // A menu opened from the keyboard shows where the keyboard is at once.
        if (auto* top = application().top_window(owner_)) { application().focus_cue(handle, top->focus_visible()); }
    }
    // The first frame the compositor shows already has the content.
    application().update(*this, [] {});
    // Events raised before a client asked for the tree would be lost.
    if (UiaClientsAreListening()) { ElementAutomation::attach(*this); }
    raise_menu_event(true);
}

void PopupWindow::close() { finish_close(CloseReason::requested); }

void PopupWindow::finish_close(CloseReason reason) noexcept {
    if (!open_ || closing_) { return; }
    const auto alive = alive_;
    closing_ = true;
    const auto done = wil::scope_exit([&] {
        if (*alive) { closing_ = false; }
    });
    // The popups this one owns go first: hiding a window does not hide the windows it owns.
    auto& tracker = PopupTracker::get();
    const auto open = tracker.open;
    for (auto it = open.rbegin(); it != open.rend(); ++it) {
        auto* popup = *it;
        if (popup == this || std::ranges::find(tracker.open, popup) == tracker.open.end() || !popup->hwnd()) { continue; }
        if (contains(popup->hwnd())) { popup->finish_close(CloseReason::owner_changed); }
        if (!*alive) { return; }
    }
    open_ = false;
    pending_.reset();
    tracker.closed(*this);
    const bool destroyed = reason == CloseReason::destroyed || !hwnd();
    try {
        if (!destroyed) {
            raise_menu_event(false);
            const bool take = takes_focus();
            // Give the keyboard back to the window the popup came from, unless the user went
            // elsewhere: taking the foreground back from another application would fail.
            const bool giveBack = take && GetForegroundWindow() == hwnd() && IsWindowEnabled(owner_);
            ShowWindow(hwnd(), SW_HIDE);
            if (!*alive) { return; }
            if (giveBack) { SetForegroundWindow(owner_); }
            if (!*alive) { return; }
            if (take) {
                const auto frame = frame_owner();
                if (frame) { SendMessageW(frame, WM_NCACTIVATE, GetForegroundWindow() == frame || GetForegroundWindow() == owner_, 0); }
            }
            if (!*alive) { return; }
        }
        closed_.emit(reason);
    } catch (...) {
        if (*alive) { application().report_error(std::current_exception()); }
    }
}

void PopupWindow::close_later(CloseReason reason) noexcept {
    if (!open_ || closing_) { return; }
    if (pending_) {
        if (rank(reason) > rank(*pending_)) { pending_ = reason; }
        return;
    }
    pending_ = reason;
    try {
        application().post([this, alive = alive_, presentation = presentation_] {
            if (!*alive || presentation != presentation_ || !pending_) { return; }
            finish_close(*pending_);
        });
    } catch (...) { pending_.reset(); }
}

bool PopupWindow::contains(HWND target) const noexcept {
    const auto self = hwnd();
    if (!target || !self) { return false; }
    if (target == self || IsChild(self, target)) { return true; }
    for (auto window = GetAncestor(target, GA_ROOT); window; window = GetWindow(window, GW_OWNER)) {
        if (window == self) { return true; }
    }
    return false;
}

HWND PopupWindow::frame_owner() const noexcept {
    const auto& tracker = PopupTracker::get();
    auto frame = owner_;
    while (frame && tracker.is_popup(frame)) { frame = GetWindow(frame, GW_OWNER); }
    return frame;
}

void PopupWindow::raise_menu_event(bool opened) noexcept {
    if (options_.menu && hwnd()) { ElementAutomation::menu_event(*this, opened ? UIA_MenuOpenedEventId : UIA_MenuClosedEventId); }
}

std::optional<LRESULT> PopupWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return takes_focus() ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE) {
            // The owner's title bar stays active while its popup has the keyboard.
            if (takes_focus()) {
                if (const auto frame = frame_owner()) { SendMessageW(frame, WM_NCACTIVATE, TRUE, 0); }
            }
        } else if (open_ && !contains(reinterpret_cast<HWND>(lparam))) {
            // Not during the activation change: by the time the close runs, the popup may be shown again.
            close_later(CloseReason::deactivated);
        }
        break;
    case WM_CANCELMODE: {
        // A focused text box takes the message first, for its own capture.
        const auto alive = alive_;
        auto result = ElementWindow::on_message(message, wparam, lparam);
        if (*alive) { finish_close(CloseReason::escape); }
        return result ? result : std::optional<LRESULT>{0};
    }
    case WM_COMMAND:
        // The dialog manager turns Escape into IDCANCEL when nothing in the tree has the focus.
        if (LOWORD(wparam) == IDCANCEL && HIWORD(wparam) == 0 && !lparam) {
            finish_close(CloseReason::escape);
            return 0;
        }
        break;
    case WM_SHOWWINDOW:
        if (!wparam && lparam == SW_PARENTCLOSING && open_) {
            // The owner was minimized; the popup is not shown again when it comes back.
            finish_close(CloseReason::owner_changed);
            return 0;
        }
        if (wparam && lparam == SW_PARENTOPENING && !open_) { return 0; }
        break;
    case WM_DPICHANGED:
        if (placing_) {
            ElementWindow::on_resize();
            return 0;
        }
        if (open_) { close_later(CloseReason::owner_changed); }
        break;
    case WM_DESTROY:
        // The owner is being destroyed, or someone destroyed the window: nothing to hand back.
        if (open_) { finish_close(CloseReason::destroyed); }
        break;
    default: break;
    }
    auto result = ElementWindow::on_message(message, wparam, lparam);
    // Escape that nothing in the popup used closes it.
    if (!result && message == WM_KEYDOWN && wparam == VK_ESCAPE && open_) {
        finish_close(CloseReason::escape);
        return 0;
    }
    return result;
}

}
