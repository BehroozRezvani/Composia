#pragma once

#include "BuilderModel.hpp"
#include "TextField.hpp"
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class BuilderPainter;

// A visual UI builder: a palette and outline, a design surface with drag placement, corner resize
// handles, and anchor pins for edge constraints, an inspector, and a live preview that instantiates
// the framework's real controls. Designs save to a small text format; nothing leaves the PC.
class BuilderDemoWindow final : public composia::Window {
public:
    enum class Hit { palette, outline, anchor_target, anchor_clear, checked, snap, widget, handle, pin, form_corner, preview_checkbox, preview_slider };
    struct Region {
        Hit kind;
        unsigned id;
        composia::layout::Rect bounds;
    };

    explicit BuilderDemoWindow(composia::Application&);
    ~BuilderDemoWindow() override;

    [[nodiscard]] builder::Document& document() noexcept { return history_.document(); }
    [[nodiscard]] const builder::Document& document() const noexcept { return history_.document(); }
    [[nodiscard]] builder::History& history() noexcept { return history_; }
    [[nodiscard]] std::optional<unsigned> selected() const noexcept { return selected_; }
    [[nodiscard]] bool previewing() const noexcept { return preview_ != nullptr; }
    [[nodiscard]] const builder::Document* preview_document() const noexcept;
    [[nodiscard]] const std::wstring& status() const noexcept { return status_; }
    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }
    [[nodiscard]] bool snapping() const noexcept { return snap_; }
    [[nodiscard]] unsigned draw_count() const noexcept { return drawCount_; }
    [[nodiscard]] composia::layout::Rect form_bounds() const noexcept { return form_; }
    // Bounds in window DIPs from the most recent draw, for pointer input and tests.
    [[nodiscard]] std::optional<composia::layout::Rect> region(Hit, unsigned id = 0) const noexcept;
    [[nodiscard]] std::optional<composia::layout::Rect> widget_bounds(unsigned id) const noexcept;
    [[nodiscard]] composia::layout::Rect handle_bounds(unsigned corner) const noexcept;  // 0 top-left, clockwise.
    [[nodiscard]] composia::layout::Rect pin_bounds(builder::Edge) const noexcept;
    [[nodiscard]] composia::CompositionWindowTarget& composition_target() noexcept { return target_; }
    [[nodiscard]] TextField& name_field() noexcept { return name_; }
    [[nodiscard]] TextField& text_field() noexcept { return text_; }
    [[nodiscard]] TextField& x_field() noexcept { return x_; }
    [[nodiscard]] TextField& y_field() noexcept { return y_; }
    [[nodiscard]] TextField& width_field() noexcept { return width_; }
    [[nodiscard]] TextField& height_field() noexcept { return height_; }
    [[nodiscard]] TextField& value_field() noexcept { return value_; }
    [[nodiscard]] TextField& margin_field(builder::Edge) noexcept;
    [[nodiscard]] composia::Button& undo_button() noexcept { return undoButton_; }
    [[nodiscard]] composia::Button& redo_button() noexcept { return redoButton_; }
    [[nodiscard]] composia::Button& preview_button() noexcept { return previewButton_; }
    [[nodiscard]] composia::Button& stop_button() noexcept { return stopButton_; }
    [[nodiscard]] composia::Button& delete_button() noexcept { return deleteButton_; }
    [[nodiscard]] composia::Button& duplicate_button() noexcept { return duplicateButton_; }
    [[nodiscard]] composia::Button* preview_control(unsigned id) noexcept;
    [[nodiscard]] TextField* preview_field(unsigned id) noexcept;

    void select(std::optional<unsigned> id);
    // Adds into the selected panel (or the form) at a free spot. Returns the new id.
    unsigned add_widget(builder::Kind);
    unsigned add_widget(builder::Kind, unsigned parent, int x, int y);
    void remove_selected();
    void duplicate_selected();
    void bring_forward();
    void send_backward();
    void nudge(int dx, int dy);
    bool set_anchor(builder::Edge, builder::Anchor);
    void release_anchor(builder::Edge);
    void cycle_anchor(builder::Edge);  // none, parent, then each sibling.
    void set_margin(builder::Edge, int offset);
    void resize_form(int width, int height);
    void undo();
    void redo();
    void new_document();
    bool save_to(const std::wstring& path);
    bool open_from(const std::wstring& path);
    void save();
    void save_as();
    void open();
    void start_preview();
    void stop_preview();
    void toggle_snap();

private:
    enum class DragKind { none, place, move, resize, pin, form, slider };
    struct Drag {
        DragKind kind{};
        unsigned id{};     // Widget, or the palette kind index for placements.
        unsigned index{};  // Corner or edge.
        composia::numerics::float2 start{}, current{};
        composia::layout::Rect original{};
        bool moved{}, recorded{};
    };
    struct Preview {
        builder::Document document;
        std::vector<std::pair<unsigned, std::unique_ptr<composia::Button>>> buttons;
        std::vector<std::pair<unsigned, std::unique_ptr<TextField>>> fields;
        std::vector<composia::Connection> clicks;
    };
    struct InspectorRows {
        float header{}, name{}, text{}, position{}, size{}, extra{}, anchors{}, actions{}, bottom{};
        bool widget{};
        builder::Kind kind{};
    };

    void on_resize() override { invalidate(); }
    void on_paint() override;
    void on_graphics_recreated() override { brush_.reset(); }
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    void layout_panes();
    void arrange();
    void draw();
    void draw_palette(BuilderPainter&);
    void draw_outline(BuilderPainter&);
    void draw_form(BuilderPainter&);
    void draw_widget(BuilderPainter&, const builder::Widget&, const builder::Placement&, const std::vector<builder::Placement>&);
    void draw_overlay(BuilderPainter&);
    void draw_inspector(BuilderPainter&);
    void draw_status(BuilderPainter&);
    void refresh_placements();
    void sync_inspector();
    void update_controls();
    void notify(std::wstring text);
    void changed(std::string_view key = {});  // Records history before an edit.
    void apply_geometry(builder::Edge, TextField&);
    void apply_position(bool vertical, TextField&);
    void apply_size(bool vertical, TextField&);
    void begin_drag(DragKind, unsigned id, unsigned index, composia::numerics::float2 point);
    void update_drag(composia::numerics::float2 point);
    void end_drag(bool cancel);
    void set_slider(unsigned id, float x);
    [[nodiscard]] const builder::Document& active() const noexcept;
    [[nodiscard]] const builder::Placement* placement(unsigned id) const noexcept;
    [[nodiscard]] std::optional<Region> hit_test(float x, float y) const noexcept;
    [[nodiscard]] std::optional<unsigned> widget_at(composia::numerics::float2 formPoint, bool containersOnly, unsigned exclude) const noexcept;
    [[nodiscard]] std::optional<builder::Anchor> pin_target(builder::Edge, composia::numerics::float2 formPoint) const noexcept;
    [[nodiscard]] composia::layout::Rect parent_rect(unsigned id) const noexcept;  // Form coordinates.
    [[nodiscard]] composia::layout::Rect snapped(composia::layout::Rect desired, unsigned id) const noexcept;
    [[nodiscard]] composia::layout::Rect to_window(composia::layout::Rect form) const noexcept;
    [[nodiscard]] composia::numerics::float2 to_form(composia::numerics::float2 window) const noexcept;
    [[nodiscard]] composia::numerics::float2 pointer(LPARAM) const noexcept;
    [[nodiscard]] std::wstring anchor_label(const builder::Anchor&) const;

    composia::Application& app_;
    composia::CompositionWindowTarget target_;
    wil::com_ptr<ID2D1SolidColorBrush> brush_;
    builder::History history_;
    std::unique_ptr<Preview> preview_;
    std::vector<builder::Placement> placements_;
    std::vector<Region> regions_;
    std::optional<Region> hover_;
    std::optional<unsigned> selected_, dropTarget_;
    std::optional<builder::Anchor> pinTarget_;
    Drag drag_;
    composia::layout::Rect palette_{}, outline_{}, workspace_{}, inspector_{}, form_{};
    InspectorRows rows_;
    std::wstring status_, path_;
    float outlineScroll_{}, outlineExtent_{};
    bool snap_{true}, tracking_{}, syncing_{};
    unsigned drawCount_{};
    // Declaration order is creation order, which sets the Tab order.
    composia::Button newButton_, openButton_, saveButton_, undoButton_, redoButton_, previewButton_, stopButton_;
    TextField name_, text_, x_, y_, width_, height_, value_, marginLeft_, marginTop_, marginRight_, marginBottom_;
    composia::Button deleteButton_, duplicateButton_, frontButton_, backButton_;
    std::array<composia::Connection, 32> connections_;
};
