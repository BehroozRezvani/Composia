#pragma once

#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/VirtualSurface.hpp>
#include <array>

class VirtualDemoWindow final : public composia::Window {
public:
    explicit VirtualDemoWindow(composia::Application&);
    [[nodiscard]] const composia::VirtualSurface& document() const noexcept { return document_; }
    [[nodiscard]] composia::numerics::float2 origin() const noexcept { return origin_; }
    [[nodiscard]] float zoom() const noexcept { return zoom_; }

private:
    void on_resize() override { invalidate(); }
    void on_paint() override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    void draw_tile(composia::ScopedSurfaceDraw&, const RECT&);
    void draw_overlay();
    void set_zoom(float value, composia::numerics::float2 anchor);
    void scroll(bool horizontal, UINT command);

    composia::Application& app_;
    composia::CompositionWindowTarget target_;
    composia::VirtualSurface document_;
    composia::composition::CompositionSurfaceBrush brush_{nullptr};
    composia::composition::SpriteVisual map_{nullptr};
    composia::Button home_, far_, minus_, plus_;
    std::array<composia::Connection, 4> clicks_;
    composia::numerics::float2 origin_{}, viewport_{1, 1};
    float zoom_{1};
    POINT drag_{};
    bool dragging_{};
    std::uint64_t painted_{};
};
