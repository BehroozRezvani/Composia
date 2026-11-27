#include "TextBoxHost.hpp"
#include <composia/Application.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace composia {
namespace {
struct RichEdit {
    wil::unique_hmodule dll{LoadLibraryExW(L"msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    template <class T> T symbol(const char* name) {
        auto address = GetProcAddress(dll.get(), name);
        THROW_LAST_ERROR_IF_NULL(address);
        return std::bit_cast<T>(address);
    }
    PCreateTextServices create = symbol<PCreateTextServices>("CreateTextServices");
    PShutdownTextServices shutdown = symbol<PShutdownTextServices>("ShutdownTextServices");
    IID services = *symbol<const IID*>("IID_ITextServices2");
    IID host = *symbol<const IID*>("IID_ITextHost");
    IID host2 = *symbol<const IID*>("IID_ITextHost2");
    IID overrides = *symbol<const IID*>("IID_IRicheditUiaOverrides");
};
const RichEdit& rich_edit() {
    static const RichEdit api;
    return api;
}
LONG rounded(float n) {
    return static_cast<LONG>(std::clamp(std::round(static_cast<double>(n)), double(LONG_MIN), double(LONG_MAX)));
}
}

TextBox::Host::Host(TextBox& owner) : box(&owner) {
    fill_format();
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_ALIGNMENT;
    paragraph.wAlignment = PFA_LEFT;
}

void TextBox::Host::fill_format() {
    const auto& o = box->options();
    format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET;
    format.yHeight = rounded(o.fontSize * 15); // DIPs to twips
    format.crTextColor = o.foreground;
    format.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(format.szFaceName, o.fontFamily.c_str());
}

void TextBox::Host::restyle() {
    fill_format();
    if (!engine) { return; }
    THROW_IF_FAILED(engine->OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE | TXTBIT_VIEWINSETCHANGE,
        TXTBIT_CHARFORMATCHANGE | TXTBIT_VIEWINSETCHANGE));
}

void TextBox::Host::initialize() {
    THROW_IF_FAILED(OleInitialize(nullptr));
    ole = true;
    wil::com_ptr<IUnknown> unknown;
    THROW_IF_FAILED(rich_edit().create(nullptr, this, unknown.put()));
    THROW_IF_FAILED(unknown->QueryInterface(rich_edit().services, engine.put_void()));
    send(EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE);
    const DWORD flags = SES_USECTF | SES_LOGICALCARET;
    send(EM_SETEDITSTYLE, flags, flags);
    send(EM_SETEDITSTYLEEX, SES_EX_USEMOUSEWPARAM, SES_EX_USEMOUSEWPARAM);
    send(EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL);
}

void TextBox::Host::shutdown() noexcept {
    deactivate();
    provider.reset();
    box = nullptr;
    if (engine && SUCCEEDED(rich_edit().shutdown(engine.get()))) { (void)engine.detach(); }
    engine.reset();
    if (ole) {
        OleUninitialize();
        ole = false;
    }
}

HRESULT TextBox::Host::QueryInterface(REFIID iid, void** result) noexcept {
    if (!result) { return E_POINTER; }
    *result = nullptr;
    if (iid == IID_IUnknown || iid == rich_edit().host || iid == rich_edit().host2) {
        *result = static_cast<ITextHost2*>(this);
    } else if (iid == __uuidof(IRichEditUiaInformation)) {
        *result = static_cast<IRichEditUiaInformation*>(this);
    } else if (iid == __uuidof(IRawElementProviderWindowlessSite)) {
        *result = static_cast<IRawElementProviderWindowlessSite*>(this);
    } else if (iid == rich_edit().overrides) {
        *result = static_cast<IRicheditUiaOverrides*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}
ULONG TextBox::Host::Release() noexcept {
    const auto count = --references;
    if (!count) { delete this; }
    return count;
}

HWND TextBox::Host::hwnd() const noexcept {
    auto* window = box ? box->host_window() : nullptr;
    return window ? window->hwnd() : nullptr;
}

RECT TextBox::Host::client() const noexcept {
    const auto b = box ? box->bounds() : layout::Rect{};
    return {0, 0, std::max(1L, rounded(b.width)), std::max(1L, rounded(b.height))};
}

void TextBox::Host::sync() {
    if (!hwnd()) { return; }
    auto r = client();
    if (!inPlace) {
        THROW_IF_FAILED(engine->OnTxInPlaceActivate(&r));
        inPlace = true;
        arranged = r;
    } else if (!EqualRect(&r, &arranged)) {
        THROW_IF_FAILED(engine->OnTxPropertyBitsChange(TXTBIT_CLIENTRECTCHANGE | TXTBIT_EXTENTCHANGE, 0));
        arranged = r;
    }
}

LRESULT TextBox::Host::send(UINT msg, WPARAM w, LPARAM l) const {
    LRESULT result{};
    THROW_IF_FAILED(engine->TxSendMessage(msg, w, l, &result));
    return result;
}

std::optional<LRESULT> TextBox::Host::input(UINT msg, WPARAM w, LPARAM l) {
    LRESULT result{};
    auto hr = engine->TxSendMessage(msg, w, l, &result);
    if (hr == S_FALSE || hr == S_MSG_KEY_IGNORED) { return std::nullopt; }
    THROW_IF_FAILED(hr);
    return result;
}

void TextBox::Host::focus(bool value) {
    if (value == active) { return; }
    if (value) {
        sync();
        if (!inPlace) { return; }
        THROW_IF_FAILED(engine->OnTxUIActivate());
        active = true;
        send(WM_SETFOCUS);
        if (box) { box->focusChanged_.emit(true); }
    } else {
        active = false;
        send(WM_KILLFOCUS);
        THROW_IF_FAILED(engine->OnTxUIDeactivate());
        TxSetCapture(FALSE);
        TxDestroyCaret();
        if (box) { box->focusChanged_.emit(false); }
    }
    invalidate();
}
void TextBox::Host::deactivate() noexcept {
    // Clients let go of the provider, which stays: Rich Edit makes one per text services, and
    // refuses another for an editor that leaves a tree and joins one again.
    if (provider) { UiaDisconnectProvider(provider.get()); }
    try {
        focus(false);
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
    if (inPlace) {
        inPlace = false;
        LOG_IF_FAILED(engine->OnTxInPlaceDeactivate());
    }
    TxSetCapture(FALSE);
    TxDestroyCaret();
    timers.clear();
}

void TextBox::Host::invalidate(const RECT* r) noexcept {
    if (!box) { return; }
    try {
        if (!r) {
            box->invalidate();
        } else if (!IsRectEmpty(r)) {
            box->invalidate({float(r->left), float(r->top), float(r->right - r->left), float(r->bottom - r->top)});
        }
    } catch (...) {
        if (auto* w = box->host_window()) { w->application().report_error(std::current_exception()); }
    }
}

BOOL TextBox::Host::TxSetTimer(UINT id, UINT delay) {
    auto* window = box ? box->host_window() : nullptr;
    if (!window || !window->hwnd()) { return FALSE; }
    try {
        auto& timer = timers[id];
        if (!timer) { timer = std::make_unique<Timer>(window->application(), [this, id] { (void)input(WM_TIMER, id, 0); }); }
        timer->start(std::chrono::milliseconds{delay}, true);
        return TRUE;
    } catch (...) { return FALSE; }
}
void TextBox::Host::TxKillTimer(UINT id) {
    if (auto i = timers.find(id); i != timers.end()) { timers.erase(i); }
}

void TextBox::Host::blink(bool restart) noexcept {
    if (blinkTimer) { blinkTimer->stop(); }
    caretOn = restart && active && showCaret;
    auto* window = box ? box->host_window() : nullptr;
    if (caretOn && window && window->hwnd()) {
        const auto delay = GetCaretBlinkTime();
        if (delay && delay != INFINITE) {
            try {
                if (!blinkTimer) {
                    blinkTimer = std::make_unique<Timer>(window->application(), [this] {
                        caretOn = !caretOn;
                        invalidate(&caret);
                    });
                }
                blinkTimer->start(std::chrono::milliseconds{delay}, true);
            } catch (...) { LOG_CAUGHT_EXCEPTION(); }
        }
    }
    invalidate(&caret);
}
BOOL TextBox::Host::TxCreateCaret(HBITMAP bitmap, INT width, INT height) {
    invalidate(&caret);
    const auto flags = reinterpret_cast<UINT_PTR>(bitmap);
    caret.right = caret.left + ((flags & CARET_NULL) ? 0 : std::max(1, width));
    caret.bottom = caret.top + std::max(0, height);
    if (active && hwnd()) { nativeCaret = CreateCaret(hwnd(), nullptr, 1, std::max(1, height)) != FALSE; }
    return TRUE;
}
BOOL TextBox::Host::TxSetCaretPos(INT x, INT y) {
    invalidate(&caret);
    OffsetRect(&caret, x - caret.left, y - caret.top);
    if (nativeCaret && hwnd()) {
        POINT pt{x, y};
        if (TxClientToScreen(&pt) && ScreenToClient(hwnd(), &pt)) { SetCaretPos(pt.x, pt.y); }
    }
    blink(true);
    return TRUE;
}
BOOL TextBox::Host::TxShowCaret(BOOL show) {
    showCaret = show != FALSE;
    blink(true);
    return TRUE;
}
HRESULT TextBox::Host::TxDestroyCaret() {
    showCaret = false;
    blink(false);
    if (nativeCaret) {
        DestroyCaret();
        nativeCaret = false;
    }
    return S_OK;
}

void TextBox::Host::TxSetCapture(BOOL take) {
    if (take && hwnd()) {
        SetCapture(hwnd());
        captured = GetCapture() == hwnd();
    } else if (captured) {
        captured = false;
        if (hwnd() && GetCapture() == hwnd()) { ReleaseCapture(); }
    }
}
void TextBox::Host::TxSetFocus() {
    if (box && box->host_window() && (!box->focused() || !box->host_window()->focused())) {
        try {
            auto& w = *box->host_window();
            w.focus_element(*box);
        } catch (...) { box->host_window()->application().report_error(std::current_exception()); }
    }
}
BOOL TextBox::Host::TxClientToScreen(LPPOINT pt) {
    if (!hwnd()) { return FALSE; }
    auto b = box->viewport_bounds();
    const float scale = box->host_window()->scale();
    pt->x = rounded((b.x + float(pt->x)) * scale);
    pt->y = rounded((b.y + float(pt->y)) * scale);
    return ClientToScreen(hwnd(), pt);
}
BOOL TextBox::Host::TxScreenToClient(LPPOINT pt) {
    if (!hwnd() || !ScreenToClient(hwnd(), pt)) { return FALSE; }
    auto local = box->host_window()->to_dips(*pt);
    auto b = box->viewport_bounds();
    pt->x = rounded(local.x - b.x);
    pt->y = rounded(local.y - b.y);
    return TRUE;
}
HRESULT TextBox::Host::GetBoundaryRectangle(UiaRect* value) noexcept {
    if (!value) { return E_POINTER; }
    *value = {};
    if (!hwnd()) { return UIA_E_ELEMENTNOTAVAILABLE; }
    auto b = box->viewport_bounds(true);
    POINT pt{};
    if (!ClientToScreen(hwnd(), &pt)) { return UIA_E_ELEMENTNOTAVAILABLE; }
    const auto s = box->host_window()->scale();
    *value = {pt.x + double(b.x) * s, pt.y + double(b.y) * s, double(b.width) * s, double(b.height) * s};
    return S_OK;
}
HRESULT TextBox::Host::IsVisible() noexcept {
    UiaRect r{};
    return SUCCEEDED(GetBoundaryRectangle(&r)) && IsWindowVisible(hwnd()) && r.width > 0 && r.height > 0 ? S_OK
                                                                                                         : S_FALSE;
}
HRESULT TextBox::Host::TxGetViewInset(LPRECT r) {
    const LONG inset = box ? rounded(box->options().padding * (2540.f / 96.f)) : 0;
    *r = {inset, inset, inset, inset};
    return S_OK;
}
HRESULT TextBox::Host::TxGetExtent(LPSIZEL s) {
    const auto r = client();
    *s = {MulDiv(r.right, 2540, 96), MulDiv(r.bottom, 2540, 96)};
    return S_OK;
}
COLORREF TextBox::Host::TxGetSysColor(int index) {
    if (box) {
        if (index == COLOR_HIGHLIGHT) { return box->options().selectionColor; }
        if (index == COLOR_HIGHLIGHTTEXT) { return box->options().selectedTextColor; }
        if (index == COLOR_WINDOW) { return box->options().background; }
        if (index == COLOR_WINDOWTEXT) { return box->options().foreground; }
    }
    return GetSysColor(index);
}
HRESULT TextBox::Host::TxGetScrollBars(DWORD* value) {
    *value = box && box->options().multiline ? ES_AUTOVSCROLL : ES_AUTOHSCROLL;
    return S_OK;
}
HRESULT TextBox::Host::TxGetWindowStyles(DWORD* style, DWORD* ex) {
    TxGetScrollBars(style);
    if (box && box->options().multiline) { *style |= ES_MULTILINE | ES_WANTRETURN; }
    *ex = 0;
    return S_OK;
}
HRESULT TextBox::Host::TxGetPropertyBits(DWORD mask, DWORD* bits) {
    DWORD flags = TXTBIT_D2DDWRITE | TXTBIT_HIDESELECTION | TXTBIT_SAVESELECTION | TXTBIT_DISABLEDRAG;
    if (box && box->options().multiline) { flags |= TXTBIT_WORDWRAP | TXTBIT_MULTILINE; }
    if (box && box->options().readOnly) { flags |= TXTBIT_READONLY; }
    if (box && box->options().passwordChar) { flags |= TXTBIT_USEPASSWORD; }
    *bits = flags & mask;
    return S_OK;
}
HRESULT TextBox::Host::TxNotify(DWORD code, void*) {
    if (code == EN_CHANGE || code == EN_SELCHANGE || code == EN_VSCROLL || code == EN_HSCROLL) { invalidate(); }
    if (code == EN_CHANGE && box && !changePending && box->host_window()) {
        try {
            wil::com_ptr<Host> self = this;
            changePending = box->host_window()->application().post([self] {
                self->changePending = false;
                if (self->box && self->box->host_window()) { self->box->changed_.emit(); }
            });
        } catch (...) { return E_FAIL; }
    }
    return S_OK;
}

}
