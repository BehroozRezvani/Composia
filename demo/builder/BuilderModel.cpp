#include "BuilderModel.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace builder {
namespace {
struct KindInfo {
    Kind kind;
    std::wstring_view name, token, text;
    Size size;
};
constexpr KindInfo kindTable[]{
    {Kind::panel, L"Panel", L"panel", L"Panel", {240, 160}},
    {Kind::label, L"Label", L"label", L"Label", {120, 20}},
    {Kind::button, L"Button", L"button", L"Button", {100, 36}},
    {Kind::text_field, L"Text field", L"textfield", L"Enter text", {200, 36}},
    {Kind::checkbox, L"Checkbox", L"checkbox", L"Option", {140, 24}},
    {Kind::slider, L"Slider", L"slider", L"", {200, 24}},
    {Kind::image, L"Image", L"image", L"Image", {120, 90}},
};
constexpr std::wstring_view edgeNames[]{L"left", L"top", L"right", L"bottom"};

const KindInfo& info(Kind kind) noexcept {
    for (const auto& entry : kindTable) { if (entry.kind == kind) { return entry; } }
    return kindTable[0];
}

int round_dip(float value) noexcept {
    const auto clamped = std::clamp(value, -100000.0f, 100000.0f);
    return static_cast<int>(std::lround(clamped));
}

std::wstring quote(std::wstring_view value) {
    std::wstring out{L"\""};
    for (const wchar_t c : value) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'"': out += L"\\\""; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out.push_back(c);
        }
    }
    out.push_back(L'"');
    return out;
}

// Splits one line into whitespace-separated tokens; quoted tokens keep spaces and escapes.
std::optional<std::vector<std::wstring>> tokenize(std::wstring_view line) {
    std::vector<std::wstring> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] == L' ' || line[i] == L'\t' || line[i] == L'\r') { ++i; continue; }
        std::wstring token;
        if (line[i] == L'"') {
            ++i;
            bool closed{};
            while (i < line.size()) {
                const wchar_t c = line[i++];
                if (c == L'"') { closed = true; break; }
                if (c == L'\\') {
                    if (i >= line.size()) { return std::nullopt; }
                    const wchar_t escaped = line[i++];
                    switch (escaped) {
                    case L'n': token.push_back(L'\n'); break;
                    case L'r': token.push_back(L'\r'); break;
                    case L't': token.push_back(L'\t'); break;
                    case L'\\': case L'"': token.push_back(escaped); break;
                    default: return std::nullopt;
                    }
                } else { token.push_back(c); }
            }
            if (!closed) { return std::nullopt; }
        } else {
            while (i < line.size() && line[i] != L' ' && line[i] != L'\t' && line[i] != L'\r') { token.push_back(line[i++]); }
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}
}

std::wstring_view kind_name(Kind kind) noexcept { return info(kind).name; }
std::wstring_view kind_token(Kind kind) noexcept { return info(kind).token; }
Size default_size(Kind kind) noexcept { return info(kind).size; }
std::wstring_view default_text(Kind kind) noexcept { return info(kind).text; }

std::optional<Kind> kind_from_token(std::wstring_view token) noexcept {
    for (const auto& entry : kindTable) { if (entry.token == token) { return entry.kind; } }
    return std::nullopt;
}

std::wstring_view edge_name(Edge edge) noexcept { return edgeNames[static_cast<std::size_t>(edge)]; }

std::optional<Edge> edge_from_name(std::wstring_view name) noexcept {
    for (const auto edge : edges) { if (edge_name(edge) == name) { return edge; } }
    return std::nullopt;
}

float edge_position(const composia::layout::Rect& rect, Edge edge) noexcept {
    switch (edge) {
    case Edge::left: return rect.x;
    case Edge::top: return rect.y;
    case Edge::right: return rect.x + rect.width;
    case Edge::bottom: return rect.y + rect.height;
    }
    return rect.x;
}

std::optional<int> parse_int(std::wstring_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) { ++i; }
    bool negative{};
    if (i < text.size() && (text[i] == L'-' || text[i] == L'+')) { negative = text[i] == L'-'; ++i; }
    if (i >= text.size()) { return std::nullopt; }
    long long value{};
    std::size_t digits{};
    for (; i < text.size() && text[i] >= L'0' && text[i] <= L'9'; ++i, ++digits) {
        value = value * 10 + (text[i] - L'0');
        if (value > std::numeric_limits<int>::max()) { return std::nullopt; }
    }
    while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) { ++i; }
    if (digits == 0 || i != text.size()) { return std::nullopt; }
    return static_cast<int>(negative ? -value : value);
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) { return {}; }
    const auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(length, 0)), '\0');
    if (length > 0) { WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr); }
    return out;
}

std::wstring widen(std::string_view text) {
    if (text.empty()) { return {}; }
    const auto length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    if (length > 0) { MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length); }
    return out;
}

void Document::resize(int width, int height) noexcept {
    width_ = std::clamp(width, minimum_form, maximum_form);
    height_ = std::clamp(height, minimum_form, maximum_form);
}

const Widget* Document::find(unsigned id) const noexcept {
    const auto it = std::ranges::find(widgets_, id, &Widget::id);
    return it == widgets_.end() ? nullptr : &*it;
}

Widget* Document::find(unsigned id) noexcept { return const_cast<Widget*>(std::as_const(*this).find(id)); }

std::vector<unsigned> Document::children(unsigned parent) const {
    std::vector<unsigned> ids;
    for (const auto& widget : widgets_) { if (widget.parent == parent) { ids.push_back(widget.id); } }
    return ids;
}

std::vector<unsigned> Document::siblings(unsigned id) const {
    const auto widget = find(id);
    if (!widget) { return {}; }
    auto ids = children(widget->parent);
    std::erase(ids, id);
    return ids;
}

bool Document::is_ancestor(unsigned ancestor, unsigned id) const noexcept {
    for (auto current = find(id); current && current->parent != 0; current = find(current->parent)) {
        if (current->parent == ancestor) { return true; }
    }
    return false;
}

unsigned Document::depth(unsigned id) const noexcept {
    unsigned depth{};
    for (auto current = find(id); current && current->parent != 0; current = find(current->parent)) { ++depth; }
    return depth;
}

std::wstring Document::unique_name(Kind kind) const {
    const auto token = kind_token(kind);
    for (unsigned n = 1;; ++n) {
        auto candidate = std::wstring{token} + std::to_wstring(n);
        if (std::ranges::none_of(widgets_, [&](const Widget& w) { return w.name == candidate; })) { return candidate; }
    }
}

unsigned Document::add(Kind kind, unsigned parent, int x, int y) {
    if (parent != 0) {
        const auto container = find(parent);
        if (!container || !container->container()) { return 0; }
    }
    Widget widget;
    widget.id = nextId_++;
    widget.kind = kind;
    widget.parent = parent;
    widget.name = unique_name(kind);
    widget.text = default_text(kind);
    const auto size = default_size(kind);
    widget.x = x;
    widget.y = y;
    widget.width = size.width;
    widget.height = size.height;
    widgets_.push_back(std::move(widget));
    return widgets_.back().id;
}

void Document::collect(unsigned id, std::vector<unsigned>& out) const {
    out.push_back(id);
    for (const auto child : children(id)) { collect(child, out); }
}

bool Document::remove(unsigned id) {
    if (!find(id)) { return false; }
    std::vector<unsigned> doomed;
    collect(id, doomed);
    std::erase_if(widgets_, [&](const Widget& w) { return std::ranges::find(doomed, w.id) != doomed.end(); });
    for (auto& widget : widgets_) {
        for (auto& anchor : widget.anchors) {
            if (anchor.target == Target::widget && !find(anchor.id)) { anchor = {}; }
        }
    }
    return true;
}

unsigned Document::duplicate(unsigned id) {
    const auto original = find(id);
    if (!original) { return 0; }
    std::vector<unsigned> subtree;
    collect(id, subtree);
    std::vector<std::pair<unsigned, unsigned>> remap;
    std::vector<Widget> copies;
    for (const auto member : subtree) {
        Widget copy = *find(member);
        remap.emplace_back(member, nextId_);
        copy.id = nextId_++;
        copies.push_back(std::move(copy));
    }
    const auto mapped = [&](unsigned old) {
        const auto it = std::ranges::find(remap, old, &std::pair<unsigned, unsigned>::first);
        return it == remap.end() ? 0u : it->second;
    };
    for (auto& copy : copies) {
        if (const auto parent = mapped(copy.parent)) { copy.parent = parent; }
        for (auto& anchor : copy.anchors) {
            if (anchor.target == Target::widget) {
                const auto target = mapped(anchor.id);
                if (target) { anchor.id = target; } else { anchor = {}; }
            }
        }
    }
    copies.front().anchors = {};
    copies.front().x += 16;
    copies.front().y += 16;
    for (auto& copy : copies) {
        widgets_.push_back(std::move(copy));
        widgets_.back().name = unique_name(widgets_.back().kind);
    }
    return copies.empty() ? 0 : remap.front().second;
}

bool Document::reparent(unsigned id, unsigned parent) {
    const auto widget = find(id);
    if (!widget || parent == id || is_ancestor(id, parent)) { return false; }
    if (parent != 0) {
        const auto container = find(parent);
        if (!container || !container->container()) { return false; }
    }
    const auto placements = resolve();
    const auto placed = std::ranges::find(placements, id, &Placement::id);
    if (placed == placements.end()) { return false; }
    const auto bounds = placed->bounds;
    widget->parent = parent;
    for (auto& anchor : widget->anchors) { if (anchor.target == Target::widget) { anchor = {}; } }
    for (auto& other : widgets_) {
        if (other.id == id) { continue; }
        for (auto& anchor : other.anchors) { if (anchor.target == Target::widget && anchor.id == id) { anchor = {}; } }
    }
    // Moving the subtree to the end draws it above its new siblings.
    std::vector<unsigned> subtree;
    collect(id, subtree);
    std::stable_partition(widgets_.begin(), widgets_.end(), [&](const Widget& w) { return std::ranges::find(subtree, w.id) == subtree.end(); });
    return fit(id, bounds);
}

bool Document::raise(unsigned id) {
    const auto widget = find(id);
    if (!widget) { return false; }
    const auto order = children(widget->parent);
    const auto position = std::ranges::find(order, id);
    if (position + 1 == order.end()) { return false; }
    std::iter_swap(std::ranges::find(widgets_, id, &Widget::id), std::ranges::find(widgets_, *(position + 1), &Widget::id));
    return true;
}

bool Document::lower(unsigned id) {
    const auto widget = find(id);
    if (!widget) { return false; }
    const auto order = children(widget->parent);
    const auto position = std::ranges::find(order, id);
    if (position == order.begin()) { return false; }
    std::iter_swap(std::ranges::find(widgets_, id, &Widget::id), std::ranges::find(widgets_, *(position - 1), &Widget::id));
    return true;
}

bool Document::valid_anchor(unsigned id, Edge edge, const Anchor& anchor) const noexcept {
    const auto widget = find(id);
    if (!widget) { return false; }
    if (anchor.target != Target::widget) { return true; }
    const auto target = find(anchor.id);
    return target && anchor.id != id && target->parent == widget->parent && horizontal(anchor.edge) == horizontal(edge);
}

bool Document::set_anchor(unsigned id, Edge edge, Anchor anchor) {
    if (!valid_anchor(id, edge, anchor)) { return false; }
    if (anchor.target == Target::none) { anchor = {}; }
    if (anchor.target == Target::parent) { anchor.id = 0; anchor.edge = edge; }
    find(id)->anchor(edge) = anchor;
    return true;
}

bool Document::release(unsigned id, Edge edge) {
    const auto widget = find(id);
    if (!widget) { return false; }
    widget->anchor(edge) = {};
    return true;
}

bool Document::fit(unsigned id, composia::layout::Rect desired) {
    const auto widget = find(id);
    if (!widget) { return false; }
    const auto placements = resolve();
    const auto rect_of = [&](unsigned other) -> std::optional<composia::layout::Rect> {
        const auto it = std::ranges::find(placements, other, &Placement::id);
        if (it == placements.end()) { return std::nullopt; }
        return it->bounds;
    };
    const composia::layout::Rect form{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
    const auto parent = widget->parent ? rect_of(widget->parent).value_or(form) : form;
    desired.width = std::max(static_cast<float>(minimum_size), desired.width);
    desired.height = std::max(static_cast<float>(minimum_size), desired.height);
    widget->x = round_dip(desired.x - parent.x);
    widget->y = round_dip(desired.y - parent.y);
    widget->width = round_dip(desired.width);
    widget->height = round_dip(desired.height);
    for (const auto edge : edges) {
        auto& anchor = widget->anchor(edge);
        if (anchor.target == Target::none) { continue; }
        std::optional<float> target;
        if (anchor.target == Target::parent) { target = edge_position(parent, edge); }
        else if (const auto rect = rect_of(anchor.id)) { target = edge_position(*rect, anchor.edge); }
        if (!target) { continue; }
        const auto mine = edge_position(desired, edge);
        anchor.offset = leading(edge) ? round_dip(mine - *target) : round_dip(*target - mine);
    }
    return true;
}

std::vector<Placement> Document::resolve(float width, float height) const {
    std::vector<Placement> result;
    result.reserve(widgets_.size());
    solve(0, {0, 0, std::max(1.0f, width), std::max(1.0f, height)}, result);
    return result;
}

void Document::solve(unsigned parent, composia::layout::Rect content, std::vector<Placement>& out) const {
    struct Node {
        const Widget* widget;
        composia::layout::Rect bounds;
        int state;  // 0 unvisited, 1 visiting, 2 resolved.
        bool cyclic;
    };
    std::vector<Node> nodes;
    for (const auto id : children(parent)) { nodes.push_back({find(id), {}, 0, false}); }
    const auto node_of = [&](unsigned id) -> Node* {
        for (auto& node : nodes) { if (node.widget->id == id) { return &node; } }
        return nullptr;
    };
    const float minimum = static_cast<float>(minimum_size);
    std::function<void(Node&)> visit = [&](Node& node) {
        if (node.state != 0) { return; }
        node.state = 1;
        const auto& widget = *node.widget;
        // Returns the target edge position with the margin applied, or nothing when the edge is free.
        const auto attached = [&](Edge edge) -> std::optional<float> {
            const auto& anchor = widget.anchor(edge);
            float base{};
            if (anchor.target == Target::parent) { base = edge_position(content, edge); }
            else if (anchor.target == Target::widget) {
                const auto target = node_of(anchor.id);
                if (!target || target == &node || horizontal(anchor.edge) != horizontal(edge)) { return std::nullopt; }
                visit(*target);
                if (target->state != 2) { node.cyclic = true; return std::nullopt; }
                base = edge_position(target->bounds, anchor.edge);
            } else { return std::nullopt; }
            return leading(edge) ? base + static_cast<float>(anchor.offset) : base - static_cast<float>(anchor.offset);
        };
        const auto axis = [&](Edge lead, Edge trail, float origin, int position, int length, float& start, float& extent) {
            const auto first = attached(lead), second = attached(trail);
            const float size = std::max(minimum, static_cast<float>(length));
            if (first && second) { start = *first; extent = std::max(minimum, *second - *first); }
            else if (first) { start = *first; extent = size; }
            else if (second) { extent = size; start = *second - size; }
            else { start = origin + static_cast<float>(position); extent = size; }
        };
        axis(Edge::left, Edge::right, content.x, widget.x, widget.width, node.bounds.x, node.bounds.width);
        axis(Edge::top, Edge::bottom, content.y, widget.y, widget.height, node.bounds.y, node.bounds.height);
        node.state = 2;
    };
    for (auto& node : nodes) { visit(node); }
    for (const auto& node : nodes) {
        out.push_back({node.widget->id, node.bounds, node.cyclic});
        if (node.widget->container()) { solve(node.widget->id, node.bounds, out); }
    }
}

std::string Document::to_text() const {
    std::wstring out{L"composia-ui 1\n"};
    out += L"form " + std::to_wstring(width_) + L" " + std::to_wstring(height_) + L" " + quote(title_) + L"\n";
    for (const auto& w : widgets_) {
        out += L"widget " + std::to_wstring(w.id) + L" " + std::wstring{kind_token(w.kind)} + L" " + std::to_wstring(w.parent) + L" " +
            std::to_wstring(w.x) + L" " + std::to_wstring(w.y) + L" " + std::to_wstring(w.width) + L" " + std::to_wstring(w.height) + L" " +
            (w.checked ? L"1 " : L"0 ") + std::to_wstring(w.value) + L" " + quote(w.name) + L" " + quote(w.text) + L"\n";
    }
    for (const auto& w : widgets_) {
        for (const auto edge : edges) {
            const auto& anchor = w.anchor(edge);
            if (anchor.target == Target::none) { continue; }
            out += L"anchor " + std::to_wstring(w.id) + L" " + std::wstring{edge_name(edge)} + L" ";
            if (anchor.target == Target::parent) { out += L"parent"; }
            else { out += std::to_wstring(anchor.id) + L" " + std::wstring{edge_name(anchor.edge)}; }
            out += L" " + std::to_wstring(anchor.offset) + L"\n";
        }
    }
    return narrow(out);
}

std::optional<Document> Document::from_text(std::string_view utf8) {
    if (utf8.starts_with("\xEF\xBB\xBF")) { utf8.remove_prefix(3); }
    const auto text = widen(utf8);
    Document document;
    bool header{};
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(L'\n', start);
        const auto line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        start = end == std::wstring::npos ? text.size() + 1 : end + 1;
        const auto tokens = tokenize(line);
        if (!tokens) { return std::nullopt; }
        if (tokens->empty() || tokens->front().starts_with(L'#')) { continue; }
        const auto& t = *tokens;
        if (!header) {
            if (t.size() != 2 || t[0] != L"composia-ui" || t[1] != L"1") { return std::nullopt; }
            header = true;
        } else if (t[0] == L"form") {
            const auto width = t.size() == 4 ? parse_int(t[1]) : std::nullopt;
            const auto height = t.size() == 4 ? parse_int(t[2]) : std::nullopt;
            if (!width || !height) { return std::nullopt; }
            document.resize(*width, *height);
            document.set_title(t[3]);
        } else if (t[0] == L"widget") {
            if (t.size() != 12) { return std::nullopt; }
            const auto id = parse_int(t[1]), parent = parse_int(t[3]), x = parse_int(t[4]), y = parse_int(t[5]), width = parse_int(t[6]),
                height = parse_int(t[7]), checked = parse_int(t[8]), value = parse_int(t[9]);
            const auto kind = kind_from_token(t[2]);
            if (!id || *id <= 0 || !kind || !parent || *parent < 0 || !x || !y || !width || !height || !checked || !value) { return std::nullopt; }
            if (document.find(static_cast<unsigned>(*id))) { return std::nullopt; }
            Widget widget;
            widget.id = static_cast<unsigned>(*id);
            widget.kind = *kind;
            widget.parent = static_cast<unsigned>(*parent);
            widget.x = *x;
            widget.y = *y;
            widget.width = std::max(minimum_size, *width);
            widget.height = std::max(minimum_size, *height);
            widget.checked = *checked != 0;
            widget.value = std::clamp(*value, 0, 100);
            widget.name = t[10];
            widget.text = t[11];
            document.widgets_.push_back(std::move(widget));
            document.nextId_ = std::max(document.nextId_, static_cast<unsigned>(*id) + 1);
        } else if (t[0] == L"anchor") {
            if (t.size() != 5 && t.size() != 6) { return std::nullopt; }
            const auto id = parse_int(t[1]);
            const auto edge = edge_from_name(t[2]);
            const auto offset = parse_int(t.back());
            if (!id || !edge || !offset) { return std::nullopt; }
            Anchor anchor;
            anchor.offset = *offset;
            if (t.size() == 5) {
                if (t[3] != L"parent") { return std::nullopt; }
                anchor.target = Target::parent;
            } else {
                const auto target = parse_int(t[3]);
                const auto targetEdge = edge_from_name(t[4]);
                if (!target || *target <= 0 || !targetEdge) { return std::nullopt; }
                anchor.target = Target::widget;
                anchor.id = static_cast<unsigned>(*target);
                anchor.edge = *targetEdge;
            }
            // Anchors may precede their targets in the file, so they are validated after loading.
            if (const auto widget = document.find(static_cast<unsigned>(*id))) { widget->anchor(*edge) = anchor; }
            else { return std::nullopt; }
        } else { return std::nullopt; }
    }
    if (!header) { return std::nullopt; }
    for (const auto& widget : document.widgets_) {
        if (widget.parent != 0) {
            const auto parent = document.find(widget.parent);
            if (!parent || !parent->container() || document.is_ancestor(widget.id, widget.parent) || widget.parent == widget.id) { return std::nullopt; }
        }
    }
    for (auto& widget : document.widgets_) {
        for (const auto edge : edges) {
            if (!document.valid_anchor(widget.id, edge, widget.anchor(edge))) { widget.anchor(edge) = {}; }
            else if (widget.anchor(edge).target == Target::parent) { widget.anchor(edge).id = 0; widget.anchor(edge).edge = edge; }
        }
    }
    return document;
}

Document Document::sample() {
    Document document;
    document.resize(640, 440);
    document.set_title(L"Sign in");
    const auto place = [&](Kind kind, unsigned parent, std::wstring_view name, std::wstring_view text, int x, int y, int width, int height) {
        const auto id = document.add(kind, parent, x, y);
        auto& widget = *document.find(id);
        widget.name = name;
        widget.text = text;
        widget.width = width;
        widget.height = height;
        return id;
    };
    const auto parent = [&](unsigned id, Edge edge, int offset) {
        document.set_anchor(id, edge, {Target::parent, 0, edge, offset});
    };
    const auto sibling = [&](unsigned id, Edge edge, unsigned target, Edge targetEdge, int offset) {
        document.set_anchor(id, edge, {Target::widget, target, targetEdge, offset});
    };
    const auto panel = place(Kind::panel, 0, L"account", L"Account", 32, 32, 576, 272);
    parent(panel, Edge::left, 32);
    parent(panel, Edge::top, 32);
    parent(panel, Edge::right, 32);
    const auto emailLabel = place(Kind::label, panel, L"emailLabel", L"Email", 24, 40, 120, 20);
    parent(emailLabel, Edge::left, 24);
    parent(emailLabel, Edge::top, 40);
    const auto email = place(Kind::text_field, panel, L"email", L"name@example.com", 24, 66, 528, 36);
    parent(email, Edge::left, 24);
    parent(email, Edge::right, 24);
    parent(email, Edge::top, 66);
    const auto passwordLabel = place(Kind::label, panel, L"passwordLabel", L"Password", 24, 116, 120, 20);
    parent(passwordLabel, Edge::left, 24);
    parent(passwordLabel, Edge::top, 116);
    const auto password = place(Kind::text_field, panel, L"password", L"Password", 24, 142, 528, 36);
    parent(password, Edge::left, 24);
    parent(password, Edge::right, 24);
    parent(password, Edge::top, 142);
    const auto remember = place(Kind::checkbox, panel, L"remember", L"Remember me on this device", 24, 196, 240, 24);
    parent(remember, Edge::left, 24);
    parent(remember, Edge::top, 196);
    document.find(remember)->checked = true;
    const auto session = place(Kind::slider, panel, L"session", L"", 24, 232, 528, 24);
    parent(session, Edge::left, 24);
    parent(session, Edge::right, 24);
    parent(session, Edge::top, 232);
    document.find(session)->value = 35;
    const auto logo = place(Kind::image, 0, L"logo", L"Logo", 32, 336, 72, 72);
    parent(logo, Edge::left, 32);
    parent(logo, Edge::bottom, 32);
    const auto footer = place(Kind::label, 0, L"footer", L"New here? Create an account", 120, 362, 220, 20);
    sibling(footer, Edge::left, logo, Edge::right, 16);
    parent(footer, Edge::bottom, 58);
    const auto signIn = place(Kind::button, 0, L"signIn", L"Sign in", 488, 372, 120, 36);
    parent(signIn, Edge::right, 32);
    parent(signIn, Edge::bottom, 32);
    const auto cancel = place(Kind::button, 0, L"cancel", L"Cancel", 380, 372, 100, 36);
    sibling(cancel, Edge::right, signIn, Edge::left, 8);
    sibling(cancel, Edge::bottom, signIn, Edge::bottom, 0);
    return document;
}

void History::record(std::string_view key) {
    if (!key.empty() && key == key_) { return; }
    past_.push_back(present_);
    if (past_.size() > limit) { past_.erase(past_.begin()); }
    future_.clear();
    key_ = key;
}

bool History::undo() {
    if (past_.empty()) { return false; }
    future_.push_back(std::move(present_));
    present_ = std::move(past_.back());
    past_.pop_back();
    key_.clear();
    return true;
}

bool History::redo() {
    if (future_.empty()) { return false; }
    past_.push_back(std::move(present_));
    present_ = std::move(future_.back());
    future_.pop_back();
    key_.clear();
    return true;
}

void History::reset(Document document) {
    present_ = document;
    saved_ = std::move(document);
    past_.clear();
    future_.clear();
    key_.clear();
}

}
