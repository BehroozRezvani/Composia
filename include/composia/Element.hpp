#pragma once

#include <composia/Input.hpp>
#include <composia/Layout.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace composia {

class Element;
class ElementTree;
class ElementWindow;
class HostedWindow;
class ScopedSurfaceDraw;
struct ElementAccessibility;
enum class SelectionAction;

struct ElementLayout {
    enum class Kind { overlay, stack, flex, grid, scroll, canvas };
    Kind kind{Kind::overlay};
    layout::Axis axis{layout::Axis::vertical}; // Stack, flex and scroll direction.
    float gap{};                              // Both axes for a grid.
    std::size_t columns{1};                   // Equal width, row-major grid.
    std::optional<float> viewportLength{};    // Intrinsic length of a scroll viewport.
    layout::Insets padding{};                 // Space between the element's edges and its children.
};

// A UI-thread element. A parent owns its children. Siblings lay out and take focus in
// insertion order, and paint in stable z order. All drawing and input clip to each ancestor.
class Element {
public:
    explicit Element(ElementLayout = {});
    virtual ~Element();
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    void add(std::shared_ptr<Element>);
    void insert(std::size_t index, std::shared_ptr<Element>);
    void remove(Element&);
    [[nodiscard]] std::span<const std::shared_ptr<Element>> children() const noexcept { return children_; }
    [[nodiscard]] Element* parent() const noexcept { return parent_; }

    void set_layout(ElementLayout);
    void set_preferred_size(layout::Size);     // Intrinsic size, before constraints and children.
    void set_flex_grow(float weight);
    void set_offset(layout::Point);            // Translation after layout, in parent DIPs.
    void set_z_index(int);
    void set_focusable(bool);
    [[nodiscard]] bool focusable() const noexcept { return focusable_; }
    void set_enabled(bool);
    [[nodiscard]] bool enabled() const noexcept; // Includes retained ancestors.
    [[nodiscard]] bool interactive() const noexcept; // Also checks the host HWND.
    void set_accessibility(ElementAccessibility);
    void clear_accessibility();
    void set_accessible_value(double);         // Update RangeValue and notify clients.
    void set_accessible_value(std::wstring);   // Update Value and notify clients.
    // Text an ElementWindow shows in a tooltip after the pointer rests on the element, or on a
    // descendant without a tooltip of its own, and after keyboard focus reaches it. It is also
    // the element's UI Automation help text when the accessibility description has none.
    // Empty for no tooltip.
    void set_tooltip(std::wstring);
    [[nodiscard]] const std::wstring& tooltip() const noexcept { return tooltip_; }
    [[nodiscard]] const ElementAccessibility* accessibility() const noexcept { return accessibility_.get(); }
    bool select_item(SelectionAction);
    // Scroll containers have at most one child. Scroll distance is clamped during layout.
    void set_scroll_offset(float);
    [[nodiscard]] float scroll_offset() const noexcept { return scrollOffset_; }
    [[nodiscard]] float scroll_limit() const noexcept { return scrollLimit_; }
    [[nodiscard]] bool focused() const noexcept;
    bool request_focus();
    [[nodiscard]] bool hovered() const noexcept;
    [[nodiscard]] layout::Rect bounds() const noexcept { return bounds_; } // In parent DIPs.
    [[nodiscard]] layout::Size desired_size() const noexcept { return desired_; }
    // Bounds in viewport DIPs, optionally intersected with all ancestor clips.
    [[nodiscard]] layout::Rect viewport_bounds(bool clipped = false) const noexcept;

    void invalidate();
    void invalidate(layout::Rect local);
    void invalidate_measure();

protected:
    [[nodiscard]] ElementWindow* host_window() const noexcept;
    void update_host_layout();
    // Additional enabled gate for controls bound to a command.
    void set_available(bool);
    bool capture_pointer();
    void release_pointer() noexcept;
    virtual void on_capture_lost() noexcept {}
    virtual void on_hover_changed(bool) {}
    // Opt in to a callback after arrangement, when child membership may safely change.
    void observe_layout(bool value = true) noexcept { observeLayout_ = value; }
    virtual void on_layout_updated() {}
    // Host lifecycle hooks may invalidate paint. Leave tree structure and focus unchanged.
    virtual void on_focus_changed(bool) {}
    virtual void on_detaching() noexcept {}
    // Available dimensions can be +infinity. Return finite nonnegative DIPs. Layout and paint
    // hooks must leave the tree structurally unchanged. Input handlers may edit the tree.
    virtual layout::Size measure_content(layout::Size available) const;
    // Origin is local (0,0). The clip includes the surface update and all ancestor bounds.
    virtual void paint(ScopedSurfaceDraw&) {}
    // Draws above the children, with the same origin and clip as paint: a scrollbar over content.
    virtual void paint_over(ScopedSurfaceDraw&) {}
    // True to receive pointer input at this local point itself rather than through a child
    // there, such as a scrollbar over content. Hit-testing asks before it looks at children.
    [[nodiscard]] virtual bool claims_pointer(layout::Point) const { return false; }
    // The deepest hit or focused element is called first. Return true to stop bubbling.
    virtual bool on_pointer(const PointerEvent&) { return false; }
    virtual bool on_key(const KeyEvent&) { return false; }
    // A context menu request, from the element under the pointer or the focused element up.
    virtual bool on_context_menu(const ContextMenuEvent&) { return false; }
    virtual bool on_text_input(std::wstring_view) { return false; }

private:
    friend class ElementTree;
    friend class HostedWindow;
    friend class HostedVisual;
    friend struct ElementAutomation;
    friend struct ElementProvider;
    void attach(ElementTree*) noexcept;
    void check_mutation() const;
    bool accessibility_key(const KeyEvent&);
    void enabled_changed();
    void mark_layout(bool measure);
    void arrange(layout::Rect);
    layout::Size measure(layout::Size);
    layout::Size layout_children(layout::Size, bool arranging, layout::Point origin = {});
    std::span<Element* const> visible_children(layout::Rect local, bool point = false);
    void draw(ScopedSurfaceDraw&, layout::Point origin, layout::Rect clip);
    Element* hit(layout::Point local);
    bool reveal(bool scroll);
    Element* last_descendant() noexcept;
    Element* adjacent(bool backwards) noexcept;

    bool observeLayout_{}, hover_{}, available_{true};
    Element* parent_{};
    std::unique_ptr<ElementAccessibility> accessibility_;
    std::wstring tooltip_;
    ElementTree* tree_{};
    std::size_t siblingIndex_{};
    std::vector<std::shared_ptr<Element>> children_;
    std::vector<Element*> order_;
    ElementLayout layout_;
    layout::Rect bounds_{};
    layout::Size preferred_{}, desired_{}, constraint_{};
    layout::Point offset_{};
    float grow_{}, basis_{}, basisCross_{}, scrollOffset_{}, scrollLimit_{};
    int z_{};
    bool measureDirty_{true}, arrangeDirty_{true}, orderDirty_{true}, orderedBands_{}, focusable_{}, enabled_{true}, detaching_{};
    bool hostsWindow_{}, hostsVisual_{};
};

// Independent of HWND and graphics device lifetime. requestUpdate should enqueue work on the
// owning thread. It runs once until take_dirty_bounds() drains the accumulated surface damage.
class ElementTree {
public:
    explicit ElementTree(std::shared_ptr<Element> root, std::function<void()> requestUpdate = {});
    ~ElementTree();
    ElementTree(const ElementTree&) = delete;
    ElementTree& operator=(const ElementTree&) = delete;
    [[nodiscard]] Element& root() const noexcept { return *root_; }

    void set_viewport(layout::Size);
    void update_layout();
    [[nodiscard]] std::optional<layout::Rect> take_dirty_bounds();
    // Reconstructs the entire update_bounds(), including transparent gaps and overlapping layers.
    void paint(ScopedSurfaceDraw&);
    [[nodiscard]] Element* hit_test(layout::Point);
    bool pointer(PointerEvent);                // Input position in viewport DIPs.
    void pointer_leave();
    bool key(const KeyEvent&);
    bool text_input(std::wstring_view);
    // Offers a context menu request to the element at a viewport position, or without one to the
    // focused element, then to their ancestors until one handles it.
    bool context_menu(std::optional<layout::Point> position);
    bool focus(Element*);                      // nullptr clears focus.
    [[nodiscard]] Element* focused() const noexcept { return focused_; }
    [[nodiscard]] Element* next_focus(bool backwards = false, bool wrap = true);
    bool move_focus(bool backwards = false, bool wrap = true);

private:
    friend class Element;
    friend class ElementWindow;
    friend class HostedWindow;
    friend class HostedVisual;
    friend struct ElementProvider;
    void release_window() noexcept;
    Element* next_focus_from(Element*, bool backwards, bool wrap);
    // Moves each hosted window and visual to its element's place in the window, after layout.
    void place_windows();
    std::shared_ptr<Element> reference(Element*) const noexcept;
    struct Recipient { std::shared_ptr<Element> element; layout::Point origin; };
    template<class Handler> bool bubble(Element&, Handler&&);
    void schedule();
    void update_hover(Element*);
    void release_capture() noexcept;
    void damage(layout::Rect);
    std::shared_ptr<Element> root_;
    std::function<void()> requestUpdate_;
    std::vector<Recipient> route_;
    Element* focused_{};
    Element* hovered_{};
    Element* captured_{};
    bool updatingLayout_{};
    std::vector<std::shared_ptr<Element>> layoutCallbacks_;
    std::vector<HostedWindow*> hosted_;
    std::vector<HostedVisual*> visuals_;
    ElementWindow* window_{};
    layout::Size viewport_{};
    std::optional<layout::Rect> dirty_;
    bool scheduled_{}, traversing_{};
};

}
