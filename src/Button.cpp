#include <composia/Button.hpp>
#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <UIAutomation.h>
#include <windowsx.h>
#include <algorithm>
#include <mutex>
#include <string>

namespace composia {
namespace detail {
struct ButtonState {
    std::mutex mutex;
    HWND hwnd{};
    Button* owner{};
    Application* application{};
    std::wstring name;
    bool focused{};
};
}

namespace {
HWND require_parent(const Window& parent) {
    const auto hwnd = parent.hwnd();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd);
    return hwnd;
}

bool effectively_enabled(HWND hwnd) noexcept {
    if (!hwnd) { return false; }
    for (auto current = hwnd; current; current = GetParent(current)) {
        if (!IsWindowEnabled(current)) { return false; }
        if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD)) { break; }
    }
    return true;
}

struct ButtonProvider : winrt::implements<ButtonProvider, IRawElementProviderSimple, IInvokeProvider> {
    explicit ButtonProvider(std::shared_ptr<detail::ButtonState> state) : state_(std::move(state)) {}

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) noexcept override {
        if (!value) { return E_POINTER; }
        *value = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) noexcept override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        return pattern == UIA_InvokePatternId ? QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(result)) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) noexcept override {
        if (!result) { return E_POINTER; }
        VariantInit(result);
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        if (property == UIA_ControlTypePropertyId) {
            result->vt = VT_I4;
            result->lVal = UIA_ButtonControlTypeId;
        } else if (property == UIA_NamePropertyId) {
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocStringLen(state_->name.data(), static_cast<UINT>(state_->name.size()));
            if (!result->bstrVal) { return E_OUTOFMEMORY; }
        } else if (property == UIA_IsEnabledPropertyId || property == UIA_HasKeyboardFocusPropertyId ||
                   property == UIA_IsKeyboardFocusablePropertyId || property == UIA_IsControlElementPropertyId ||
                   property == UIA_IsContentElementPropertyId) {
            result->vt = VT_BOOL;
            const bool value = property == UIA_IsEnabledPropertyId ? effectively_enabled(state_->hwnd) :
                property == UIA_HasKeyboardFocusPropertyId ? state_->focused : true;
            result->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) noexcept override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        HWND hwnd{};
        { std::lock_guard lock{state_->mutex}; hwnd = state_->hwnd; }
        return hwnd ? UiaHostProviderFromHwnd(hwnd, result) : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE Invoke() noexcept override {
        try {
            std::lock_guard lock{state_->mutex};
            if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
            if (!effectively_enabled(state_->hwnd)) { return UIA_E_ELEMENTNOTENABLED; }
            const std::weak_ptr weak = state_;
            return state_->application->post([weak] {
                if (const auto state = weak.lock()) {
                    Button* button{};
                    { std::lock_guard guard{state->mutex}; button = state->owner; }
                    if (button) { button->invoke(); }
                }
            }) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
        } catch (const winrt::hresult_error& error) { return error.code(); }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }

private:
    std::shared_ptr<detail::ButtonState> state_;
};

void enabled_event(IRawElementProviderSimple* provider, bool oldValue, bool newValue) {
    if (!provider || !UiaClientsAreListening()) { return; }
    VARIANT before{}, after{};
    before.vt = after.vt = VT_BOOL;
    before.boolVal = oldValue ? VARIANT_TRUE : VARIANT_FALSE;
    after.boolVal = newValue ? VARIANT_TRUE : VARIANT_FALSE;
    LOG_IF_FAILED(UiaRaiseAutomationPropertyChangedEvent(provider, UIA_IsEnabledPropertyId, before, after));
}
}

Button::Button(Window& parent, std::wstring_view label)
    : Window(parent.application(), label, 160, 44, require_parent(parent)),
      target_(application().compositor(), application().graphics(), hwnd()),
      text_(application().graphics().text_factory().get(), label, 15, DWRITE_FONT_WEIGHT_SEMI_BOLD),
      state_(std::make_shared<detail::ButtonState>()) {
    state_->hwnd = hwnd();
    state_->owner = this;
    state_->application = &application();
    state_->name = label;
    provider_ = winrt::make_self<ButtonProvider>(state_).as<IRawElementProviderSimple>().get();
    THROW_IF_FAILED(text_.layout()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
    THROW_IF_FAILED(text_.layout()->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    invalidate();
}

Button::~Button() { disconnect_provider(); }

void Button::disconnect_provider() noexcept {
    { std::lock_guard lock{state_->mutex}; state_->owner = nullptr; state_->hwnd = nullptr; }
    if (provider_) { LOG_IF_FAILED(UiaDisconnectProvider(provider_.get())); provider_.reset(); }
}

void Button::enabled(bool value) { EnableWindow(require_hwnd(), value); }
bool Button::enabled() const noexcept { return effectively_enabled(hwnd()); }

void Button::invoke() {
    (void)require_hwnd();
    if (!enabled()) { return; }
    if (provider_ && UiaClientsAreListening()) {
        LOG_IF_FAILED(UiaRaiseAutomationEvent(provider_.get(), UIA_Invoke_InvokedEventId));
    }
    clicked_.emit();
}

void Button::on_resize() { invalidate(); }
void Button::on_graphics_recreated() { brush_.reset(); }

void Button::on_paint() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0) { return; }
    application().render([&] {
        target_.resize(pixels, dpi());
        ScopedSurfaceDraw draw{target_.surface(), application().graphics(), dpi()};
        const auto dc = draw.context().get();
        const auto size = target_.logical_size();
        dc->Clear(D2D1::ColorF(0x101923));
        if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0x6FE6C8), brush_.put())); }
        const bool pressed = keyPressed_ || (mousePressed_ && hovered_);
        const UINT32 fill = !enabled() ? 0x35434B : pressed ? 0x3DAB93 : hovered_ ? 0xA2F5DF : 0x6FE6C8;
        brush_->SetColor(D2D1::ColorF(fill));
        const auto bounds = D2D1::RoundedRect({2, 2, std::max(2.0f, size.x - 2), std::max(2.0f, size.y - 2)}, 8, 8);
        dc->FillRoundedRectangle(bounds, brush_.get());
        if (GetFocus() == hwnd()) {
            brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White));
            dc->DrawRoundedRectangle(bounds, brush_.get(), 2);
        }
        text_.resize(std::max(1.0f, size.x), std::max(1.0f, size.y));
        brush_->SetColor(D2D1::ColorF(enabled() ? 0x101923 : 0x93A9B5));
        dc->DrawTextLayout({0, 0}, text_.layout().get(), brush_.get());
        draw.finish();
    });
}

bool Button::hit_test(LPARAM position) const {
    const auto size = client_pixels();
    const layout::Rect bounds{0, 0, static_cast<float>(size.cx), static_cast<float>(size.cy)};
    return bounds.contains(static_cast<float>(GET_X_LPARAM(position)), static_cast<float>(GET_Y_LPARAM(position)));
}

void Button::cancel_press() {
    mousePressed_ = keyPressed_ = false;
    if (GetCapture() == hwnd()) { ReleaseCapture(); }
    invalidate();
}

std::optional<LRESULT> Button::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETOBJECT:
        if (static_cast<LONG>(lparam) == UiaRootObjectId && provider_) {
            return UiaReturnRawElementProvider(hwnd(), wparam, lparam, provider_.get());
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | ((wparam == VK_SPACE || wparam == VK_RETURN) ? DLGC_WANTMESSAGE : 0);
    case WM_MOUSEMOVE: {
        const auto inside = hit_test(lparam);
        if (hovered_ != inside) { hovered_ = inside; invalidate(); }
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd(), 0};
        THROW_IF_WIN32_BOOL_FALSE(TrackMouseEvent(&tracking));
        return 0;
    }
    case WM_MOUSELEAVE:
        hovered_ = false;
        invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        if (enabled() && hit_test(lparam)) {
            SetFocus(hwnd());
            SetCapture(hwnd());
            hovered_ = mousePressed_ = true;
            invalidate();
        }
        return 0;
    case WM_LBUTTONUP: {
        const bool activate = mousePressed_ && hit_test(lparam);
        cancel_press();
        if (activate) { invoke(); }
        return 0;
    }
    case WM_CAPTURECHANGED:
        mousePressed_ = false;
        invalidate();
        return 0;
    case WM_CANCELMODE:
        cancel_press();
        return 0;
    case WM_KEYDOWN:
        if (enabled() && GetFocus() == hwnd() && !(lparam & (1LL << 30))) {
            if (wparam == VK_SPACE) { keyPressed_ = true; invalidate(); return 0; }
            if (wparam == VK_RETURN) { invoke(); return 0; }
        }
        break;
    case WM_KEYUP:
        if (wparam == VK_SPACE) {
            const bool activate = keyPressed_;
            keyPressed_ = false;
            invalidate();
            if (activate) { invoke(); }
            return 0;
        }
        break;
    case WM_CHAR:
        if (wparam == VK_SPACE || wparam == VK_RETURN) { return 0; }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        { std::lock_guard lock{state_->mutex}; state_->focused = message == WM_SETFOCUS; }
        if (message == WM_KILLFOCUS) { cancel_press(); }
        else {
            invalidate();
            if (provider_ && UiaClientsAreListening()) { LOG_IF_FAILED(UiaRaiseAutomationEvent(provider_.get(), UIA_AutomationFocusChangedEventId)); }
        }
        return 0;
    }
    case WM_ENABLE: {
        const bool value = wparam != 0;
        if (!value) { cancel_press(); }
        invalidate();
        enabled_event(provider_.get(), !value, value);
        return 0;
    }
    case WM_DESTROY:
        disconnect_provider();
        return 0;
    }
    return std::nullopt;
}

}
