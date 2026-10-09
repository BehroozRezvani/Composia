#pragma once

#include "FormView.hpp"
#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>

namespace builder {

// The window size, for WM_GETMINMAXINFO, whose client area is the given DIP size at the window's DPI.
[[nodiscard]] POINT minimum_track_size(HWND, int widthDip, int heightDip) noexcept;

// The window of a built app: a top-level HWND titled and sized by the design, running its form.
// It cannot be resized below the design's minimum size.
class FormPlayerWindow final : public composia::Window {
public:
    FormPlayerWindow(composia::Application&, Document);
    [[nodiscard]] FormView& view() noexcept { return view_; }
    [[nodiscard]] unsigned draw_count() const noexcept { return drawCount_; }
    [[nodiscard]] composia::CompositionWindowTarget& composition_target() noexcept { return target_; }

private:
    void on_resize() override { invalidate(); }
    void on_paint() override;
    void on_graphics_recreated() override { brush_.reset(); }
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    [[nodiscard]] composia::numerics::float2 pointer(LPARAM) const noexcept;

    composia::Application& app_;
    composia::CompositionWindowTarget target_;
    wil::com_ptr<ID2D1SolidColorBrush> brush_;
    FormView view_;
    composia::Connection changed_;
    unsigned drawCount_{};
};

}
