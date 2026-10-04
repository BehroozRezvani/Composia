#pragma once

#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <memory>

class DemoWindow : public composia::Window {
public:
    explicit DemoWindow(composia::Application&);
    void redraw();
    [[nodiscard]] composia::Application& application() const noexcept { return app_; }
    [[nodiscard]] composia::CompositionWindowTarget& composition_target() noexcept { return target_; }
    [[nodiscard]] unsigned draw_count() const noexcept { return drawCount_; }

protected:
    void on_resize() override;
    void on_paint() override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;

private:
    void draw_canvas();

    composia::Application& app_;
    composia::CompositionWindowTarget target_;
    composia::composition::ContainerVisual stage_{nullptr};
    composia::composition::SpriteVisual tile_{nullptr};
    composia::composition::SpriteVisual indicator_{nullptr};
    unsigned drawCount_{};
};
