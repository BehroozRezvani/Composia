#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include "support/TestSupport.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

// An application window through its whole life in the message loop: the first paint, coalesced
// resizes, drawing scopes unwound by exceptions, DPI sizing, minimize and restore, device loss
// while drawing and while idle, and a composition animation completing without a render loop.
using namespace composia;
using testing::require;

namespace {
constexpr UINT_PTR stepTimer = 1;

class Canvas final : public Window {
public:
    explicit Canvas(Application& app) : Window(app, L"Composia smoke test", 640, 420), target_(app.compositor(), app.graphics(), hwnd()) {
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), stepTimer, 300, nullptr) == 0);
        auto compositor = app.compositor();
        probe_ = compositor.CreateSpriteVisual();
        probe_.Size({1.0f, 1.0f});
        target_.root().Children().InsertAtTop(probe_);
        batch_ = compositor.CreateScopedBatch(composition::CompositionBatchTypes::Animation);
        auto animation = compositor.CreateScalarKeyFrameAnimation();
        animation.InsertKeyFrame(0.0f, 0.0f);
        animation.InsertKeyFrame(1.0f, 1.0f);
        animation.Duration(std::chrono::milliseconds{450});
        probe_.StartAnimation(L"Opacity", animation);
        completedToken_ = batch_.Completed([done = animationCompleted_](const auto&, const auto&) { *done = true; });
        batch_.End();
    }
    ~Canvas() override { batch_.Completed(completedToken_); }

    void redraw() {
        (void)target_.render(*this, [&](ScopedSurfaceDraw& draw, numerics::float2 size) {
            draw.context()->Clear(D2D1::ColorF(0x101923));
            draw.context()->FillRectangle({16, 16, size.x - 16, size.y - 16}, draw.solid_brush(0x6FE6C8));
            ++draws_;
        });
    }

private:
    void on_resize() override { invalidate(); }
    void on_paint() override { redraw(); }
    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM) override {
        if (message == WM_TIMER && wparam == stepTimer) { step(); return 0; }
        return std::nullopt;
    }

    void step() {
        auto& app = application();
        auto& graphics = app.graphics();
        switch (stage_++) {
        case 0: {
            require(draws_ > 0, "The initial surface was not drawn");
            require(dpi() != 0, "The HWND has no DPI");
            const auto beforeResize = draws_;
            for (int resize = 0; resize != 32; ++resize) { SendMessageW(hwnd(), WM_SIZE, SIZE_RESTORED, 0); }
            require(draws_ == beforeResize, "Resize events triggered redundant immediate drawing");
            UpdateWindow(hwnd());
            require(draws_ == beforeResize + 1, "Coalesced resize did not paint once");
            require(testing::throws<std::runtime_error>([&] {
                ScopedSurfaceDraw draw{target_.surface(), graphics, dpi()};
                throw std::runtime_error("unwind drawing scope");
            }), "The drawing scope did not unwind");
            // This BeginDraw fails if the unwinding above did not call EndDraw.
            ScopedSurfaceDraw draw{target_.surface(), graphics, dpi()};
            draw.context()->Clear(D2D1::ColorF(0x101923));
            draw.finish();
            draw.finish();
            require(!draw.context(), "A finished scope retained an invalid drawing context");
            redraw();
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd(), nullptr, 0, 0, 880, 620, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            std::cout << "event=smoke_scope_unwind_pass\n";
            break;
        }
        case 1: {
            const auto pixels = client_pixels();
            const auto logical = target_.logical_size();
            require(std::abs(logical.x * scale() - static_cast<float>(pixels.cx)) < 0.1f, "DPI width mismatch");
            require(std::abs(logical.y * scale() - static_cast<float>(pixels.cy)) < 0.1f, "DPI height mismatch");
            show(SW_MINIMIZE);
            const auto beforeMinimized = draws_;
            redraw();
            require(draws_ == beforeMinimized, "A minimized window was painted");
            std::cout << "event=smoke_resize_pass dpi=" << dpi() << '\n';
            break;
        }
        case 2:
            show(SW_RESTORE);
            generation_ = graphics.generation();
            app.render([&, inject = true]() mutable {
                if (inject) {
                    inject = false;
                    throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED);
                }
                redraw();
            });
            require(graphics.generation() == generation_ + 1, "Draw failure did not recreate the device");
            std::cout << "event=smoke_draw_recovery_pass\n";
            break;
        case 3:
            generation_ = graphics.generation();
            drawsBeforeRemoval_ = draws_;
            THROW_IF_WIN32_BOOL_FALSE(SetEvent(graphics.removed_event()));
            break;
        case 4:
            require(graphics.generation() == generation_ + 1, "Idle device notification was not handled");
            require(draws_ > drawsBeforeRemoval_, "Device replacement did not redraw the surface");
            require(*animationCompleted_, "Composition animation did not complete while the message loop ran");
            std::cout << "event=smoke_pass generation=" << graphics.generation() << " draws=" << draws_ << " animation_completed=true\n";
            THROW_IF_WIN32_BOOL_FALSE(PostMessageW(hwnd(), WM_CLOSE, 0, 0));
            break;
        default:
            throw std::runtime_error("Smoke test did not close its window");
        }
    }

    CompositionWindowTarget target_;
    composition::SpriteVisual probe_{nullptr};
    composition::CompositionScopedBatch batch_{nullptr};
    winrt::event_token completedToken_{};
    std::shared_ptr<std::atomic_bool> animationCompleted_ = std::make_shared<std::atomic_bool>(false);
    std::uint64_t generation_{};
    unsigned draws_{}, drawsBeforeRemoval_{};
    int stage_{};
};

int smoke(const testing::Options& options) {
    Application app{options.warp};
    int result{};
    {
        Canvas window{app};
        window.show();
        result = app.run();
    }
    app.close();
    return result;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"lifecycle", smoke}});
}
