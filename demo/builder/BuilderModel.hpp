#pragma once

#include <composia/Layout.hpp>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The UI builder's document: a tree of widgets whose edges can be anchored to the parent or to a
// sibling. Nothing here depends on windows or graphics; the solver produces DIP rectangles.
namespace builder {

enum class Kind { panel, label, button, text_field, checkbox, slider, image };
constexpr std::array<Kind, 7> kinds{Kind::panel, Kind::label, Kind::button, Kind::text_field, Kind::checkbox, Kind::slider, Kind::image};
struct Size { int width{}, height{}; };
[[nodiscard]] std::wstring_view kind_name(Kind) noexcept;   // "Text field"
[[nodiscard]] std::wstring_view kind_token(Kind) noexcept;  // "textfield": files and generated names
[[nodiscard]] std::optional<Kind> kind_from_token(std::wstring_view) noexcept;
[[nodiscard]] Size default_size(Kind) noexcept;
[[nodiscard]] std::wstring_view default_text(Kind) noexcept;

enum class Edge { left, top, right, bottom };
constexpr std::array<Edge, 4> edges{Edge::left, Edge::top, Edge::right, Edge::bottom};
[[nodiscard]] std::wstring_view edge_name(Edge) noexcept;
[[nodiscard]] std::optional<Edge> edge_from_name(std::wstring_view) noexcept;
[[nodiscard]] constexpr bool horizontal(Edge edge) noexcept { return edge == Edge::left || edge == Edge::right; }
[[nodiscard]] constexpr bool leading(Edge edge) noexcept { return edge == Edge::left || edge == Edge::top; }
[[nodiscard]] constexpr Edge opposite(Edge edge) noexcept {
    switch (edge) {
    case Edge::left: return Edge::right;
    case Edge::top: return Edge::bottom;
    case Edge::right: return Edge::left;
    case Edge::bottom: return Edge::top;
    }
    return Edge::left;
}
[[nodiscard]] float edge_position(const composia::layout::Rect&, Edge) noexcept;

enum class Target { none, parent, widget };

// An edge's attachment. Offsets are margins measured inward: a left or top edge sits at the target
// edge plus the offset; a right or bottom edge sits at the target edge minus the offset.
struct Anchor {
    Target target{Target::none};
    unsigned id{};   // The sibling when target is a widget.
    Edge edge{};     // The sibling's edge; parent anchors use the widget's own edge.
    int offset{};
    bool operator==(const Anchor&) const = default;
};

struct Widget {
    unsigned id{};
    Kind kind{Kind::button};
    unsigned parent{};  // 0 is the form.
    std::wstring name, text;
    int x{}, y{}, width{}, height{};  // Used by edges without anchors; x and y are relative to the parent.
    std::array<Anchor, 4> anchors{};  // Indexed by Edge.
    bool checked{};
    int value{50};  // Slider position, 0 to 100.
    bool operator==(const Widget&) const = default;
    [[nodiscard]] const Anchor& anchor(Edge edge) const noexcept { return anchors[static_cast<std::size_t>(edge)]; }
    [[nodiscard]] Anchor& anchor(Edge edge) noexcept { return anchors[static_cast<std::size_t>(edge)]; }
    [[nodiscard]] bool container() const noexcept { return kind == Kind::panel; }
};

struct Placement {
    unsigned id{};
    composia::layout::Rect bounds;  // In form coordinates.
    bool cyclic{};                  // An anchor took part in a cycle and was ignored.
};

class Document {
public:
    static constexpr int minimum_size = 12, minimum_form = 160, maximum_form = 4096;
    [[nodiscard]] static Document sample();

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] const std::wstring& title() const noexcept { return title_; }
    void resize(int width, int height) noexcept;
    void set_title(std::wstring title) { title_ = std::move(title); }
    [[nodiscard]] const std::vector<Widget>& widgets() const noexcept { return widgets_; }
    [[nodiscard]] std::size_t size() const noexcept { return widgets_.size(); }
    [[nodiscard]] const Widget* find(unsigned id) const noexcept;
    [[nodiscard]] Widget* find(unsigned id) noexcept;
    [[nodiscard]] std::vector<unsigned> children(unsigned parent) const;  // Back to front.
    [[nodiscard]] std::vector<unsigned> siblings(unsigned id) const;      // Excludes the widget.
    [[nodiscard]] bool is_ancestor(unsigned ancestor, unsigned id) const noexcept;
    [[nodiscard]] unsigned depth(unsigned id) const noexcept;
    [[nodiscard]] std::wstring unique_name(Kind) const;

    // Returns the new id, or 0 when the parent is not a panel.
    unsigned add(Kind, unsigned parent, int x, int y);
    bool remove(unsigned id);        // Removes descendants and releases anchors to the widget.
    unsigned duplicate(unsigned id);  // Copies the subtree beside the original.
    bool reparent(unsigned id, unsigned parent);  // Keeps the placement; releases sibling anchors.
    bool raise(unsigned id);
    bool lower(unsigned id);
    [[nodiscard]] bool valid_anchor(unsigned id, Edge, const Anchor&) const noexcept;
    bool set_anchor(unsigned id, Edge, Anchor);
    bool release(unsigned id, Edge);
    // Changes position, size, and margins so the widget resolves to the rectangle (form coordinates).
    bool fit(unsigned id, composia::layout::Rect desired);

    [[nodiscard]] std::vector<Placement> resolve(float width, float height) const;  // Draw order.
    [[nodiscard]] std::vector<Placement> resolve() const { return resolve(static_cast<float>(width_), static_cast<float>(height_)); }

    [[nodiscard]] std::string to_text() const;  // UTF-8.
    [[nodiscard]] static std::optional<Document> from_text(std::string_view utf8);
    bool operator==(const Document&) const = default;

private:
    void solve(unsigned parent, composia::layout::Rect content, std::vector<Placement>& out) const;
    void collect(unsigned id, std::vector<unsigned>& out) const;

    std::vector<Widget> widgets_;
    unsigned nextId_{1};
    int width_{640}, height_{440};
    std::wstring title_{L"Untitled"};
};

// Snapshot history. Consecutive edits recorded with the same key share one undo step.
class History {
public:
    explicit History(Document initial) : present_(initial), saved_(std::move(initial)) {}
    [[nodiscard]] Document& document() noexcept { return present_; }
    [[nodiscard]] const Document& document() const noexcept { return present_; }
    void record(std::string_view key = {});
    void seal() noexcept { key_.clear(); }
    bool undo();
    bool redo();
    [[nodiscard]] bool can_undo() const noexcept { return !past_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !future_.empty(); }
    void reset(Document);
    void mark_saved() { saved_ = present_; }
    [[nodiscard]] bool dirty() const { return !(present_ == saved_); }

private:
    static constexpr std::size_t limit = 200;
    Document present_, saved_;
    std::vector<Document> past_, future_;
    std::string key_;
};

[[nodiscard]] std::optional<int> parse_int(std::wstring_view) noexcept;
[[nodiscard]] std::string narrow(std::wstring_view);
[[nodiscard]] std::wstring widen(std::string_view);

}
