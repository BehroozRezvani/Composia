#pragma once

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
};

struct PointerEvent {
    enum class Kind { down, up, move, wheel };
    enum class Button { none, left, middle, right, x1, x2 };
    Kind kind{};
    layout::Point position{};                 // Local to the current recipient.
    Button button{};
    float wheelDelta{};                       // Detents, positive towards the start.
    layout::Axis wheelAxis{layout::Axis::vertical};
    unsigned modifiers{};                     // Win32 mouse key state (MK_SHIFT, MK_LBUTTON, ...).
    bool doubleClick{};
};

struct KeyEvent {
    unsigned key{};                           // Windows virtual-key code.
    bool down{}, shift{}, control{}, alt{};
    std::intptr_t keyData{};                   // Win32 repeat count, scan code and transition flags.
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
    // The deepest hit or focused element is called first. Return true to stop bubbling.
    virtual bool on_pointer(const PointerEvent&) { return false; }
    virtual bool on_key(const KeyEvent&) { return false; }
    virtual bool on_text_input(std::wstring_view) { return false; }

private:
    friend class ElementTree;
    friend struct ElementAutomation;
    friend struct ElementProvider;
    void attach(ElementTree*) noexcept;
    void check_mutation() const;
    bool accessibility_key(const KeyEvent&);
    void enabled_changed();
    void mark_layout(bool measure);
    void arrange(layout::Rect);
    layout::Size measure(layout::Size);
    layout::Size layout_children(layout::Size, bool arranging);
    std::span<Element* const> visible_children(layout::Rect local, bool point = false);
    void draw(ScopedSurfaceDraw&, layout::Point origin, layout::Rect clip);
    Element* hit(layout::Point local);
    bool reveal(bool scroll);
    Element* last_descendant() noexcept;
    Element* adjacent(bool backwards) noexcept;

    bool observeLayout_{}, hover_{}, available_{true};
    Element* parent_{};
    std::unique_ptr<ElementAccessibility> accessibility_;
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
    bool focus(Element*);                      // nullptr clears focus.
    [[nodiscard]] Element* focused() const noexcept { return focused_; }
    [[nodiscard]] Element* next_focus(bool backwards = false, bool wrap = true);
    bool move_focus(bool backwards = false, bool wrap = true);

private:
    friend class Element;
    friend class ElementWindow;
    void release_window() noexcept;
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
    ElementWindow* window_{};
    layout::Size viewport_{};
    std::optional<layout::Rect> dirty_;
    bool scheduled_{}, traversing_{};
};

}
