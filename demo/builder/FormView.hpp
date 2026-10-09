#pragma once

#include "BuilderModel.hpp"
#include "FormPainter.hpp"
#include "TextField.hpp"
#include <composia/Button.hpp>
#include <composia/Signal.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace builder {

// Runs a document as a working form inside a host window: the framework's real Button and
// TextField controls for those widgets, painted panels, labels, and images, and pointer-driven
// checkboxes and sliders. The designer's preview and built apps share it, so they look the same.
class FormView {
public:
    enum class Interactive { checkbox, slider };
    struct Region {
        Interactive kind;
        unsigned id;
        composia::layout::Rect bounds;
    };
    enum class Pointer { ignored, handled, dragging };

    FormView(composia::Window& host, Document document);
    ~FormView();
    FormView(const FormView&) = delete;
    FormView& operator=(const FormView&) = delete;

    // Resolves the layout for `bounds` (window DIPs) and positions the controls.
    void arrange(composia::layout::Rect bounds);
    // Paints the widgets; call after arrange. Records the interactive regions for pointer input.
    void draw(FormPainter&);
    [[nodiscard]] Pointer pointer_down(composia::numerics::float2 point);
    void pointer_move(composia::numerics::float2 point);
    void pointer_up() noexcept { sliderDrag_.reset(); }
    [[nodiscard]] bool dragging() const noexcept { return sliderDrag_.has_value(); }
    [[nodiscard]] bool interactive_at(composia::numerics::float2 point) const noexcept;

    [[nodiscard]] const Document& document() const noexcept { return document_; }
    [[nodiscard]] composia::layout::Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] const std::vector<Placement>& placements() const noexcept { return placements_; }
    [[nodiscard]] const std::vector<Region>& regions() const noexcept { return regions_; }
    [[nodiscard]] std::optional<composia::layout::Rect> widget_bounds(unsigned id) const noexcept;  // Window DIPs.
    [[nodiscard]] composia::Button* button(unsigned id) noexcept;
    [[nodiscard]] TextField* field(unsigned id) noexcept;
    composia::Connection on_click(std::function<void(const Widget&)> callback) { return clicked_.connect(std::move(callback)); }
    composia::Connection on_change(std::function<void(const Widget&)> callback) { return changed_.connect(std::move(callback)); }

private:
    void set_slider(unsigned id, float x);

    composia::Window& host_;
    Document document_;
    composia::layout::Rect bounds_{};
    std::vector<Placement> placements_;
    std::vector<Region> regions_;
    std::vector<std::pair<unsigned, std::unique_ptr<composia::Button>>> buttons_;
    std::vector<std::pair<unsigned, std::unique_ptr<TextField>>> fields_;
    std::vector<composia::Connection> clicks_;
    std::optional<unsigned> sliderDrag_;
    composia::Signal<const Widget&> clicked_, changed_;
};

}
