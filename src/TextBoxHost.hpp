#pragma once

#include <composia/TextBox.hpp>
#include <composia/ElementWindow.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/Timer.hpp>
#include <imm.h>
#include <richedit.h>
#include <textserv.h>
#include <UIAutomation.h>
#include <atomic>
#include <map>
#include <memory>

namespace composia {

// SDK versions differ on INT versus LONG for the upper limit.
template <class T> T scroll_range_max(BOOL (ITextHost::*)(INT, LONG, T, BOOL));
struct TextBox::Host final : ITextHost2,
                             IRichEditUiaInformation,
                             IRawElementProviderWindowlessSite,
                             IRicheditUiaOverrides {
    explicit Host(TextBox&);
    void initialize();
    void shutdown() noexcept;
    void deactivate() noexcept;
    void sync();
    IRawElementProviderSimple* automation_provider();
    LRESULT send(UINT, WPARAM = 0, LPARAM = 0) const;
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM);
    void focus(bool);
    void invalidate(const RECT* = nullptr) noexcept;
    void blink(bool restart) noexcept;
    // The options' font, colors and padding changed: Rich Edit reads them again.
    void restyle();
    void fill_format();
    HWND hwnd() const noexcept;
    RECT client() const noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetBoundaryRectangle(UiaRect*) noexcept override;
    HRESULT STDMETHODCALLTYPE IsVisible() noexcept override;
    HRESULT STDMETHODCALLTYPE GetPropertyOverrideValue(PROPERTYID, VARIANT*) noexcept override;
    HRESULT STDMETHODCALLTYPE GetAdjacentFragment(NavigateDirection, IRawElementProviderFragment**) noexcept override;
    HRESULT STDMETHODCALLTYPE GetRuntimeIdPrefix(SAFEARRAY**) noexcept override;
    wil::com_ptr<IRawElementProviderSimple> provider;
    LONG siteId{};

    HDC TxGetDC() override { return GetDC(hwnd()); }
    INT TxReleaseDC(HDC dc) override { return ReleaseDC(hwnd(), dc); }
    BOOL TxShowScrollBar(INT, BOOL) override { return FALSE; }
    BOOL TxEnableScrollBar(INT, INT) override { return FALSE; }
    BOOL TxSetScrollRange(INT, LONG, decltype(scroll_range_max(&ITextHost::TxSetScrollRange)), BOOL) override {
        return TRUE;
    }
    BOOL TxSetScrollPos(INT, INT, BOOL) override { return TRUE; }
    void TxInvalidateRect(LPCRECT r, BOOL) override { invalidate(r); }
    void TxViewChange(BOOL) override { invalidate(); }
    BOOL TxCreateCaret(HBITMAP, INT, INT) override;
    BOOL TxShowCaret(BOOL) override;
    BOOL TxSetCaretPos(INT, INT) override;
    BOOL TxSetTimer(UINT, UINT) override;
    void TxKillTimer(UINT) override;
    void TxScrollWindowEx(INT, INT, LPCRECT, LPCRECT, HRGN, LPRECT, UINT) override { invalidate(); }
    void TxSetCapture(BOOL) override;
    void TxSetFocus() override;
    void TxSetCursor(HCURSOR cursor, BOOL) override { SetCursor(cursor); }
    BOOL TxScreenToClient(LPPOINT) override;
    BOOL TxClientToScreen(LPPOINT) override;
    HRESULT TxActivate(LONG* old) override {
        if (old) { *old = 0; }
        return S_OK;
    }
    HRESULT TxDeactivate(LONG) override { return S_OK; }
    HRESULT TxGetClientRect(LPRECT r) override {
        *r = client();
        return S_OK;
    }
    HRESULT TxGetViewInset(LPRECT r) override;
    HRESULT TxGetCharFormat(const CHARFORMATW** value) override {
        *value = &format;
        return S_OK;
    }
    HRESULT TxGetParaFormat(const PARAFORMAT** value) override {
        *value = &paragraph;
        return S_OK;
    }
    COLORREF TxGetSysColor(int) override;
    HRESULT TxGetBackStyle(TXTBACKSTYLE* value) override {
        *value = TXTBACK_TRANSPARENT;
        return S_OK;
    }
    HRESULT TxGetMaxLength(DWORD* value) override {
        const auto limit = box ? box->options().maxLength : 0;
        *value = limit > 0 ? static_cast<DWORD>(limit) : INT_MAX - 1;
        return S_OK;
    }
    HRESULT TxGetScrollBars(DWORD* value) override;
    HRESULT TxGetPasswordChar(TCHAR* value) override {
        *value = box ? box->options().passwordChar : 0;
        return *value ? S_OK : S_FALSE;
    }
    HRESULT TxGetAcceleratorPos(LONG* value) override {
        *value = -1;
        return S_OK;
    }
    HRESULT TxGetExtent(LPSIZEL value) override;
    HRESULT OnTxCharFormatChange(const CHARFORMATW*) override { return S_OK; }
    HRESULT OnTxParaFormatChange(const PARAFORMAT*) override { return S_OK; }
    HRESULT TxGetPropertyBits(DWORD, DWORD*) override;
    HRESULT TxNotify(DWORD, void*) override;
    HIMC TxImmGetContext() override { return hwnd() ? ImmGetContext(hwnd()) : nullptr; }
    void TxImmReleaseContext(HIMC context) override { ImmReleaseContext(hwnd(), context); }
    HRESULT TxGetSelectionBarWidth(LONG* value) override {
        *value = 0;
        return S_OK;
    }
    BOOL TxIsDoubleClickPending() override { return FALSE; }
    HRESULT TxGetWindow(HWND* value) override {
        *value = hwnd();
        return *value ? S_OK : E_FAIL;
    }
    HRESULT TxSetForegroundWindow() override { return S_FALSE; }
    HPALETTE TxGetPalette() override { return nullptr; }
    HRESULT TxGetEastAsianFlags(LONG* value) override {
        *value = 0;
        return S_OK;
    }
    HCURSOR TxSetCursor2(HCURSOR c, BOOL) override { return SetCursor(c); }
    void TxFreeTextServicesNotification() override {}
    HRESULT TxGetEditStyle(DWORD, DWORD* value) override {
        *value = 0;
        return S_OK;
    }
    HRESULT TxGetWindowStyles(DWORD*, DWORD*) override;
    HRESULT TxShowDropCaret(BOOL, HDC, LPCRECT) override { return S_FALSE; }
    HRESULT TxDestroyCaret() override;
    HRESULT TxGetHorzExtent(LONG* value) override {
        *value = client().right;
        return S_OK;
    }

    std::atomic<ULONG> references{1};
    TextBox* box;
    wil::com_ptr<ITextServices2> engine;
    CHARFORMAT2W format{};
    PARAFORMAT2 paragraph{};
    RECT arranged{}, caret{};
    // Rich Edit's timers by its ids, and the caret's blink. They run on the dispatcher queue, so they
    // reach the text services whichever element has the focus, and no window procedure sees them.
    std::map<UINT, std::unique_ptr<Timer>> timers;
    std::unique_ptr<Timer> blinkTimer;
    bool inPlace{}, active{}, captured{}, showCaret{}, caretOn{}, nativeCaret{}, ole{}, changePending{};
};

}
