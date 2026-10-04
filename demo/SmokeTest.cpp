#include "SmokeTest.hpp"
#include "DemoWindow.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}
}

SmokeTest::SmokeTest(DemoWindow& window) : window_(window) {
    auto compositor = window.application().compositor();
    probe_ = compositor.CreateSpriteVisual();
    probe_.Size({1.0f, 1.0f});
    window.composition_target().root().Children().InsertAtTop(probe_);
    batch_ = compositor.CreateScopedBatch(composia::composition::CompositionBatchTypes::Animation);
    auto animation = compositor.CreateScalarKeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, 0.0f);
    animation.InsertKeyFrame(1.0f, 1.0f);
    animation.Duration(std::chrono::milliseconds{450});
    probe_.StartAnimation(L"Opacity", animation);
    completedToken_ = batch_.Completed([done = animationCompleted_](const auto&, const auto&) { *done = true; });
    batch_.End();
}

SmokeTest::~SmokeTest() { batch_.Completed(completedToken_); }

void SmokeTest::tick() {
    auto& app = window_.application();
    auto& graphics = app.graphics();
    switch (stage_++) {
    case 0: {
        require(window_.draw_count() > 0, "The initial surface was not drawn");
        require(window_.dpi() != 0, "The HWND has no DPI");
        try {
            composia::ScopedSurfaceDraw draw(window_.composition_target().surface(), graphics, window_.dpi());
            throw std::runtime_error("unwind drawing scope");
        } catch (const std::runtime_error&) {}
        // This BeginDraw fails if the preceding unwinding did not call EndDraw.
        composia::ScopedSurfaceDraw draw(window_.composition_target().surface(), graphics, window_.dpi());
        draw.context()->Clear(D2D1::ColorF(0x101923));
        draw.finish();
        draw.finish();
        require(!draw.context(), "A finished scope retained an invalid drawing context");
        window_.redraw();
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window_.hwnd(), nullptr, 0, 0, 880, 620,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        spdlog::info("event=smoke_scope_unwind_pass");
        break;
    }
    case 1: {
        const auto pixels = window_.client_pixels();
        const auto logical = window_.composition_target().logical_size();
        const auto scale = static_cast<float>(window_.dpi()) / 96.0f;
        require(std::abs(logical.x * scale - static_cast<float>(pixels.cx)) < 0.1f, "DPI width mismatch");
        require(std::abs(logical.y * scale - static_cast<float>(pixels.cy)) < 0.1f, "DPI height mismatch");
        ShowWindow(window_.hwnd(), SW_MINIMIZE);
        spdlog::info("event=smoke_resize_pass dpi={}", window_.dpi());
        break;
    }
    case 2:
        ShowWindow(window_.hwnd(), SW_RESTORE);
        generation_ = graphics.generation();
        app.render([&, inject = true]() mutable {
            if (inject) {
                inject = false;
                throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED);
            }
            window_.redraw();
        });
        require(graphics.generation() == generation_ + 1, "Draw failure did not recreate the device");
        spdlog::info("event=smoke_draw_recovery_pass");
        break;
    case 3:
        generation_ = graphics.generation();
        draws_ = window_.draw_count();
        THROW_IF_WIN32_BOOL_FALSE(SetEvent(graphics.removed_event()));
        break;
    case 4:
        require(graphics.generation() == generation_ + 1, "Idle device notification was not handled");
        require(window_.draw_count() > draws_, "Device replacement did not redraw the surface");
        require(*animationCompleted_, "Composition animation did not complete while the message loop ran");
        spdlog::info("event=smoke_pass generation={} draws={} animation_completed=true", graphics.generation(), window_.draw_count());
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window_.hwnd(), WM_CLOSE, 0, 0));
        break;
    default:
        throw std::runtime_error("Smoke test did not close its window");
    }
}
