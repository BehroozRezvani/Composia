#pragma once

#include <composia/Platform.hpp>
#include <exception>
#include <optional>
#include <string_view>

namespace composia {

class Application;

class Window {
public:
    Window(Application&, std::wstring_view title, int widthDip, int heightDip);
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void show(int command = SW_SHOWNORMAL);
    void invalidate();
    void rethrow_callback_error();
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_.get(); }
    [[nodiscard]] const wil::unique_hwnd& native_window() const noexcept { return hwnd_; }
    [[nodiscard]] UINT dpi() const noexcept;
    [[nodiscard]] SIZE client_pixels() const;

protected:
    virtual void on_resize() {}
    virtual void on_paint() {}
    virtual std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) { return std::nullopt; }

private:
    static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT dispatch(HWND, UINT, WPARAM, LPARAM);

    Application& application_;
    wil::unique_hwnd hwnd_;
    std::exception_ptr callbackError_;
};

}
