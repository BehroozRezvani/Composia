#include <composia/Accessible.hpp>
#include <composia/Application.hpp>
#include "WindowHelpers.hpp"
#include <UIAutomation.h>
#include <mutex>
#include <stdexcept>

namespace composia {
namespace detail {
struct AccessibleState {
    std::mutex mutex;
    HWND hwnd{};
    Accessible* owner{};
    Application* application{};
    std::wstring name, value, automationId, localizedControlType;
    long controlType{};
    bool focused{}, hasInvoke{}, hasValue{}, readOnly{};
    std::function<void()> invoke;
    std::function<void(std::wstring)> setValue;
};
}

namespace {
HRESULT allocate(VARIANT* result, const std::wstring& text) noexcept {
    result->vt = VT_BSTR;
    result->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    return result->bstrVal ? S_OK : E_OUTOFMEMORY;
}

struct Provider : winrt::implements<Provider, IRawElementProviderSimple, IInvokeProvider, IValueProvider> {
    explicit Provider(std::shared_ptr<detail::AccessibleState> state) : state_(std::move(state)) {}

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
        if (pattern == UIA_InvokePatternId && state_->hasInvoke) { return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(result)); }
        if (pattern == UIA_ValuePatternId && state_->hasValue) { return QueryInterface(__uuidof(IValueProvider), reinterpret_cast<void**>(result)); }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) noexcept override {
        if (!result) { return E_POINTER; }
        VariantInit(result);
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        switch (property) {
        case UIA_ControlTypePropertyId:
            result->vt = VT_I4;
            result->lVal = state_->controlType;
            return S_OK;
        case UIA_NamePropertyId:
            return allocate(result, state_->name);
        case UIA_AutomationIdPropertyId:
            return state_->automationId.empty() ? S_OK : allocate(result, state_->automationId);
        case UIA_LocalizedControlTypePropertyId:
            return state_->localizedControlType.empty() ? S_OK : allocate(result, state_->localizedControlType);
        case UIA_IsEnabledPropertyId:
        case UIA_HasKeyboardFocusPropertyId:
        case UIA_IsKeyboardFocusablePropertyId:
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId: {
            result->vt = VT_BOOL;
            // A disabled window cannot take the focus, so it is not focusable while disabled.
            const bool value = property == UIA_IsEnabledPropertyId || property == UIA_IsKeyboardFocusablePropertyId
                ? detail::effectively_enabled(state_->hwnd)
                : property == UIA_HasKeyboardFocusPropertyId ? state_->focused : true;
            result->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        default:
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) noexcept override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        HWND hwnd{};
        { std::lock_guard lock{state_->mutex}; hwnd = state_->hwnd; }
        return hwnd ? UiaHostProviderFromHwnd(hwnd, result) : UIA_E_ELEMENTNOTAVAILABLE;
    }

    // Runs an action on the UI thread; the control may be gone by then, which is not an error.
    HRESULT post(std::function<void(detail::AccessibleState&)> action) noexcept {
        try {
            const std::weak_ptr weak = state_;
            return state_->application->post([weak, action = std::move(action)] {
                if (const auto state = weak.lock()) {
                    bool alive{};
                    { std::lock_guard guard{state->mutex}; alive = state->owner != nullptr; }
                    if (alive) { action(*state); }
                }
            }) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }

    HRESULT STDMETHODCALLTYPE Invoke() noexcept override {
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        if (!state_->hasInvoke) { return UIA_E_NOTSUPPORTED; }
        if (!detail::effectively_enabled(state_->hwnd)) { return UIA_E_ELEMENTNOTENABLED; }
        return post([](detail::AccessibleState& state) { if (state.invoke) { state.invoke(); } });
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) noexcept override {
        if (!value) { return E_POINTER; }
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        if (!state_->hasValue) { return UIA_E_NOTSUPPORTED; }
        if (state_->readOnly) { return UIA_E_INVALIDOPERATION; }
        if (!detail::effectively_enabled(state_->hwnd)) { return UIA_E_ELEMENTNOTENABLED; }
        try {
            return post([text = std::wstring{value}](detail::AccessibleState& state) { if (state.setValue) { state.setValue(text); } });
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) noexcept override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        *result = SysAllocStringLen(state_->value.data(), static_cast<UINT>(state_->value.size()));
        return *result ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* result) noexcept override {
        if (!result) { return E_POINTER; }
        std::lock_guard lock{state_->mutex};
        if (!state_->owner) { return UIA_E_ELEMENTNOTAVAILABLE; }
        *result = state_->readOnly ? TRUE : FALSE;
        return S_OK;
    }

private:
    std::shared_ptr<detail::AccessibleState> state_;
};

void property_changed(IRawElementProviderSimple* provider, PROPERTYID property, const VARIANT& before, const VARIANT& after) noexcept {
    if (provider && UiaClientsAreListening()) { LOG_IF_FAILED(UiaRaiseAutomationPropertyChangedEvent(provider, property, before, after)); }
}

void text_changed(IRawElementProviderSimple* provider, PROPERTYID property, const std::wstring& before, const std::wstring& after) noexcept {
    if (!provider || !UiaClientsAreListening()) { return; }
    wil::unique_variant old, now;
    old.vt = now.vt = VT_BSTR;
    old.bstrVal = SysAllocStringLen(before.data(), static_cast<UINT>(before.size()));
    now.bstrVal = SysAllocStringLen(after.data(), static_cast<UINT>(after.size()));
    if (old.bstrVal && now.bstrVal) { LOG_IF_FAILED(UiaRaiseAutomationPropertyChangedEvent(provider, property, old, now)); }
}
}

Accessible::Accessible(Window& window, Options options) : window_(&window), state_(std::make_shared<detail::AccessibleState>()) {
    const auto hwnd = window.hwnd();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd);
    if (window.automation_provider()) { throw std::logic_error("The window already has a UI Automation provider"); }
    state_->hwnd = hwnd;
    state_->owner = this;
    state_->application = &window.application();
    state_->name = std::move(options.name);
    state_->controlType = options.controlType;
    state_->hasInvoke = options.invoke != nullptr;
    state_->invoke = std::move(options.invoke);
    state_->hasValue = options.value;
    state_->readOnly = options.setValue == nullptr;
    state_->setValue = std::move(options.setValue);
    state_->automationId = std::move(options.automationId);
    state_->localizedControlType = std::move(options.localizedControlType);
    state_->focused = GetFocus() == hwnd;
    provider_ = winrt::make_self<Provider>(state_).as<IRawElementProviderSimple>().get();
    focusConnection_ = window.on_focus_changed([this](bool focused) { focus_changed(focused); });
    enabledConnection_ = window.on_enabled_changed([this](bool enabled) { enabled_changed(enabled); });
    destroyConnection_ = window.on_destroy([this] { disconnect(); });
    window.set_automation_provider(provider_.get());
}

Accessible::~Accessible() { disconnect(); }

// Runs when this object is destroyed, when the native window is destroyed, and when the Window
// object is destroyed first; after it, nothing here refers to the window any more.
void Accessible::disconnect() noexcept {
    { std::lock_guard lock{state_->mutex}; state_->owner = nullptr; state_->hwnd = nullptr; }
    if (window_ && window_->automation_provider() == provider_.get()) { window_->set_automation_provider(nullptr); }
    window_ = nullptr;
    if (provider_) { LOG_IF_FAILED(UiaDisconnectProvider(provider_.get())); provider_.reset(); }
}

void Accessible::focus_changed(bool focused) {
    { std::lock_guard lock{state_->mutex}; state_->focused = focused; }
    if (focused && provider_ && UiaClientsAreListening()) { LOG_IF_FAILED(UiaRaiseAutomationEvent(provider_.get(), UIA_AutomationFocusChangedEventId)); }
}

void Accessible::enabled_changed(bool enabled) {
    wil::unique_variant before, after;
    before.vt = after.vt = VT_BOOL;
    before.boolVal = enabled ? VARIANT_FALSE : VARIANT_TRUE;
    after.boolVal = enabled ? VARIANT_TRUE : VARIANT_FALSE;
    property_changed(provider_.get(), UIA_IsEnabledPropertyId, before, after);
}

void Accessible::set_name(std::wstring name) {
    std::wstring previous;
    {
        std::lock_guard lock{state_->mutex};
        previous = std::exchange(state_->name, std::move(name));
        name = state_->name;
    }
    text_changed(provider_.get(), UIA_NamePropertyId, previous, name);
}

void Accessible::set_value(std::wstring value) {
    std::wstring previous;
    {
        std::lock_guard lock{state_->mutex};
        previous = std::exchange(state_->value, std::move(value));
        value = state_->value;
    }
    text_changed(provider_.get(), UIA_ValueValuePropertyId, previous, value);
}

void Accessible::raise_invoked() {
    if (provider_ && UiaClientsAreListening()) { LOG_IF_FAILED(UiaRaiseAutomationEvent(provider_.get(), UIA_Invoke_InvokedEventId)); }
}

std::wstring Accessible::name() const {
    std::lock_guard lock{state_->mutex};
    return state_->name;
}

std::wstring Accessible::value() const {
    std::lock_guard lock{state_->mutex};
    return state_->value;
}

}
