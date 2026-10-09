#include "MediaDemoWindow.hpp"
#include <windows.graphics.capture.interop.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

class SourceWindow final : public composia::Window {
public:
    explicit SourceWindow(composia::Application& app) : Window(app, L"Composia WGC test source", 360, 260) {
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), 30, 40, nullptr) == 0);
        set_bounds({20, 20, 360, 260});
        show(SW_SHOWNOACTIVATE);
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    }
    void green() { color_ = RGB(0, 255, 0); invalidate(); }

private:
    void on_paint() override {
        const auto dc = GetDC(hwnd());
        THROW_LAST_ERROR_IF_NULL(dc);
        const auto release = wil::scope_exit([&] { ReleaseDC(hwnd(), dc); });
        const auto size = client_pixels();
        const RECT bounds{0, 0, size.cx, size.cy};
        const auto brush = CreateSolidBrush(color_);
        THROW_LAST_ERROR_IF_NULL(brush);
        const auto cleanup = wil::scope_exit([&] { DeleteObject(brush); });
        FillRect(dc, &bounds, brush);
        const auto x = static_cast<LONG>(ticks_ % 160);
        const RECT marker{x, 8, x + 30, 28};
        FillRect(dc, &marker, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        GdiFlush();
    }
    void on_resize() override { invalidate(); }
    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM) override {
        if (message == WM_TIMER && wparam == 30) { ++ticks_; invalidate(); return 0; }
        return std::nullopt;
    }
    COLORREF color_ = RGB(255, 0, 0);
    unsigned ticks_{};
};

class CaptureTest final : public MediaDemoWindow {
public:
    CaptureTest(composia::Application& app, bool monitor) : MediaDemoWindow(app), monitor_(monitor) {
        set_bounds({570, 20, 760, 600});
        source_ = std::make_unique<SourceWindow>(app);
        select();
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), 31, 50, nullptr) == 0);
    }

private:
    void select() {
        const auto interop = winrt::get_activation_factory<composia::capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        composia::capture::GraphicsCaptureItem item{nullptr};
        if (monitor_) {
            const auto monitor = MonitorFromWindow(source_->hwnd(), MONITOR_DEFAULTTONEAREST);
            THROW_IF_FAILED(interop->CreateForMonitor(monitor, winrt::guid_of<composia::capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            THROW_IF_WIN32_BOOL_FALSE(GetMonitorInfoW(monitor, &info));
            monitorOrigin_ = {info.rcMonitor.left, info.rcMonitor.top};
        } else {
            THROW_IF_FAILED(interop->CreateForWindow(source_->hwnd(), winrt::guid_of<composia::capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
        }
        require(start_capture(item), "Capture target was rejected");
    }

    D3D11_TEXTURE2D_DESC description() const {
        D3D11_TEXTURE2D_DESC desc{};
        if (const auto texture = capture_texture()) { texture->GetDesc(&desc); }
        return desc;
    }

    bool matches(bool green) {
        const auto texture = capture_texture();
        if (!texture) { return false; }
        auto& graphics = application().graphics();
        auto desc = description();
        POINT point{static_cast<LONG>(desc.Width / 2), static_cast<LONG>(desc.Height / 2)};
        if (monitor_) {
            const auto size = source_->client_pixels();
            point = {size.cx / 2, size.cy / 2};
            THROW_IF_WIN32_BOOL_FALSE(ClientToScreen(source_->hwnd(), &point));
            point.x -= monitorOrigin_.x;
            point.y -= monitorOrigin_.y;
        }
        require(point.x >= 0 && point.y >= 0 && static_cast<UINT>(point.x) < desc.Width && static_cast<UINT>(point.y) < desc.Height,
            "Owned source window is outside the captured monitor");
        desc.Width = desc.Height = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        wil::com_ptr<ID3D11Texture2D> readback;
        THROW_IF_FAILED(graphics.d3d_device()->CreateTexture2D(&desc, nullptr, readback.put()));
        const D3D11_BOX region{static_cast<UINT>(point.x), static_cast<UINT>(point.y), 0,
            static_cast<UINT>(point.x + 1), static_cast<UINT>(point.y + 1), 1};
        const auto dc = graphics.d3d_context().get();
        dc->CopySubresourceRegion(readback.get(), 0, 0, 0, 0, texture, 0, &region);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        THROW_IF_FAILED(dc->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto unmap = wil::scope_exit([&] { dc->Unmap(readback.get(), 0); });
        const auto pixel = static_cast<const unsigned char*>(mapped.pData);
        return pixel[0] < 20 && pixel[green ? 1 : 2] > 230 && pixel[green ? 2 : 1] < 20;
    }

    void stop() {
        const auto button = FindWindowExW(hwnd(), nullptr, L"Composia.Window", L"Stop capture");
        require(button != nullptr && IsWindowEnabled(button), "Stop capture control is unavailable");
        SetFocus(button);
        SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
        require(!capturing() && capture_texture() == nullptr, "Stop capture retained a live session or texture");
        require(!IsWindowEnabled(button), "Stop capture remained enabled after stopping");
    }

    void check() {
        if (checking_ || stage_ == 7) { return; }
        checking_ = true;
        const auto finish = wil::scope_exit([&] { checking_ = false; });
        if (std::chrono::steady_clock::now() - started_ >= std::chrono::seconds{20}) {
            std::cerr << "stage=" << stage_ << " frames=" << capture_frames() << " baseline=" << baseline_ << '\n';
            throw std::runtime_error("WGC test stopped making progress");
        }
        require(SUCCEEDED(media_error()), "Capture reported an error");
        switch (stage_) {
        case 0:
            if (capture_frames() < 4 || !matches(false)) { return; }
            source_->green();
            ++stage_;
            break;
        case 1:
            if (!matches(true)) { return; }
            beforeResize_ = description();
            source_->set_bounds({20, 20, 510, 340});
            baseline_ = capture_frames();
            ++stage_;
            break;
        case 2: {
            if (!monitor_ && resizeSteps_ < 8) {
                source_->set_bounds({20, 20, resizeSteps_ % 2 == 0 ? 320.0f : 510.0f, resizeSteps_ % 2 == 0 ? 230.0f : 340.0f});
                ++resizeSteps_;
                baseline_ = capture_frames();
                return;
            }
            const auto desc = description();
            if (capture_frames() < baseline_ + 4 || !matches(true)) { return; }
            if (!monitor_ && (desc.Width <= beforeResize_.Width || desc.Height <= beforeResize_.Height)) { return; }
            set_bounds({570, 20, 800, 640});
            generation_ = application().graphics().generation();
            baseline_ = capture_frames();
            application().graphics().recreate();
            ++stage_;
            break;
        }
        case 3:
            if (capture_frames() < baseline_ + 4 || !matches(true)) { return; }
            require(application().graphics().generation() == generation_ + 1, "Capture did not recover on the replacement device");
            stop();
            select();
            ++stage_;
            break;
        case 4:
            if (capture_frames() < 4 || !matches(true)) { return; }
            if (monitor_) { stop(); }
            else { THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(source_->hwnd())); }
            ++stage_;
            break;
        case 5:
            if (capturing()) { return; }
            require(capture_texture() == nullptr, "Closed target retained its preview texture");
            source_.reset();
            source_ = std::make_unique<SourceWindow>(application());
            select();
            ++stage_;
            break;
        case 6:
            if (capture_frames() < 4 || !matches(false)) { return; }
            std::cout << "capture=" << (monitor_ ? "monitor" : "window") << " pixel_updates=true resize=true restart=true recovery=true close_active=true\n";
            ++stage_;
            PostMessageW(hwnd(), WM_CLOSE, 0, 0);
            PostMessageW(source_->hwnd(), WM_CLOSE, 0, 0);
            break;
        }
    }

    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_TIMER && wparam == 31) { check(); return 0; }
        return MediaDemoWindow::on_message(message, wparam, lparam);
    }
    std::unique_ptr<SourceWindow> source_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    D3D11_TEXTURE2D_DESC beforeResize_{};
    POINT monitorOrigin_{};
    std::uint64_t generation_{};
    unsigned stage_{}, baseline_{}, resizeSteps_{};
    bool monitor_{}, checking_{};
};
}

int main(int argc, char** argv) {
    try {
        const bool warp = argc > 1 && std::string_view{argv[1]} == "--warp";
        const bool monitor = argc > 2 && std::string_view{argv[2]} == "--monitor";
        composia::Application app{warp};
        if (!composia::ScreenCapture::supported() || !composia::TextureSurface::supported(app.compositor(), app.graphics().d3d_device().get())) {
            std::cout << "WGC or composition textures are unsupported on this device\n";
            return 77;
        }
        {
            CaptureTest window{app, monitor};
            window.show(SW_SHOWNOACTIVATE);
            require(app.run() == 0, "Capture message loop failed");
        }
        app.close();
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
