#include <composia/Application.hpp>
#include <composia/ScreenCapture.hpp>
#include "support/TestSupport.hpp"
#include <windows.graphics.capture.interop.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

// Windows Graphics Capture of an owned window and of its monitor, through ScreenCapture alone:
// changing pixels, a target resize, a graphics device replacement, stop and restart, and the
// target closing while captured.
using namespace composia;
using testing::require;
using testing::rejects;

namespace {
constexpr UINT_PTR repaintTimer = 30, checkTimer = 31;

// A window painted with GDI: a solid color and a white marker that moves on a timer, so the
// capture keeps producing frames.
class SourceWindow final : public Window {
public:
    explicit SourceWindow(Application& app) : Window(app, L"Composia capture source", 360, 260) {
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), repaintTimer, 40, nullptr) == 0);
        set_bounds({20, 20, 360, 260});
        show(SW_SHOWNOACTIVATE);
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    }
    void fill(COLORREF color) { color_ = color; invalidate(); }

private:
    void on_paint() override {
        const auto dc = GetDC(hwnd());
        THROW_LAST_ERROR_IF_NULL(dc);
        const auto release = wil::scope_exit([&] { ReleaseDC(hwnd(), dc); });
        const auto size = client_pixels();
        const RECT bounds{0, 0, size.cx, size.cy};
        const wil::unique_hbrush brush{CreateSolidBrush(color_)};
        THROW_LAST_ERROR_IF_NULL(brush);
        FillRect(dc, &bounds, brush.get());
        const auto x = static_cast<LONG>(ticks_ % 160);
        const RECT marker{x, 8, x + 30, 28};
        FillRect(dc, &marker, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        GdiFlush();
    }
    void on_resize() override { invalidate(); }
    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM) override {
        if (message == WM_TIMER && wparam == repaintTimer) { ++ticks_; invalidate(); return 0; }
        return std::nullopt;
    }
    COLORREF color_ = RGB(255, 0, 0);
    unsigned ticks_{};
};

// Drives the capture from a timer on its own window, one stage at a time.
class CaptureCheck final : public Window {
public:
    CaptureCheck(Application& app, bool monitor) : Window(app, L"Composia capture check", 240, 120), monitor_(monitor) {
        source_ = std::make_unique<SourceWindow>(app);
        start();
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), checkTimer, 50, nullptr) == 0);
    }
    bool finished() const noexcept { return stage_ == 6; }

private:
    capture::GraphicsCaptureItem item() {
        const auto interop = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        capture::GraphicsCaptureItem item{nullptr};
        if (monitor_) {
            const auto monitor = MonitorFromWindow(source_->hwnd(), MONITOR_DEFAULTTONEAREST);
            THROW_IF_FAILED(interop->CreateForMonitor(monitor, winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            THROW_IF_WIN32_BOOL_FALSE(GetMonitorInfoW(monitor, &info));
            monitorOrigin_ = {info.rcMonitor.left, info.rcMonitor.top};
        } else {
            THROW_IF_FAILED(interop->CreateForWindow(source_->hwnd(), winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
        }
        return item;
    }

    void start() {
        const auto target = item();
        capture_.start(target, application().graphics().d3d_device().get());
        require(capture_.active() && !capture_.target_closed(), "A started capture was not active");
        require(capture_.item() == target && capture_.frame_pool() && capture_.session(), "The capture objects were not exposed");
        frames_ = 0;
    }

    // Takes the latest frame and checks the color at the source window's center.
    bool shows(COLORREF color, SIZE* contentSize = nullptr) {
        auto frame = capture_.next_frame();
        if (!frame) { return false; }
        const auto close = wil::scope_exit([&] { frame.Close(); });
        ++frames_;
        const auto size = frame.ContentSize();
        if (contentSize) { *contentSize = {size.Width, size.Height}; }
        POINT point{size.Width / 2, size.Height / 2};
        if (monitor_) {
            const auto client = source_->client_pixels();
            point = {client.cx / 2, client.cy / 2};
            THROW_IF_WIN32_BOOL_FALSE(ClientToScreen(source_->hwnd(), &point));
            point.x -= monitorOrigin_.x;
            point.y -= monitorOrigin_.y;
        }
        require(point.x >= 0 && point.y >= 0 && point.x < size.Width && point.y < size.Height, "The source window is outside the capture");
        const auto pixel = testing::frame_pixel(application(), frame, static_cast<UINT>(point.x), static_cast<UINT>(point.y));
        return testing::matches(pixel, (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color), 24);
    }

    void check() {
        if (checking_ || finished()) { return; }
        checking_ = true;
        const auto done = wil::scope_exit([&] { checking_ = false; });
        require(std::chrono::steady_clock::now() - started_ < std::chrono::seconds{20}, "Capture stopped making progress");
        auto& graphics = application().graphics();
        switch (stage_) {
        case 0:
            if (!shows(RGB(255, 0, 0)) || frames_ < 3) { return; }
            source_->fill(RGB(0, 255, 0));
            ++stage_;
            break;
        case 1: {
            SIZE before{};
            if (!shows(RGB(0, 255, 0), &before)) { return; }
            beforeResize_ = before;
            source_->set_bounds({20, 20, 510, 340});
            ++stage_;
            break;
        }
        case 2: {
            SIZE size{};
            if (!shows(RGB(0, 255, 0), &size)) { return; }
            // A window capture follows the target's size; the frame pool adapts to it.
            if (!monitor_ && (size.cx <= beforeResize_.cx || size.cy <= beforeResize_.cy)) { return; }
            generation_ = graphics.generation();
            graphics.recreate();
            capture_.recreate(graphics.d3d_device().get());
            frames_ = 0;
            ++stage_;
            break;
        }
        case 3:
            if (!shows(RGB(0, 255, 0)) || frames_ < 3) { return; }
            require(graphics.generation() == generation_ + 1, "The capture did not continue on the replacement device");
            capture_.close();
            require(!capture_.active() && !capture_.next_frame() && !capture_.item(), "A closed capture kept its session");
            capture_.close();  // Closing again is harmless.
            source_->fill(RGB(0, 0, 255));
            start();
            ++stage_;
            break;
        case 4:
            if (!shows(RGB(0, 0, 255)) || frames_ < 3) { return; }
            if (monitor_) {
                std::cout << "capture=monitor pixels=true restart=true recovery=true\n";
                finish();
                return;
            }
            THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(source_->hwnd()));
            ++stage_;
            break;
        case 5:
            if (!capture_.target_closed()) { return; }
            require(!capture_.next_frame(), "A closed target still produced frames");
            std::cout << "capture=window pixels=true resize=true restart=true recovery=true target_closed=true\n";
            finish();
            break;
        }
    }

    void finish() {
        capture_.close();
        stage_ = 6;
        if (source_->hwnd()) { PostMessageW(source_->hwnd(), WM_CLOSE, 0, 0); }
        PostMessageW(hwnd(), WM_CLOSE, 0, 0);
    }

    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM) override {
        if (message == WM_TIMER && wparam == checkTimer) { check(); return 0; }
        return std::nullopt;
    }

    ScreenCapture capture_;
    std::unique_ptr<SourceWindow> source_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    SIZE beforeResize_{};
    POINT monitorOrigin_{};
    std::uint64_t generation_{};
    unsigned stage_{}, frames_{};
    bool monitor_{}, checking_{};
};

int run_capture(const testing::Options& options, bool monitor) {
    if (!ScreenCapture::supported()) {
        std::cout << "skipped: Windows Graphics Capture is unavailable\n";
        return testing::skipped;
    }
    Application app{options.warp};
    {
        ScreenCapture idle;
        require(!idle.active() && !idle.next_frame() && !idle.target_closed(), "An idle capture reported a session");
        require(rejects(RO_E_CLOSED, [&] { idle.recreate(app.graphics().d3d_device().get()); }), "An idle capture accepted recreation");
        require(rejects(E_INVALIDARG, [&] { idle.start(nullptr, app.graphics().d3d_device().get()); }), "A missing capture item was accepted");
        CaptureCheck check{app, monitor};
        check.show(SW_SHOWNOACTIVATE);
        require(app.run() == 0, "The capture loop failed");
        require(check.finished(), "The capture stages did not complete");
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"window", [](const testing::Options& options) { return run_capture(options, false); }},
        {"monitor", [](const testing::Options& options) { return run_capture(options, true); }},
    });
}
