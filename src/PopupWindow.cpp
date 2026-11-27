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

// The popups on this thread: all of them, and the open ones in the order they opened.
class PopupTracker {
public:
    static PopupTracker& get() noexcept {
        static thread_local PopupTracker tracker;
        return tracker;
    }
    [[nodiscard]] bool is_popup(HWND hwnd) const noexcept {
        return hwnd && std::ranges::any_of(all, [hwnd](const PopupWindow* popup) { return popup->hwnd() == hwnd; });
    }
    std::vector<PopupWindow*> all, open;
};

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
    auto& tracker = PopupTracker::get();
    std::erase(tracker.open, this);
    std::erase(tracker.all, this);
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
    place(detail::place_popup(request));
}

void PopupWindow::place(RECT bounds) {
    const auto handle = require_hwnd();
    if (!IsWindow(owner_)) { THROW_WIN32(ERROR_INVALID_WINDOW_HANDLE); }
    const bool take = takes_focus();
    open_ = true;
    pending_.reset();
    ++presentation_;
    auto& tracker = PopupTracker::get();
    std::erase(tracker.open, this);
    tracker.open.push_back(this);
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
    std::erase(tracker.open, this);
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
