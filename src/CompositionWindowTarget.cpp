#include <composia/CompositionWindowTarget.hpp>
#include <composia/Application.hpp>
#include <composia/GraphicsDevice.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <windows.ui.composition.interop.h>
#include <commctrl.h>
#include <optional>

namespace composia {
namespace {
constexpr wchar_t hostProperty[] = L"Composia.CompositionHost";

HWND checked_window(Window& window, CompositionWindowTarget::Hosting hosting) {
    (void)window.client_pixels();
    const auto hwnd = window.hwnd();
    if (hosting == CompositionWindowTarget::Hosting::shared) {
        THROW_HR_IF_MSG(E_INVALIDARG, !(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP),
            "Shared composition requires Window::Content::composition at window creation");
        THROW_HR_IF_MSG(E_INVALIDARG, GetPropW(hwnd, hostProperty) != nullptr, "This window already has a shared composition target");
        if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) {
            THROW_HR_IF_MSG(E_INVALIDARG, !CompositionWindowTarget::hosts_children(GetParent(hwnd)),
                "Create the parent's shared composition target before its children");
        }
    }
    return hwnd;
}
}

struct CompositionWindowTarget::Host {
    CompositionWindowTarget& owner;
    Application& application;
    Window& window;
    HWND hwnd{}, parent{};
    bool addedTransparent{};

    Host(CompositionWindowTarget& target, Window& window) : owner(target), application(window.application()), window(window), hwnd(window.hwnd()) {
        auto rollback = wil::scope_exit([&] { detach(); });
        parent = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) ? GetParent(hwnd) : nullptr;
        THROW_IF_WIN32_BOOL_FALSE(SetPropW(hwnd, hostProperty, this));
        THROW_IF_WIN32_BOOL_FALSE(SetWindowSubclass(hwnd, window_proc, 0, reinterpret_cast<DWORD_PTR>(this)));
        if (parent) {
            const auto host = static_cast<Host*>(GetPropW(parent, hostProperty));
            owner.target_.Root(nullptr);
            owner.frame_.RelativeSizeAdjustment({0, 0});
            owner.frame_.Clip(application.compositor().CreateInsetClip());
            host->owner.frame_.Children().InsertAtTop(owner.frame_);
            const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            addedTransparent = !(style & WS_EX_TRANSPARENT);
            SetLastError(0);
            const auto previous = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
            THROW_LAST_ERROR_IF(!previous && GetLastError());
        }
        refresh();
        reorder();
        rollback.release();
    }

    ~Host() { detach(); }

    void detach() noexcept {
        if (hwnd) {
            RemoveWindowSubclass(hwnd, window_proc, 0);
            RemovePropW(hwnd, hostProperty);
            if (addedTransparent) { SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT); }
            hwnd = nullptr;
        }
        try {
            owner.frame_.IsVisible(false);
            if (const auto visual = owner.frame_.Parent()) { visual.Children().Remove(owner.frame_); }
        } catch (...) { LOG_CAUGHT_EXCEPTION(); }
    }

    void refresh() {
        THROW_HR_IF_MSG(E_INVALIDARG, static_cast<bool>(parent) != static_cast<bool>(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD),
            "A shared composition window cannot change between child and top-level");
        THROW_HR_IF_MSG(E_INVALIDARG, parent && (GetParent(hwnd) != parent || !hosts_children(parent)),
            "A shared composition pane cannot be reparented or outlive its parent target");
        owner.frame_.IsVisible((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE) != 0);
        RECT bounds{};
        THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hwnd, &bounds));
        if (bounds.right != owner.pixels_.cx || bounds.bottom != owner.pixels_.cy || window.dpi() != owner.dpi_) { window.invalidate(); }
        if (!parent) { return; }
        POINT origin{};
        MapWindowPoints(hwnd, parent, &origin, 1);
        owner.frame_.Offset({static_cast<float>(origin.x), static_cast<float>(origin.y), 0});
        owner.frame_.Size({static_cast<float>(bounds.right), static_cast<float>(bounds.bottom)});
        RECT region{};
        const auto kind = GetWindowRgnBox(hwnd, &region);
        THROW_HR_IF_MSG(E_INVALIDARG, kind == COMPLEXREGION, "Shared composition panes support rectangular HWND regions only");
        if (kind == ERROR) { region = bounds; }
        else {
            RECT window{};
            THROW_IF_WIN32_BOOL_FALSE(GetWindowRect(hwnd, &window));
            POINT client{};
            THROW_IF_WIN32_BOOL_FALSE(ClientToScreen(hwnd, &client));
            OffsetRect(&region, window.left - client.x, window.top - client.y);
            if (!IntersectRect(&region, &region, &bounds)) { SetRectEmpty(&region); }
        }
        const auto clip = owner.frame_.Clip().as<composition::InsetClip>();
        clip.LeftInset(static_cast<float>(region.left));
        clip.TopInset(static_cast<float>(region.top));
        clip.RightInset(static_cast<float>(bounds.right - region.right));
        clip.BottomInset(static_cast<float>(bounds.bottom - region.bottom));
    }

    void reorder() {
        if (!parent) { return; }
        const auto host = static_cast<Host*>(GetPropW(parent, hostProperty));
        THROW_HR_IF(E_INVALIDARG, !host);
        const auto children = host->owner.frame_.Children();
        const auto first = GetWindow(parent, GW_CHILD);
        for (auto child = first ? GetWindow(first, GW_HWNDLAST) : nullptr; child; child = GetWindow(child, GW_HWNDPREV)) {
            if (const auto sibling = static_cast<Host*>(GetPropW(child, hostProperty))) {
                children.Remove(sibling->owner.frame_);
                children.InsertAtTop(sibling->owner.frame_);
            }
        }
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) noexcept {
        const auto self = reinterpret_cast<Host*>(data);
        if (message == WM_NCDESTROY) {
            self->detach();
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        if (message == WM_WINDOWPOSCHANGING) { reinterpret_cast<WINDOWPOS*>(lparam)->flags |= SWP_NOCOPYBITS; }
        const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
        if (GetPropW(hwnd, hostProperty) != self) { return result; }
        try {
            if (message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW || message == WM_DPICHANGED_AFTERPARENT) { self->refresh(); }
            if (message == WM_WINDOWPOSCHANGED && !(reinterpret_cast<const WINDOWPOS*>(lparam)->flags & SWP_NOZORDER)) { self->reorder(); }
        } catch (...) { self->application.report_error(std::current_exception()); }
        return result;
    }
};

bool CompositionWindowTarget::hosts_children(HWND hwnd) noexcept {
    return hwnd && GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId() && GetPropW(hwnd, hostProperty);
}

CompositionWindowTarget::CompositionWindowTarget(Window& window, Hosting hosting)
    : CompositionWindowTarget(window.application().compositor(), window.application().graphics(), checked_window(window, hosting)) {
    if (hosting == Hosting::shared) {
        ensure_frame();
        host_ = std::make_unique<Host>(*this, window);
    }
}

CompositionWindowTarget::CompositionWindowTarget(const composition::Compositor& compositor,
    GraphicsDevice& graphics, HWND hwnd) : hwnd_(hwnd) {
    THROW_IF_FAILED(compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>()
        ->CreateDesktopWindowTarget(hwnd, FALSE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target_))));
    root_ = compositor.CreateContainerVisual();
    target_.Root(root_);
    surface_ = graphics.composition_device().CreateDrawingSurface({1, 1},
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
    brush_ = compositor.CreateSurfaceBrush(surface_);
    brush_.Stretch(composition::CompositionStretch::Fill);
    canvas_ = compositor.CreateSpriteVisual();
    canvas_.Brush(brush_);
    root_.Children().InsertAtBottom(canvas_);
}

CompositionWindowTarget::~CompositionWindowTarget() {
    host_.reset();
    try {
        target_.Root(nullptr);
        target_.Close();
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
    }
}

void CompositionWindowTarget::ensure_frame() {
    if (frame_) { return; }
    const auto frame = root_.Compositor().CreateContainerVisual();
    frame.RelativeSizeAdjustment({1, 1});
    target_.Root(nullptr);
    auto rollback = wil::scope_exit([&] {
        try {
            if (root_.Parent() == frame) { frame.Children().Remove(root_); }
            target_.Root(root_);
        } catch (...) { LOG_CAUGHT_EXCEPTION(); }
    });
    frame.Children().InsertAtTop(root_);
    target_.Root(frame);
    frame_ = frame;
    rollback.release();
}

void CompositionWindowTarget::set_background(winrt::Windows::UI::Color color) {
    ensure_frame();
    if (!background_) {
        const auto compositor = frame_.Compositor();
        background_ = compositor.CreateColorBrush(color);
        const auto fill = compositor.CreateSpriteVisual();
        fill.RelativeSizeAdjustment({1, 1});
        fill.Brush(background_);
        frame_.Children().InsertAtBottom(fill);
    } else { background_.Color(color); }
}

void CompositionWindowTarget::resize(SIZE pixels, UINT dpi) {
    if (pixels.cx <= 0 || pixels.cy <= 0 || dpi == 0) {
        return;
    }
    if (pixels.cx != pixels_.cx || pixels.cy != pixels_.cy) {
        THROW_IF_FAILED(surface_.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>()->Resize(pixels));
        pixels_ = pixels;
        complete_ = false;  // Resizing discards the content.
    }
    if (dpi != dpi_) {
        dpi_ = dpi;
        complete_ = false;
    }
    const auto scale = static_cast<float>(dpi) / 96.0f;
    logicalSize_ = {static_cast<float>(pixels.cx) / scale, static_cast<float>(pixels.cy) / scale};
    root_.Size(logicalSize_);
    root_.Scale({scale, scale, 1.0f});
    canvas_.Size(logicalSize_);
}

bool CompositionWindowTarget::render(Window& window, const Painter& paint) {
    const auto pixels = window.client_pixels();
    THROW_HR_IF(E_INVALIDARG, window.hwnd() != hwnd_);
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(window.hwnd())) {
        return false;
    }
    auto& application = window.application();
    const auto requested = window.paint_rect();
    bool painted{};
    application.render([&] {
        const auto dpi = window.dpi();
        resize(pixels, dpi);
        auto& graphics = application.graphics();
        std::optional<RECT> update;
        // Composition keeps the content outside an update, so a partial repaint is enough while
        // the surface holds a complete frame from the current device.
        if (requested && complete_ && generation_ == graphics.generation()) {
            const RECT whole{0, 0, pixels.cx, pixels.cy};
            RECT area{};
            if (!IntersectRect(&area, &*requested, &whole)) { return; }
            if (!EqualRect(&area, &whole)) { update = area; }
        }
        complete_ = false;
        std::optional<ScopedSurfaceDraw> draw;
        if (update) { draw.emplace(surface_, graphics, dpi, *update); }
        else { draw.emplace(surface_, graphics, dpi); }
        paint(*draw, logicalSize_);
        draw->finish();
        complete_ = true;
        generation_ = graphics.generation();
        painted = true;
    });
    return painted;
}

}
