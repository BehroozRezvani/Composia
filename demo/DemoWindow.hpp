#pragma once

#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <memory>

class SmokeTest;

class DemoWindow final : public composia::Window {
public:
    explicit DemoWindow(composia::Application&, bool smokeTest);
    ~DemoWindow() override;
    void redraw();
    [[nodiscard]] composia::Application& application() const noexcept { return app_; }
    [[nodiscard]] composia::CompositionWindowTarget& composition_target() noexcept { return target_; }
    [[nodiscard]] unsigned draw_count() const noexcept { return drawCount_; }

private:
    void on_resize() override;
    void on_paint() override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    void draw_canvas();

    composia::Application& app_;
    composia::CompositionWindowTarget target_;
    composia::composition::ContainerVisual stage_{nullptr};
    composia::composition::SpriteVisual tile_{nullptr};
    composia::composition::SpriteVisual indicator_{nullptr};
    std::unique_ptr<SmokeTest> smoke_;
    unsigned drawCount_{};
};
