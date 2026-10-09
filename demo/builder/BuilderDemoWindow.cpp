#include "BuilderDemoWindow.hpp"
#include "AppPackager.hpp"
#include "FormPlayerWindow.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>

using namespace composia;
using builder::Anchor;
using builder::Edge;
using builder::FormPainter;
using builder::Kind;
using builder::Target;
using builder::inset;
using builder::intersect;
using Style = builder::TextStyle;

namespace {
namespace palette = builder::palette;
constexpr UINT32 chrome = palette::chrome, pane = palette::pane, workspaceBg = palette::workspace, formBg = palette::form, divider = palette::divider,
    ink = palette::ink, inkSoft = palette::inkSoft, inkMuted = palette::inkMuted, inkFaint = palette::inkFaint,
    accent = palette::accent, danger = palette::danger, selectedBg = palette::selected, hoverBg = palette::hover, fieldBg = palette::field,
    chipBg = palette::chip, gridDot = palette::gridDot, buttonInk = palette::buttonInk, hoverOutline = palette::hoverOutline, shadow = palette::shadow;
constexpr float topBar = 56, statusBar = 28, paletteWidth = 224, inspectorWidth = 304, paletteRow = 36, outlineRow = 26, outlineTop = 364,
    handleSize = 8, pinRadius = 5, pinGap = 14, gridStep = 8, snapDistance = 6, dragThreshold = 3, fieldHeight = 32;
constexpr wchar_t fileFilter[] = L"Composia UI designs (*.cui)\0*.cui\0All files\0*.*\0";
constexpr wchar_t appFilter[] = L"Applications (*.exe)\0*.exe\0";

float snap_grid(float value) noexcept { return std::round(value / gridStep) * gridStep; }

std::wstring capitalized(std::wstring_view value) {
    std::wstring result{value};
    if (!result.empty() && result[0] >= L'a' && result[0] <= L'z') { result[0] = static_cast<wchar_t>(result[0] - L'a' + L'A'); }
    return result;
}

std::wstring file_name(const std::wstring& path) {
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

bool control_down() noexcept { return GetKeyState(VK_CONTROL) < 0; }
bool shift_down() noexcept { return GetKeyState(VK_SHIFT) < 0; }
}

BuilderDemoWindow::BuilderDemoWindow(Application& app)
    : Window(app, L"Composia | UI builder", 1380, 880), app_(app), target_(app.compositor(), app.graphics(), hwnd()),
      history_(builder::Document::sample()),
      newButton_(*this, L"New"), openButton_(*this, L"Open"), saveButton_(*this, L"Save"), undoButton_(*this, L"Undo"),
      redoButton_(*this, L"Redo"), previewButton_(*this, L"Preview"), stopButton_(*this, L"Stop preview"), buildButton_(*this, L"Build app…"),
      name_(*this, L"Name"), text_(*this, L"Text"), x_(*this, L"X"), y_(*this, L"Y"), width_(*this, L"Width"), height_(*this, L"Height"),
      minWidth_(*this, L"Min width"), minHeight_(*this, L"Min height"), value_(*this, L"Value"), marginLeft_(*this, L"0"), marginTop_(*this, L"0"), marginRight_(*this, L"0"), marginBottom_(*this, L"0"),
      deleteButton_(*this, L"Delete"), duplicateButton_(*this, L"Duplicate"), frontButton_(*this, L"Bring forward"), backButton_(*this, L"Send backward"),
      runButton_(*this, L"Run app") {
    connections_[0] = newButton_.on_click([this] { new_document(); });
    connections_[1] = openButton_.on_click([this] { open(); });
    connections_[2] = saveButton_.on_click([this] { save(); });
    connections_[3] = undoButton_.on_click([this] { undo(); });
    connections_[4] = redoButton_.on_click([this] { redo(); });
    connections_[5] = previewButton_.on_click([this] { start_preview(); });
    connections_[6] = stopButton_.on_click([this] { stop_preview(); });
    connections_[7] = deleteButton_.on_click([this] { remove_selected(); });
    connections_[8] = duplicateButton_.on_click([this] { duplicate_selected(); });
    connections_[9] = frontButton_.on_click([this] { bring_forward(); });
    connections_[10] = backButton_.on_click([this] { send_backward(); });
    connections_[11] = name_.on_change([this] {
        if (syncing_) { return; }
        const auto widget = selected_ ? document().find(*selected_) : nullptr;
        if (!widget || widget->name == name_.text()) { return; }
        changed("name:" + std::to_string(widget->id));
        widget->name = name_.text();
        invalidate();
    });
    connections_[12] = text_.on_change([this] {
        if (syncing_) { return; }
        if (const auto widget = selected_ ? document().find(*selected_) : nullptr) {
            if (widget->text == text_.text()) { return; }
            changed("text:" + std::to_string(widget->id));
            widget->text = text_.text();
        } else {
            if (document().title() == text_.text()) { return; }
            changed("title");
            document().set_title(text_.text());
        }
        invalidate();
    });
    connections_[13] = x_.on_change([this] { apply_position(false, x_); });
    connections_[14] = y_.on_change([this] { apply_position(true, y_); });
    connections_[15] = width_.on_change([this] { apply_size(false, width_); });
    connections_[16] = height_.on_change([this] { apply_size(true, height_); });
    connections_[17] = value_.on_change([this] {
        if (syncing_) { return; }
        const auto widget = selected_ ? document().find(*selected_) : nullptr;
        const auto value = builder::parse_int(value_.text());
        if (!widget || !value) { return; }
        const auto clamped = std::clamp(*value, 0, 100);
        if (widget->value == clamped) { return; }
        changed("value:" + std::to_string(widget->id));
        widget->value = clamped;
        invalidate();
    });
    for (std::size_t index = 0; index != builder::edges.size(); ++index) {
        const auto edge = builder::edges[index];
        connections_[18 + index] = margin_field(edge).on_change([this, edge] {
            if (syncing_) { return; }
            if (const auto value = builder::parse_int(margin_field(edge).text())) { set_margin(edge, *value); }
        });
        connections_[22 + index] = margin_field(edge).on_submit([this] { sync_inspector(); });
    }
    connections_[26] = x_.on_submit([this] { sync_inspector(); });
    connections_[27] = y_.on_submit([this] { sync_inspector(); });
    connections_[28] = width_.on_submit([this] { sync_inspector(); });
    connections_[29] = height_.on_submit([this] { sync_inspector(); });
    connections_[30] = value_.on_submit([this] { sync_inspector(); });
    connections_[31] = buildButton_.on_click([this] { build_app(); });
    connections_[32] = runButton_.on_click([this] { run_built(); });
    connections_[33] = minWidth_.on_change([this] { apply_minimum(false, minWidth_); });
    connections_[34] = minHeight_.on_change([this] { apply_minimum(true, minHeight_); });
    connections_[35] = minWidth_.on_submit([this] { sync_inspector(); });
    connections_[36] = minHeight_.on_submit([this] { sync_inspector(); });
    stopButton_.show(SW_HIDE);
    runButton_.show(SW_HIDE);
    sync_inspector();
    update_controls();
    invalidate();
}

BuilderDemoWindow::~BuilderDemoWindow() = default;

TextField& BuilderDemoWindow::margin_field(Edge edge) noexcept {
    switch (edge) {
    case Edge::left: return marginLeft_;
    case Edge::top: return marginTop_;
    case Edge::right: return marginRight_;
    case Edge::bottom: return marginBottom_;
    }
    return marginLeft_;
}

const builder::Document* BuilderDemoWindow::preview_document() const noexcept { return preview_ ? &preview_->document() : nullptr; }
const builder::Document& BuilderDemoWindow::active() const noexcept { return preview_ ? preview_->document() : document(); }
Button* BuilderDemoWindow::preview_control(unsigned id) noexcept { return preview_ ? preview_->button(id) : nullptr; }
TextField* BuilderDemoWindow::preview_field(unsigned id) noexcept { return preview_ ? preview_->field(id) : nullptr; }

std::optional<layout::Rect> BuilderDemoWindow::region(Hit kind, unsigned id) const noexcept {
    for (const auto& region : regions_) {
        if (region.kind == kind && region.id == id) { return region.bounds; }
    }
    return std::nullopt;
}

const builder::Placement* BuilderDemoWindow::placement(unsigned id) const noexcept {
    const auto it = std::ranges::find(placements_, id, &builder::Placement::id);
    return it == placements_.end() ? nullptr : &*it;
}

std::optional<layout::Rect> BuilderDemoWindow::widget_bounds(unsigned id) const noexcept {
    const auto placed = placement(id);
    if (!placed) { return std::nullopt; }
    return to_window(placed->bounds);
}

layout::Rect BuilderDemoWindow::handle_bounds(unsigned corner) const noexcept {
    const auto placed = selected_ ? placement(*selected_) : nullptr;
    if (!placed) { return {}; }
    const auto r = to_window(placed->bounds);
    const float cx = corner == 0 || corner == 3 ? r.x : r.x + r.width;
    const float cy = corner < 2 ? r.y : r.y + r.height;
    return {cx - handleSize / 2, cy - handleSize / 2, handleSize, handleSize};
}

layout::Rect BuilderDemoWindow::pin_bounds(Edge edge) const noexcept {
    const auto placed = selected_ ? placement(*selected_) : nullptr;
    if (!placed) { return {}; }
    const auto r = to_window(placed->bounds);
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    switch (edge) {
    case Edge::left: cx = r.x - pinGap; break;
    case Edge::top: cy = r.y - pinGap; break;
    case Edge::right: cx = r.x + r.width + pinGap; break;
    case Edge::bottom: cy = r.y + r.height + pinGap; break;
    }
    return {cx - pinRadius - 4, cy - pinRadius - 4, 2 * (pinRadius + 4), 2 * (pinRadius + 4)};
}

layout::Rect BuilderDemoWindow::to_window(layout::Rect r) const noexcept { return {form_.x + r.x, form_.y + r.y, r.width, r.height}; }
numerics::float2 BuilderDemoWindow::to_form(numerics::float2 p) const noexcept { return {p.x - form_.x, p.y - form_.y}; }

numerics::float2 BuilderDemoWindow::pointer(LPARAM lparam) const noexcept {
    const auto scale = static_cast<float>(dpi() ? dpi() : 96) / 96.0f;
    return {GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale};
}

std::wstring BuilderDemoWindow::anchor_label(const Anchor& anchor) const {
    switch (anchor.target) {
    case Target::none: return L"Free";
    case Target::parent: return L"Parent";
    case Target::widget: {
        const auto target = document().find(anchor.id);
        return (target ? target->name : std::wstring{L"?"}) + L" · " + std::wstring{builder::edge_name(anchor.edge)};
    }
    }
    return L"Free";
}

void BuilderDemoWindow::notify(std::wstring text) {
    status_ = std::move(text);
    builtPath_.clear();
    invalidate();
}

void BuilderDemoWindow::changed(std::string_view key) {
    history_.record(key);
    update_controls();
}

void BuilderDemoWindow::update_controls() {
    const bool preview = previewing();
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    const auto visible = [](Window& window, bool shown) {
        if ((IsWindowVisible(window.hwnd()) != FALSE) != shown) { window.show(shown ? SW_SHOWNA : SW_HIDE); }
    };
    visible(previewButton_, !preview);
    visible(stopButton_, preview);
    newButton_.set_enabled(!preview);
    openButton_.set_enabled(!preview);
    saveButton_.set_enabled(!preview);
    buildButton_.set_enabled(!preview);
    undoButton_.set_enabled(!preview && history_.can_undo());
    redoButton_.set_enabled(!preview && history_.can_redo());
    const bool actions = widget && !preview;
    deleteButton_.set_enabled(actions);
    duplicateButton_.set_enabled(actions);
    frontButton_.set_enabled(actions);
    backButton_.set_enabled(actions);
}

void BuilderDemoWindow::sync_inspector() {
    syncing_ = true;
    const auto done = wil::scope_exit([&] { syncing_ = false; });
    const auto assign = [](TextField& field, std::wstring value) { if (field.text() != value) { field.set_text(std::move(value)); } };
    const auto number = [](float value) { return std::to_wstring(static_cast<int>(std::lround(value))); };
    const auto& document = this->document();
    if (const auto widget = selected_ ? document.find(*selected_) : nullptr) {
        const auto placements = document.resolve();
        const auto bounds_of = [&](unsigned id) -> layout::Rect {
            const auto it = std::ranges::find(placements, id, &builder::Placement::id);
            return it == placements.end() ? layout::Rect{} : it->bounds;
        };
        const auto bounds = bounds_of(widget->id);
        const auto parent = widget->parent ? bounds_of(widget->parent) : layout::Rect{};
        assign(name_, widget->name);
        assign(text_, widget->text);
        assign(x_, number(bounds.x - parent.x));
        assign(y_, number(bounds.y - parent.y));
        assign(width_, number(bounds.width));
        assign(height_, number(bounds.height));
        assign(value_, std::to_wstring(widget->value));
        for (const auto edge : builder::edges) {
            const auto& anchor = widget->anchor(edge);
            assign(margin_field(edge), anchor.target == Target::none ? std::wstring{} : std::to_wstring(anchor.offset));
        }
    } else {
        assign(text_, document.title());
        assign(width_, std::to_wstring(document.width()));
        assign(height_, std::to_wstring(document.height()));
        assign(minWidth_, std::to_wstring(document.minimum_width()));
        assign(minHeight_, std::to_wstring(document.minimum_height()));
    }
}

void BuilderDemoWindow::select(std::optional<unsigned> id) {
    if (id && !document().find(*id)) { id.reset(); }
    selected_ = id;
    history_.seal();
    sync_inspector();
    update_controls();
    invalidate();
}

unsigned BuilderDemoWindow::add_widget(Kind kind) {
    unsigned parent{};
    if (const auto widget = selected_ ? document().find(*selected_) : nullptr) { parent = widget->container() ? widget->id : widget->parent; }
    const auto count = static_cast<int>(document().children(parent).size());
    const int offset = 24 + 16 * (count % 8);
    return add_widget(kind, parent, offset, offset);
}

unsigned BuilderDemoWindow::add_widget(Kind kind, unsigned parent, int x, int y) {
    if (previewing()) { return 0; }
    changed();
    const auto id = document().add(kind, parent, x, y);
    if (!id) { return 0; }
    select(id);
    notify(L"Added " + document().find(id)->name);
    return id;
}

void BuilderDemoWindow::remove_selected() {
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    if (!widget || previewing()) { return; }
    const auto name = widget->name;
    const auto before = document().size();
    changed();
    document().remove(widget->id);
    const auto removed = before - document().size();
    select(std::nullopt);
    notify(removed > 1 ? L"Deleted " + name + L" and " + std::to_wstring(removed - 1) + L" nested widgets" : L"Deleted " + name);
}

void BuilderDemoWindow::duplicate_selected() {
    if (!selected_ || previewing()) { return; }
    changed();
    const auto id = document().duplicate(*selected_);
    if (!id) { return; }
    select(id);
    notify(L"Duplicated as " + document().find(id)->name);
}

void BuilderDemoWindow::bring_forward() {
    if (!selected_ || previewing()) { return; }
    changed();
    if (!document().raise(*selected_)) { notify(L"Already in front"); return; }
    notify(L"Brought forward");
}

void BuilderDemoWindow::send_backward() {
    if (!selected_ || previewing()) { return; }
    changed();
    if (!document().lower(*selected_)) { notify(L"Already at the back"); return; }
    notify(L"Sent backward");
}

void BuilderDemoWindow::nudge(int dx, int dy) {
    if (!selected_ || previewing()) { return; }
    const auto placements = document().resolve();
    const auto it = std::ranges::find(placements, *selected_, &builder::Placement::id);
    if (it == placements.end()) { return; }
    auto bounds = it->bounds;
    bounds.x += static_cast<float>(dx);
    bounds.y += static_cast<float>(dy);
    changed("nudge:" + std::to_string(*selected_));
    document().fit(*selected_, bounds);
    sync_inspector();
    invalidate();
}

bool BuilderDemoWindow::set_anchor(Edge edge, Anchor anchor) {
    if (!selected_ || previewing()) { return false; }
    auto& document = this->document();
    if (!document.valid_anchor(*selected_, edge, anchor)) { return false; }
    if (anchor.target == Target::none) { release_anchor(edge); return true; }
    const auto placements = document.resolve();
    const auto bounds_of = [&](unsigned id) -> layout::Rect {
        const auto it = std::ranges::find(placements, id, &builder::Placement::id);
        return it == placements.end() ? layout::Rect{} : it->bounds;
    };
    const auto widget = document.find(*selected_);
    const auto bounds = bounds_of(widget->id);
    const auto parent = widget->parent ? bounds_of(widget->parent)
        : layout::Rect{0, 0, static_cast<float>(document.width()), static_cast<float>(document.height())};
    const float target = anchor.target == Target::parent ? builder::edge_position(parent, edge) : builder::edge_position(bounds_of(anchor.id), anchor.edge);
    const float mine = builder::edge_position(bounds, edge);
    anchor.offset = static_cast<int>(std::lround(builder::leading(edge) ? mine - target : target - mine));
    changed();
    document.fit(widget->id, bounds);  // Keeps the free edges consistent before the anchor takes over.
    document.set_anchor(widget->id, edge, anchor);
    sync_inspector();
    notify(L"Anchored " + std::wstring{builder::edge_name(edge)} + L" to " + anchor_label(anchor));
    return true;
}

void BuilderDemoWindow::release_anchor(Edge edge) {
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    if (!widget || previewing() || widget->anchor(edge).target == Target::none) { return; }
    const auto placements = document().resolve();
    const auto it = std::ranges::find(placements, widget->id, &builder::Placement::id);
    changed();
    if (it != placements.end()) { document().fit(widget->id, it->bounds); }  // The widget stays where it is.
    document().release(widget->id, edge);
    sync_inspector();
    notify(L"Released the " + std::wstring{builder::edge_name(edge)} + L" edge");
}

void BuilderDemoWindow::cycle_anchor(Edge edge) {
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    if (!widget || previewing()) { return; }
    std::vector<Anchor> options{{}, {Target::parent, 0, edge, 0}};
    for (const auto sibling : document().siblings(widget->id)) { options.push_back({Target::widget, sibling, builder::opposite(edge), 0}); }
    const auto& current = widget->anchor(edge);
    std::size_t index = 0;
    for (std::size_t i = 0; i != options.size(); ++i) {
        if (options[i].target == current.target && options[i].id == current.id && (current.target != Target::widget || options[i].edge == current.edge)) { index = i; }
    }
    const auto& next = options[(index + 1) % options.size()];
    if (next.target == Target::none) { release_anchor(edge); } else { set_anchor(edge, next); }
}

void BuilderDemoWindow::set_margin(Edge edge, int offset) {
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    if (!widget || previewing()) { return; }
    auto& anchor = widget->anchor(edge);
    if (anchor.target == Target::none || anchor.offset == offset) { return; }
    changed("margin:" + std::to_string(widget->id) + ":" + std::to_string(static_cast<int>(edge)));
    anchor.offset = offset;
    invalidate();
}

void BuilderDemoWindow::resize_form(int width, int height) {
    if (previewing()) { return; }
    auto clamped = document();
    clamped.resize(width, height);
    if (clamped.width() == document().width() && clamped.height() == document().height()) { return; }
    changed("form");
    document().resize(width, height);
    sync_inspector();
    invalidate();
}

void BuilderDemoWindow::set_minimum(int width, int height) {
    if (previewing()) { return; }
    auto clamped = document();
    clamped.set_minimum(width, height);
    if (clamped.minimum_width() == document().minimum_width() && clamped.minimum_height() == document().minimum_height()) { return; }
    changed("minimum");
    document().set_minimum(width, height);
    sync_inspector();
    invalidate();
}

void BuilderDemoWindow::apply_minimum(bool vertical, TextField& field) {
    if (syncing_ || previewing()) { return; }
    const auto value = builder::parse_int(field.text());
    if (!value || *value < builder::Document::minimum_form) { return; }
    set_minimum(vertical ? document().minimum_width() : *value, vertical ? *value : document().minimum_height());
}

void BuilderDemoWindow::apply_position(bool vertical, TextField& field) {
    if (syncing_ || previewing()) { return; }
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    const auto value = builder::parse_int(field.text());
    if (!widget || !value) { return; }
    const auto placements = document().resolve();
    const auto bounds_of = [&](unsigned id) -> layout::Rect {
        const auto it = std::ranges::find(placements, id, &builder::Placement::id);
        return it == placements.end() ? layout::Rect{} : it->bounds;
    };
    auto bounds = bounds_of(widget->id);
    const auto parent = widget->parent ? bounds_of(widget->parent) : layout::Rect{};
    const float target = (vertical ? parent.y : parent.x) + static_cast<float>(*value);
    if (std::lround(vertical ? bounds.y : bounds.x) == std::lround(target)) { return; }
    (vertical ? bounds.y : bounds.x) = target;
    changed((vertical ? "y:" : "x:") + std::to_string(widget->id));
    document().fit(widget->id, bounds);
    invalidate();
}

void BuilderDemoWindow::apply_size(bool vertical, TextField& field) {
    if (syncing_ || previewing()) { return; }
    const auto value = builder::parse_int(field.text());
    if (!value) { return; }
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    if (!widget) {
        if (*value < builder::Document::minimum_form || *value > builder::Document::maximum_form) { return; }
        resize_form(vertical ? document().width() : *value, vertical ? *value : document().height());
        return;
    }
    if (*value < builder::Document::minimum_size) { return; }
    const auto placements = document().resolve();
    const auto it = std::ranges::find(placements, widget->id, &builder::Placement::id);
    if (it == placements.end()) { return; }
    auto bounds = it->bounds;
    if (std::lround(vertical ? bounds.height : bounds.width) == *value) { return; }
    (vertical ? bounds.height : bounds.width) = static_cast<float>(*value);
    changed((vertical ? "height:" : "width:") + std::to_string(widget->id));
    document().fit(widget->id, bounds);
    invalidate();
}

void BuilderDemoWindow::undo() {
    if (previewing()) { return; }
    if (!history_.undo()) { notify(L"Nothing to undo"); return; }
    select(selected_);
    notify(L"Undone");
}

void BuilderDemoWindow::redo() {
    if (previewing()) { return; }
    if (!history_.redo()) { notify(L"Nothing to redo"); return; }
    select(selected_);
    notify(L"Redone");
}

void BuilderDemoWindow::new_document() {
    if (previewing()) { stop_preview(); }
    history_.reset(builder::Document{});
    path_.clear();
    select(std::nullopt);
    notify(L"New form");
}

bool BuilderDemoWindow::save_to(const std::wstring& path) {
    const auto text = document().to_text();
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) { notify(L"Could not write " + path); return false; }
    path_ = path;
    history_.mark_saved();
    notify(L"Saved " + file_name(path));
    return true;
}

bool BuilderDemoWindow::open_from(const std::wstring& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) { notify(L"Could not open " + path); return false; }
    std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    auto document = builder::Document::from_text(text);
    if (!document) { notify(file_name(path) + L" is not a Composia UI design"); return false; }
    if (previewing()) { stop_preview(); }
    history_.reset(std::move(*document));
    path_ = path;
    select(std::nullopt);
    notify(L"Opened " + file_name(path));
    return true;
}

void BuilderDemoWindow::save() {
    if (previewing()) { return; }
    if (path_.empty()) { save_as(); } else { save_to(path_); }
}

void BuilderDemoWindow::save_as() {
    if (previewing()) { return; }
    std::wstring path(MAX_PATH, L'\0');
    if (!path_.empty() && path_.size() < path.size()) { std::ranges::copy(path_, path.begin()); }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd();
    dialog.lpstrFilter = fileFilter;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"cui";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) { save_to(path.c_str()); }
}

void BuilderDemoWindow::open() {
    if (previewing()) { return; }
    std::wstring path(MAX_PATH, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd();
    dialog.lpstrFilter = fileFilter;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) { open_from(path.c_str()); }
}

void BuilderDemoWindow::start_preview() {
    if (previewing()) { return; }
    end_drag(true);
    preview_ = std::make_unique<builder::FormView>(*this, document());
    {
        // The workspace must be able to show the form at its minimum size.
        const auto client = client_pixels();
        const auto windowDpi = dpi();
        const int neededWidth = MulDiv(document().minimum_width(), windowDpi, 96);
        const int neededHeight = MulDiv(document().minimum_height() + static_cast<int>(topBar + statusBar), windowDpi, 96);
        if (client.cx < neededWidth || client.cy < neededHeight) {
            RECT frame{};
            THROW_IF_WIN32_BOOL_FALSE(GetWindowRect(hwnd(), &frame));
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd(), nullptr, 0, 0, frame.right - frame.left + std::max(0L, neededWidth - client.cx),
                frame.bottom - frame.top + std::max(0L, neededHeight - client.cy), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        }
    }
    previewClick_ = preview_->on_click([this](const builder::Widget& widget) { notify(L"Clicked \"" + widget.text + L"\""); });
    previewChange_ = preview_->on_change([this](const builder::Widget& widget) {
        notify(widget.kind == Kind::checkbox ? widget.name + (widget.checked ? L" checked" : L" unchecked") : widget.name + L" = " + std::to_wstring(widget.value));
    });
    history_.seal();
    update_controls();
    arrange();
    SetFocus(hwnd());
    notify(L"Preview: try the controls, and resize the window to see the anchors at work. Escape returns to editing.");
}

void BuilderDemoWindow::stop_preview() {
    if (!previewing()) { return; }
    end_drag(true);
    previewClick_.disconnect();
    previewChange_.disconnect();
    preview_.reset();
    hover_.reset();
    update_controls();
    SetFocus(hwnd());
    notify(L"Back to editing");
}

void BuilderDemoWindow::toggle_snap() {
    snap_ = !snap_;
    notify(snap_ ? L"Snapping to the 8 DIP grid and to sibling edges" : L"Snapping off");
}

bool BuilderDemoWindow::build_app(const std::filesystem::path& output) {
    if (previewing()) { return false; }
    std::wstring error;
    const auto player = builder::player_bytes(&error);
    if (!player) { notify(error); return false; }
    if (!builder::write_app(*player, document(), output, &error)) { notify(error); return false; }
    std::error_code ignored;
    const auto bytes = std::filesystem::file_size(output, ignored);
    notify(L"Built " + output.filename().wstring() + L" (" + std::to_wstring((bytes + 1023) / 1024) + L" KB), a standalone app running this form");
    builtPath_ = output;
    update_controls();
    invalidate();
    return true;
}

void BuilderDemoWindow::build_app() {
    if (previewing()) { return; }
    std::wstring path(MAX_PATH, L'\0');
    const auto suggested = builder::app_file_name(document().title());
    if (suggested.size() < path.size()) { std::ranges::copy(suggested, path.begin()); }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd();
    dialog.lpstrFilter = appFilter;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"exe";
    dialog.lpstrTitle = L"Build app";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) { build_app(std::filesystem::path{path.c_str()}); }
}

void BuilderDemoWindow::run_built() {
    if (builtPath_.empty()) { return; }
    const auto path = builtPath_;
    const auto directory = path.parent_path().wstring();
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd(), L"open", path.c_str(), nullptr, directory.empty() ? nullptr : directory.c_str(), SW_SHOWNORMAL));
    if (result <= 32) { notify(L"Could not start " + path.filename().wstring()); }
}

void BuilderDemoWindow::layout_panes() {
    const auto size = target_.logical_size();
    const float paneHeight = std::max(1.0f, size.y - topBar - statusBar);
    if (previewing()) {
        palette_ = {0, topBar, 0, paneHeight};
        outline_ = {};
        inspector_ = {size.x, topBar, 0, paneHeight};
        workspace_ = {0, topBar, std::max(1.0f, size.x), paneHeight};
        form_ = workspace_;
        return;
    }
    palette_ = {0, topBar, paletteWidth, paneHeight};
    outline_ = {0, topBar + outlineTop, paletteWidth, std::max(1.0f, paneHeight - outlineTop - 8)};
    inspector_ = {size.x - inspectorWidth, topBar, inspectorWidth, paneHeight};
    workspace_ = {paletteWidth, topBar, std::max(1.0f, size.x - paletteWidth - inspectorWidth), paneHeight};
    const auto width = static_cast<float>(document().width()), height = static_cast<float>(document().height());
    form_ = {std::floor(workspace_.x + std::max(24.0f, (workspace_.width - width) / 2)),
        std::floor(workspace_.y + std::max(24.0f, (workspace_.height - height) / 2)), width, height};
}

void BuilderDemoWindow::refresh_placements() { placements_ = active().resolve(form_.width, form_.height); }

void BuilderDemoWindow::arrange() {
    layout_panes();
    refresh_placements();
    const auto size = target_.logical_size();
    if (size.x <= 0 || size.y <= 0) { return; }
    const auto visible = [](Window& window, bool shown) {
        if ((IsWindowVisible(window.hwnd()) != FALSE) != shown) { window.show(shown ? SW_SHOWNA : SW_HIDE); }
    };
    float x = 236;
    for (Button* button : {&newButton_, &openButton_, &saveButton_, &undoButton_, &redoButton_}) {
        button->set_bounds({x, 10, 72, 36});
        x += 80;
    }
    previewButton_.set_bounds({x + 8, 10, 104, 36});
    stopButton_.set_bounds({x + 8, 10, 132, 36});
    buildButton_.set_bounds({x + 120, 10, 112, 36});
    visible(buildButton_, !previewing());

    const bool preview = previewing();
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    rows_ = {};
    rows_.widget = widget != nullptr;
    rows_.kind = widget ? widget->kind : Kind::panel;
    float y = inspector_.y + 16;
    rows_.header = y;
    y += 60;
    if (widget) {
        rows_.name = y;
        y += 44;
        rows_.text = y;
        y += 44;
        rows_.position = y;
        y += 44;
        rows_.size = y;
        y += 44;
        if (widget->kind == Kind::checkbox || widget->kind == Kind::slider) { rows_.extra = y; y += 44; }
        y += 8;
        rows_.anchors = y;
        y += 48 + 4 * 40 + 12;
        rows_.actions = y;
        y += 88;
    } else {
        rows_.text = y;
        y += 44;
        rows_.size = y;
        y += 44;
        rows_.minimum = y;
        y += 64;
    }
    rows_.bottom = y;
    const float x0 = inspector_.x + 16, w = inspector_.width - 32, fieldX = x0 + 76, fieldW = std::max(1.0f, w - 76), half = std::max(1.0f, (fieldW - 8) / 2);
    const bool showWidget = widget && !preview, showForm = !widget && !preview;
    name_.set_bounds({fieldX, rows_.name + 2, fieldW, fieldHeight});
    text_.set_bounds({fieldX, rows_.text + 2, fieldW, fieldHeight});
    x_.set_bounds({fieldX, rows_.position + 2, half, fieldHeight});
    y_.set_bounds({fieldX + half + 8, rows_.position + 2, half, fieldHeight});
    width_.set_bounds({fieldX, rows_.size + 2, half, fieldHeight});
    height_.set_bounds({fieldX + half + 8, rows_.size + 2, half, fieldHeight});
    minWidth_.set_bounds({fieldX, rows_.minimum + 2, half, fieldHeight});
    minHeight_.set_bounds({fieldX + half + 8, rows_.minimum + 2, half, fieldHeight});
    value_.set_bounds({fieldX, rows_.extra + 2, 80, fieldHeight});
    visible(name_, showWidget);
    visible(text_, showWidget || showForm);
    visible(x_, showWidget);
    visible(y_, showWidget);
    visible(width_, showWidget || showForm);
    visible(height_, showWidget || showForm);
    visible(minWidth_, showForm);
    visible(minHeight_, showForm);
    visible(value_, showWidget && widget->kind == Kind::slider);
    for (std::size_t index = 0; index != builder::edges.size(); ++index) {
        const auto edge = builder::edges[index];
        auto& field = margin_field(edge);
        field.set_bounds({x0 + w - 64 - 32, rows_.anchors + 48 + static_cast<float>(index) * 40, 64, fieldHeight});
        visible(field, showWidget && widget->anchor(edge).target != Target::none);
    }
    const float actionW = std::max(1.0f, (w - 8) / 2);
    deleteButton_.set_bounds({x0, rows_.actions, actionW, 36});
    duplicateButton_.set_bounds({x0 + actionW + 8, rows_.actions, actionW, 36});
    frontButton_.set_bounds({x0, rows_.actions + 44, actionW, 36});
    backButton_.set_bounds({x0 + actionW + 8, rows_.actions + 44, actionW, 36});
    for (Button* button : {&deleteButton_, &duplicateButton_, &frontButton_, &backButton_}) { visible(*button, showWidget); }
    visible(runButton_, !preview && !builtPath_.empty());
    if (preview_) { preview_->arrange(form_); }
}

void BuilderDemoWindow::on_paint() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(hwnd())) { return; }
    app_.render([&] {
        target_.resize(pixels, dpi());
        arrange();
        draw();
    });
}

void BuilderDemoWindow::draw() {
    ScopedSurfaceDraw draw{target_.surface(), app_.graphics(), dpi()};
    const auto dc = draw.context().get();
    const auto size = target_.logical_size();
    dc->Clear(D2D1::ColorF(chrome));
    if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(ink), brush_.put())); }
    regions_.clear();
    FormPainter p{dc, draw.text_factory().get(), brush_.get()};
    p.text(L"Composia", {20, 11, 200, 22}, accent, {.size = 12, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    p.text(L"UI builder", {20, 27, 200, 24}, ink, {.size = 17, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    const auto title = (path_.empty() ? document().title() : file_name(path_)) + (history_.dirty() ? L"  ·  unsaved changes" : L"");
    p.text(title, {size.x - 420, 19, 404, 20}, inkMuted, {.size = 13, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    if (!previewing()) {
        draw_palette(p);
        draw_outline(p);
    }
    draw_form(p);
    draw_overlay(p);
    if (!previewing()) { draw_inspector(p); }
    draw_status(p);
    draw.finish();
    ++drawCount_;
}

void BuilderDemoWindow::draw_palette(FormPainter& p) {
    p.fill(palette_, pane);
    p.fill({palette_.x + palette_.width - 1, palette_.y, 1, palette_.height}, divider);
    p.text(L"WIDGETS", {16, palette_.y + 16, 180, 18}, inkMuted, {.size = 11, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    for (std::size_t index = 0; index != builder::kinds.size(); ++index) {
        const auto kind = builder::kinds[index];
        const layout::Rect row{8, palette_.y + 44 + static_cast<float>(index) * paletteRow, palette_.width - 16, paletteRow - 2};
        const bool hovered = hover_ && hover_->kind == Hit::palette && hover_->id == index;
        const bool placing = drag_.kind == DragKind::place && drag_.id == index;
        if (hovered || placing) { p.fill(row, placing ? selectedBg : hoverBg, 6); }
        p.glyph(kind, row.x + 12, row.y + 9, 16, placing ? accent : inkSoft);
        p.text(builder::kind_name(kind), {row.x + 40, row.y, row.width - 48, row.height}, ink, {.size = 14, .middle = true});
        regions_.push_back({Hit::palette, static_cast<unsigned>(index), row});
    }
    p.text(L"Click to add, or drag onto the form", {16, palette_.y + 44 + 7 * paletteRow + 4, palette_.width - 32, 18}, inkFaint, {.size = 11});
}

void BuilderDemoWindow::draw_outline(FormPainter& p) {
    const auto& document = this->document();
    p.text(L"OUTLINE", {16, outline_.y - 32, 120, 18}, inkMuted, {.size = 11, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    p.text(std::to_wstring(document.size()) + (document.size() == 1 ? L" widget" : L" widgets"), {outline_.x + outline_.width - 116, outline_.y - 32, 100, 18},
        inkFaint, {.size = 11, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    outlineExtent_ = static_cast<float>(placements_.size()) * outlineRow;
    outlineScroll_ = std::clamp(outlineScroll_, 0.0f, std::max(0.0f, outlineExtent_ - outline_.height));
    p.clip(outline_);
    if (placements_.empty()) {
        p.text(L"Nothing on the form yet", {outline_.x + 16, outline_.y + 8, outline_.width - 32, 20}, inkFaint, {.size = 12});
    }
    float y = outline_.y - outlineScroll_;
    const float bottom = outline_.y + outline_.height;
    for (const auto& placed : placements_) {
        if (y + outlineRow > outline_.y && y < bottom) {
            const auto& widget = *document.find(placed.id);
            const layout::Rect row{outline_.x + 8, y, outline_.width - 16, outlineRow - 2};
            const bool selected = selected_ == placed.id;
            const bool hovered = hover_ && hover_->kind == Hit::outline && hover_->id == placed.id;
            if (selected) { p.fill(row, selectedBg, 5); } else if (hovered) { p.fill(row, hoverBg, 5); }
            const float indent = 10 + static_cast<float>(document.depth(placed.id)) * 14;
            p.glyph(widget.kind, row.x + indent, row.y + 5, 14, selected ? accent : inkMuted);
            std::wstring anchored;
            for (const auto edge : builder::edges) {
                if (widget.anchor(edge).target != Target::none) { anchored.push_back(capitalized(builder::edge_name(edge))[0]); }
            }
            p.text(widget.name, {row.x + indent + 22, row.y, std::max(1.0f, row.width - indent - 62), row.height}, selected ? ink : inkSoft,
                {.size = 13, .weight = selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, .middle = true});
            p.text(anchored, {row.x + row.width - 44, row.y, 36, row.height}, placed.cyclic ? danger : accent,
                {.size = 10, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_TRAILING, .middle = true});
            const float top = std::max(row.y, outline_.y), clippedBottom = std::min(row.y + row.height, bottom);
            regions_.push_back({Hit::outline, placed.id, {row.x, top, row.width, std::max(0.0f, clippedBottom - top)}});
        }
        y += outlineRow;
    }
    p.unclip();
    p.scrollbar(outline_, outlineExtent_, outlineScroll_);
}

void BuilderDemoWindow::draw_form(FormPainter& p) {
    p.fill(workspace_, workspaceBg);
    p.clip(workspace_);
    const bool preview = previewing();
    if (!preview) {
        p.fill({form_.x - 2, form_.y + 2, form_.width + 4, form_.height + 6}, shadow, 10);
    }
    p.fill(form_, formBg, preview ? 0.0f : 6.0f);
    if (!preview) {
        const auto visible = intersect(form_, workspace_);
        if (visible.width > 0 && visible.height > 0) {
            brush_->SetColor(D2D1::ColorF(gridDot));
            const float startX = form_.x + std::ceil((visible.x - form_.x) / 16) * 16, startY = form_.y + std::ceil((visible.y - form_.y) / 16) * 16;
            for (float gx = startX; gx < visible.x + visible.width; gx += 16) {
                for (float gy = startY; gy < visible.y + visible.height; gy += 16) {
                    if (gx > form_.x && gy > form_.y) { p.fill({gx - 0.5f, gy - 0.5f, 1, 1}, gridDot); }
                }
            }
        }
        const auto minWidth = static_cast<float>(document().minimum_width()), minHeight = static_cast<float>(document().minimum_height());
        if (minWidth < form_.width || minHeight < form_.height) {
            p.stroke({form_.x, form_.y, minWidth, minHeight}, palette::border, 1);
            const auto label = L"min " + std::to_wstring(document().minimum_width()) + L" × " + std::to_wstring(document().minimum_height());
            const float labelWidth = p.measure(label, {.size = 10}) + 8;
            p.text(label, {form_.x + minWidth - labelWidth - 2, form_.y + minHeight - 16, labelWidth, 14}, inkFaint,
                {.size = 10, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
        }
        p.text(document().title(), {form_.x, form_.y - 22, form_.width, 18}, inkMuted, {.size = 12, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
        p.text(std::to_wstring(document().width()) + L" × " + std::to_wstring(document().height()), {form_.x, form_.y - 22, form_.width, 18}, inkFaint,
            {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    }
    if (preview_) {
        preview_->draw(p);
        for (const auto& region : preview_->regions()) {
            regions_.push_back({region.kind == builder::FormView::Interactive::checkbox ? Hit::preview_checkbox : Hit::preview_slider, region.id, region.bounds});
        }
    } else {
        for (const auto& placed : placements_) {
            const auto widget = document().find(placed.id);
            if (!widget) { continue; }
            const auto clip = builder::ancestor_clip(document(), placements_, *widget, {form_.x, form_.y}, workspace_);
            const auto r = to_window(placed.bounds);
            const auto visible = intersect(clip, r);
            if (visible.width <= 0 || visible.height <= 0) { continue; }
            p.clip(clip);
            builder::paint_widget(p, *widget, r);
            p.unclip();
        }
    }
    p.unclip();
}

void BuilderDemoWindow::draw_overlay(FormPainter& p) {
    if (previewing()) { return; }
    const auto& document = this->document();
    p.clip(workspace_);
    const auto formRect = [&]() -> layout::Rect { return form_; };
    const auto parent_rect = [&](const builder::Widget& widget) {
        if (widget.parent == 0) { return formRect(); }
        const auto placed = placement(widget.parent);
        return placed ? to_window(placed->bounds) : formRect();
    };
    if (hover_ && hover_->kind == Hit::widget && hover_->id != selected_ && drag_.kind == DragKind::none) {
        if (const auto placed = placement(hover_->id)) { p.stroke(to_window(placed->bounds), hoverOutline, 1); }
    }
    if (dropTarget_) {
        const auto placed = *dropTarget_ ? placement(*dropTarget_) : nullptr;
        p.stroke(*dropTarget_ ? (placed ? to_window(placed->bounds) : form_) : form_, accent, 2, 8);
    }
    if (drag_.kind == DragKind::place && drag_.moved && form_.contains(drag_.current.x, drag_.current.y)) {
        const auto size = builder::default_size(builder::kinds[drag_.id]);
        const layout::Rect ghost{drag_.current.x - static_cast<float>(size.width) / 2, drag_.current.y - static_cast<float>(size.height) / 2,
            static_cast<float>(size.width), static_cast<float>(size.height)};
        p.fill(ghost, accent, 6, 0.18f);
        p.stroke(ghost, accent, 1, 6);
    }
    const auto widget = selected_ ? document.find(*selected_) : nullptr;
    const auto placed = widget ? placement(widget->id) : nullptr;
    if (widget && placed) {
        const auto r = to_window(placed->bounds);
        p.stroke({r.x - 1, r.y - 1, r.width + 2, r.height + 2}, placed->cyclic ? danger : accent, 1.5f);
        const auto parent = parent_rect(*widget);
        for (const auto edge : builder::edges) {
            const auto& anchor = widget->anchor(edge);
            if (anchor.target == Target::none) { continue; }
            std::optional<float> target;
            if (anchor.target == Target::parent) { target = builder::edge_position(parent, edge); }
            else if (const auto sibling = placement(anchor.id)) { target = builder::edge_position(to_window(sibling->bounds), anchor.edge); }
            if (!target) { continue; }
            const float mine = builder::edge_position(r, edge);
            const float mid = builder::horizontal(edge) ? r.y + r.height / 2 : r.x + r.width / 2;
            const UINT32 color = placed->cyclic ? danger : accent;
            if (builder::horizontal(edge)) {
                p.line(mine, mid, *target, mid, color, 1);
                p.line(*target, mid - 6, *target, mid + 6, color, 1.5f);
            } else {
                p.line(mid, mine, mid, *target, color, 1);
                p.line(mid - 6, *target, mid + 6, *target, color, 1.5f);
            }
            const auto label = std::to_wstring(anchor.offset);
            const float labelW = p.measure(label, {.size = 10}) + 8;
            const float lx = builder::horizontal(edge) ? (mine + *target) / 2 - labelW / 2 : mid + 6;
            const float ly = builder::horizontal(edge) ? mid - 18 : (mine + *target) / 2 - 7;
            p.fill({lx, ly, labelW, 14}, chrome, 3);
            p.text(label, {lx, ly, labelW, 14}, color, {.size = 10, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_CENTER, .middle = true});
        }
        for (const auto edge : builder::edges) {
            const auto bounds = pin_bounds(edge);
            const float cx = bounds.x + bounds.width / 2, cy = bounds.y + bounds.height / 2;
            const bool attached = widget->anchor(edge).target != Target::none;
            const bool hot = (hover_ && hover_->kind == Hit::pin && hover_->id == static_cast<unsigned>(edge)) ||
                (drag_.kind == DragKind::pin && drag_.index == static_cast<unsigned>(edge));
            p.circle(cx, cy, pinRadius + (hot ? 1.5f : 0), formBg, true);
            if (attached) { p.circle(cx, cy, pinRadius + (hot ? 1.5f : 0), accent, true); }
            else { p.circle(cx, cy, pinRadius + (hot ? 1.5f : 0), hot ? ink : inkMuted, false, 1.5f); }
        }
        for (unsigned corner = 0; corner != 4; ++corner) {
            const auto handle = handle_bounds(corner);
            p.fill(handle, ink, 1.5f);
            p.stroke(handle, accent, 1, 1.5f);
        }
        if (drag_.kind == DragKind::pin && drag_.moved) {
            const auto bounds = pin_bounds(static_cast<Edge>(drag_.index));
            p.line(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, drag_.current.x, drag_.current.y, accent, 1.5f);
            if (pinTarget_ && pinTarget_->target != Target::none) {
                const auto edge = static_cast<Edge>(drag_.index);
                const auto targetRect = pinTarget_->target == Target::parent ? parent : to_window(placement(pinTarget_->id)->bounds);
                const auto targetEdge = pinTarget_->target == Target::parent ? edge : pinTarget_->edge;
                const float position = builder::edge_position(targetRect, targetEdge);
                if (builder::horizontal(targetEdge)) { p.line(position, targetRect.y, position, targetRect.y + targetRect.height, accent, 3); }
                else { p.line(targetRect.x, position, targetRect.x + targetRect.width, position, accent, 3); }
            }
        }
    }
    const layout::Rect corner{form_.x + form_.width - 6, form_.y + form_.height - 6, 12, 12};
    const bool cornerHot = (hover_ && hover_->kind == Hit::form_corner) || drag_.kind == DragKind::form;
    p.fill(corner, cornerHot ? accent : inkMuted, 2);
    regions_.push_back({Hit::form_corner, 0, {corner.x - 3, corner.y - 3, corner.width + 6, corner.height + 6}});
    p.unclip();
}

void BuilderDemoWindow::draw_inspector(FormPainter& p) {
    p.fill(inspector_, pane);
    p.fill({inspector_.x, inspector_.y, 1, inspector_.height}, divider);
    const float x0 = inspector_.x + 16, w = inspector_.width - 32, fieldX = x0 + 76;
    const auto& document = this->document();
    const auto widget = selected_ ? document.find(*selected_) : nullptr;
    const auto label = [&](std::wstring_view text, float y) { p.text(text, {x0, y + 2, 70, fieldHeight}, inkMuted, {.size = 13, .middle = true}); };
    if (!widget) {
        p.text(L"Form", {x0, rows_.header, w, 26}, ink, {.size = 18, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
        p.text(L"The window your widgets live in", {x0, rows_.header + 28, w, 18}, inkFaint, {.size = 12});
        label(L"Title", rows_.text);
        label(L"Size", rows_.size);
        label(L"Min size", rows_.minimum);
        p.text(L"The app window will not shrink below this, and neither will this form.", {x0, rows_.minimum + 38, w, 18}, inkFaint, {.size = 11});
        p.text(L"Add widgets from the palette, then select one to position it, resize it from its corners, and anchor its edges. "
            L"Drag a pin onto empty form space to anchor that edge to the form, or onto a neighbour to anchor to it. "
            L"Anchored edges keep their margin when the form changes size. Preview with F5 and resize the window to test it.",
            {x0, rows_.bottom + 12, w, 160}, inkMuted, {.size = 12, .wrap = true});
        p.text(L"Shortcuts", {x0, rows_.bottom + 148, w, 20}, inkSoft, {.size = 12, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
        p.text(L"Arrows nudge · Shift+arrows nudge 8 · Delete removes · Ctrl+D duplicates · Ctrl+Z / Ctrl+Y undo and redo · "
            L"Ctrl+S saves · Ctrl+O opens · Ctrl+N starts over · F5 previews · Escape deselects",
            {x0, rows_.bottom + 170, w, 120}, inkFaint, {.size = 11, .wrap = true});
        return;
    }
    p.text(builder::kind_name(widget->kind), {x0, rows_.header, w, 26}, ink, {.size = 18, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    const auto parent = widget->parent ? document.find(widget->parent) : nullptr;
    p.text(L"id " + std::to_wstring(widget->id) + L"  ·  in " + (parent ? parent->name : std::wstring{L"form"}), {x0, rows_.header + 28, w, 18}, inkFaint, {.size = 12});
    label(L"Name", rows_.name);
    label(widget->kind == Kind::text_field ? L"Placeholder" : widget->kind == Kind::panel ? L"Title" : L"Text", rows_.text);
    label(L"Position", rows_.position);
    label(L"Size", rows_.size);
    if (widget->kind == Kind::checkbox) {
        label(L"Checked", rows_.extra);
        const layout::Rect box{fieldX, rows_.extra + 9, 18, 18};
        if (widget->checked) { p.fill(box, accent, 4); p.check(box.x, box.y, 18, buttonInk, 2); }
        else { p.fill(box, fieldBg, 4); p.stroke(box, inkMuted, 1.2f, 4); }
        p.text(widget->checked ? L"On" : L"Off", {fieldX + 28, rows_.extra + 2, 80, fieldHeight}, inkSoft, {.size = 13, .middle = true});
        regions_.push_back({Hit::checked, widget->id, {fieldX, rows_.extra + 2, 100, fieldHeight}});
    } else if (widget->kind == Kind::slider) {
        label(L"Value", rows_.extra);
        p.text(L"0 to 100", {fieldX + 90, rows_.extra + 2, 100, fieldHeight}, inkFaint, {.size = 12, .middle = true});
    }
    p.text(L"ANCHORS", {x0, rows_.anchors + 4, w, 18}, inkMuted, {.size = 11, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    p.text(L"Drag a pin on the form, or click a target to cycle it", {x0, rows_.anchors + 24, w, 18}, inkFaint, {.size = 11});
    const auto placed = placement(widget->id);
    for (std::size_t index = 0; index != builder::edges.size(); ++index) {
        const auto edge = builder::edges[index];
        const float ry = rows_.anchors + 48 + static_cast<float>(index) * 40;
        const auto& anchor = widget->anchor(edge);
        const bool attached = anchor.target != Target::none;
        p.text(capitalized(builder::edge_name(edge)), {x0, ry, 52, fieldHeight}, inkSoft, {.size = 13, .middle = true});
        const layout::Rect chip{x0 + 52, ry, w - 52 - 64 - 32 - 8, fieldHeight};
        const bool hovered = hover_ && hover_->kind == Hit::anchor_target && hover_->id == index;
        p.fill(chip, hovered ? selectedBg : chipBg, 6);
        p.text(anchor_label(anchor), {chip.x + 10, chip.y, chip.width - 20, chip.height}, attached ? (placed && placed->cyclic ? danger : accent) : inkMuted,
            {.size = 13, .weight = attached ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, .middle = true});
        regions_.push_back({Hit::anchor_target, static_cast<unsigned>(index), chip});
        if (attached) {
            const layout::Rect clear{x0 + w - 26, ry + 4, 24, 24};
            const bool clearHot = hover_ && hover_->kind == Hit::anchor_clear && hover_->id == index;
            p.fill(clear, clearHot ? selectedBg : chipBg, 6);
            p.cross(clear.x + 8, clear.y + 8, 8, clearHot ? ink : inkMuted);
            regions_.push_back({Hit::anchor_clear, static_cast<unsigned>(index), clear});
        } else {
            p.text(L"margin", {x0 + w - 64 - 32, ry, 64, fieldHeight}, inkFaint, {.size = 11, .align = DWRITE_TEXT_ALIGNMENT_CENTER, .middle = true});
        }
    }
    if (placed && placed->cyclic) {
        p.text(L"Anchors form a cycle; one of them is ignored.", {x0, rows_.actions - 10, w, 18}, danger, {.size = 11});
    }
}

void BuilderDemoWindow::draw_status(FormPainter& p) {
    const auto size = target_.logical_size();
    const layout::Rect bar{0, size.y - statusBar, size.x, statusBar};
    p.fill(bar, chrome);
    p.fill({0, bar.y, size.x, 1}, divider);
    const auto hint = previewing() ? std::wstring{L"Preview"} : L"Select a widget on the form, drag its corners to resize it, and drag its pins to anchor edges";
    const auto message = status_.empty() ? hint : status_;
    const float messageWidth = std::clamp(p.measure(message, {.size = 12}) + 4, 1.0f, std::max(1.0f, size.x - 500));
    p.text(message, {16, bar.y + 5, messageWidth, 18}, status_.empty() ? inkMuted : ink, {.size = 12});
    runButton_.set_bounds({16 + messageWidth + 10, bar.y + 3, 80, 22});
    const auto& active = this->active();
    const auto summary = std::to_wstring(static_cast<int>(std::lround(form_.width))) + L" × " + std::to_wstring(static_cast<int>(std::lround(form_.height))) +
        (previewing() ? std::wstring{} : L"   ·   min " + std::to_wstring(active.minimum_width()) + L" × " + std::to_wstring(active.minimum_height())) +
        L"   ·   " + std::to_wstring(active.size()) + (active.size() == 1 ? L" widget" : L" widgets");
    p.text(summary, {size.x - 480, bar.y + 5, 316, 18}, inkFaint, {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    if (!previewing()) {
        const layout::Rect toggle{size.x - 150, bar.y + 4, 134, 20};
        const layout::Rect box{toggle.x, toggle.y + 3, 14, 14};
        if (snap_) { p.fill(box, accent, 3); p.check(box.x, box.y, 14, buttonInk, 1.6f); }
        else { p.stroke(box, inkMuted, 1.2f, 3); }
        p.text(L"Snap to grid", {toggle.x + 22, toggle.y, 110, 20}, hover_ && hover_->kind == Hit::snap ? ink : inkSoft, {.size = 12, .middle = true});
        regions_.push_back({Hit::snap, 0, toggle});
    }
}

std::optional<unsigned> BuilderDemoWindow::widget_at(numerics::float2 point, bool containersOnly, unsigned exclude) const noexcept {
    const auto& document = active();
    for (auto it = placements_.rbegin(); it != placements_.rend(); ++it) {
        const auto widget = document.find(it->id);
        if (!widget || it->id == exclude || (exclude && document.is_ancestor(exclude, it->id))) { continue; }
        if (containersOnly && !widget->container()) { continue; }
        auto bounds = it->bounds;
        for (auto parent = widget->parent; parent != 0;) {
            const auto ancestor = placement(parent);
            if (!ancestor) { break; }
            bounds = intersect(bounds, ancestor->bounds);
            parent = document.find(parent)->parent;
        }
        if (bounds.contains(point.x, point.y)) { return it->id; }
    }
    return std::nullopt;
}

std::optional<BuilderDemoWindow::Region> BuilderDemoWindow::hit_test(float x, float y) const noexcept {
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it) {
        if (it->bounds.contains(x, y)) { return *it; }
    }
    if (previewing() || !workspace_.contains(x, y)) { return std::nullopt; }
    if (selected_ && placement(*selected_)) {
        for (unsigned corner = 0; corner != 4; ++corner) {
            const auto handle = handle_bounds(corner);
            if (layout::Rect{handle.x - 3, handle.y - 3, handle.width + 6, handle.height + 6}.contains(x, y)) { return Region{Hit::handle, corner, handle}; }
        }
        for (const auto edge : builder::edges) {
            const auto pin = pin_bounds(edge);
            if (pin.contains(x, y)) { return Region{Hit::pin, static_cast<unsigned>(edge), pin}; }
        }
    }
    if (const auto id = widget_at(to_form({x, y}), false, 0)) { return Region{Hit::widget, *id, to_window(placement(*id)->bounds)}; }
    return std::nullopt;
}

layout::Rect BuilderDemoWindow::parent_rect(unsigned id) const noexcept {
    const auto widget = active().find(id);
    if (!widget || widget->parent == 0) { return {0, 0, form_.width, form_.height}; }
    const auto parent = placement(widget->parent);
    return parent ? parent->bounds : layout::Rect{};
}

std::optional<Anchor> BuilderDemoWindow::pin_target(Edge edge, numerics::float2 point) const noexcept {
    const auto widget = selected_ ? document().find(*selected_) : nullptr;
    const auto placed = widget ? placement(widget->id) : nullptr;
    if (!widget || !placed) { return std::nullopt; }
    if (placed->bounds.contains(point.x, point.y)) { return Anchor{}; }
    const auto siblings = document().siblings(widget->id);
    for (auto it = placements_.rbegin(); it != placements_.rend(); ++it) {
        if (std::ranges::find(siblings, it->id) == siblings.end() || !it->bounds.contains(point.x, point.y)) { continue; }
        const auto& r = it->bounds;
        Edge targetEdge;
        if (builder::horizontal(edge)) { targetEdge = std::abs(point.x - r.x) <= std::abs(point.x - (r.x + r.width)) ? Edge::left : Edge::right; }
        else { targetEdge = std::abs(point.y - r.y) <= std::abs(point.y - (r.y + r.height)) ? Edge::top : Edge::bottom; }
        return Anchor{Target::widget, it->id, targetEdge, 0};
    }
    if (parent_rect(widget->id).contains(point.x, point.y)) { return Anchor{Target::parent, 0, edge, 0}; }
    return std::nullopt;
}

layout::Rect BuilderDemoWindow::snapped(layout::Rect desired, unsigned id) const noexcept {
    if (!snap_ || !document().find(id)) { return desired; }
    const auto& document = this->document();
    const auto parentRect = parent_rect(id);
    std::vector<float> xs{parentRect.x, parentRect.x + parentRect.width}, ys{parentRect.y, parentRect.y + parentRect.height};
    for (const auto sibling : document.siblings(id)) {
        if (const auto placed = placement(sibling)) {
            xs.push_back(placed->bounds.x);
            xs.push_back(placed->bounds.x + placed->bounds.width);
            ys.push_back(placed->bounds.y);
            ys.push_back(placed->bounds.y + placed->bounds.height);
        }
    }
    const auto align = [](float start, float length, const std::vector<float>& candidates, float origin) {
        float best = origin + snap_grid(start - origin), bestDistance = snapDistance;
        for (const auto candidate : candidates) {
            for (const auto edge : {start, start + length}) {
                const float distance = std::abs(candidate - edge);
                if (distance < bestDistance) { bestDistance = distance; best = start + (candidate - edge); }
            }
        }
        return best;
    };
    desired.x = align(desired.x, desired.width, xs, parentRect.x);
    desired.y = align(desired.y, desired.height, ys, parentRect.y);
    return desired;
}

void BuilderDemoWindow::begin_drag(DragKind kind, unsigned id, unsigned index, numerics::float2 point) {
    drag_ = {kind, id, index, point, point, {}, false, false};
    if (kind == DragKind::move || kind == DragKind::resize) {
        if (const auto placed = placement(id)) { drag_.original = placed->bounds; }
    } else if (kind == DragKind::form) {
        drag_.original = {0, 0, static_cast<float>(document().width()), static_cast<float>(document().height())};
    }
    SetCapture(hwnd());
}

void BuilderDemoWindow::update_drag(numerics::float2 point) {
    if (drag_.kind == DragKind::none) { return; }
    drag_.current = point;
    const numerics::float2 delta{point.x - drag_.start.x, point.y - drag_.start.y};
    if (!drag_.moved && std::abs(delta.x) < dragThreshold && std::abs(delta.y) < dragThreshold) { return; }
    drag_.moved = true;
    const auto record = [&] { if (!drag_.recorded) { changed(); drag_.recorded = true; } };
    const auto formPoint = to_form(point);
    switch (drag_.kind) {
    case DragKind::place:
        dropTarget_ = form_.contains(point.x, point.y) ? std::optional{widget_at(formPoint, true, 0).value_or(0)} : std::nullopt;
        break;
    case DragKind::move: {
        record();
        auto desired = drag_.original;
        desired.x += delta.x;
        desired.y += delta.y;
        desired = snapped(desired, drag_.id);
        document().fit(drag_.id, desired);
        refresh_placements();
        const auto widget = document().find(drag_.id);
        const auto container = widget_at(formPoint, true, drag_.id).value_or(0);
        dropTarget_ = widget && container != widget->parent && form_.contains(point.x, point.y) ? std::optional{container} : std::nullopt;
        sync_inspector();
        break;
    }
    case DragKind::resize: {
        record();
        const auto& original = drag_.original;
        const float minimum = static_cast<float>(builder::Document::minimum_size);
        const bool left = drag_.index == 0 || drag_.index == 3, top = drag_.index < 2;
        float l = original.x, t = original.y, r = original.x + original.width, b = original.y + original.height;
        (left ? l : r) += delta.x;
        (top ? t : b) += delta.y;
        if (snap_) {
            const auto origin = parent_rect(drag_.id);
            if (left) { l = origin.x + snap_grid(l - origin.x); } else { r = origin.x + snap_grid(r - origin.x); }
            if (top) { t = origin.y + snap_grid(t - origin.y); } else { b = origin.y + snap_grid(b - origin.y); }
        }
        if (left) { l = std::min(l, r - minimum); } else { r = std::max(r, l + minimum); }
        if (top) { t = std::min(t, b - minimum); } else { b = std::max(b, t + minimum); }
        document().fit(drag_.id, {l, t, r - l, b - t});
        refresh_placements();
        sync_inspector();
        break;
    }
    case DragKind::pin:
        pinTarget_ = pin_target(static_cast<Edge>(drag_.index), formPoint);
        break;
    case DragKind::form: {
        if (!drag_.recorded) { changed("form-drag"); drag_.recorded = true; }
        float width = drag_.original.width + delta.x, height = drag_.original.height + delta.y;
        if (snap_) { width = snap_grid(width); height = snap_grid(height); }
        document().resize(static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
        sync_inspector();
        break;
    }
    case DragKind::slider:
        if (preview_) { preview_->pointer_move(point); }
        break;
    case DragKind::none:
        break;
    }
    invalidate();
}

void BuilderDemoWindow::end_drag(bool cancel) {
    if (drag_.kind == DragKind::none) { return; }
    const auto drag = std::exchange(drag_, {});
    const auto dropTarget = std::exchange(dropTarget_, std::nullopt);
    const auto pinTarget = std::exchange(pinTarget_, std::nullopt);
    if (GetCapture() == hwnd()) { ReleaseCapture(); }
    if (cancel) {
        if (drag.recorded) { history_.undo(); select(selected_); }
        if (preview_) { preview_->pointer_up(); }
        invalidate();
        return;
    }
    switch (drag.kind) {
    case DragKind::place: {
        const auto kind = builder::kinds[drag.id];
        if (!drag.moved) { add_widget(kind); break; }
        if (!form_.contains(drag.current.x, drag.current.y)) { break; }
        const auto formPoint = to_form(drag.current);
        const auto parent = widget_at(formPoint, true, 0).value_or(0);
        const auto parentRect = parent ? placement(parent)->bounds : layout::Rect{0, 0, form_.width, form_.height};
        const auto size = builder::default_size(kind);
        float x = formPoint.x - static_cast<float>(size.width) / 2 - parentRect.x, y = formPoint.y - static_cast<float>(size.height) / 2 - parentRect.y;
        if (snap_) { x = snap_grid(x); y = snap_grid(y); }
        add_widget(kind, parent, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
        break;
    }
    case DragKind::move:
        if (drag.moved && dropTarget) {
            document().reparent(drag.id, *dropTarget);
            const auto parent = document().find(*dropTarget);
            sync_inspector();
            notify(L"Moved " + document().find(drag.id)->name + L" into " + (parent ? parent->name : std::wstring{L"the form"}));
        } else if (drag.moved) {
            history_.seal();
        }
        break;
    case DragKind::pin: {
        const auto edge = static_cast<Edge>(drag.index);
        const auto widget = selected_ ? document().find(*selected_) : nullptr;
        if (!widget) { break; }
        if (!drag.moved) {
            if (widget->anchor(edge).target != Target::none) { release_anchor(edge); }
            else { set_anchor(edge, {Target::parent, 0, edge, 0}); }
        } else if (pinTarget) {
            if (pinTarget->target == Target::none) { release_anchor(edge); }
            else { set_anchor(edge, *pinTarget); }
        }
        break;
    }
    case DragKind::resize:
    case DragKind::form:
        history_.seal();
        break;
    case DragKind::slider:
        if (preview_) { preview_->pointer_up(); }
        break;
    case DragKind::none:
        break;
    }
    invalidate();
}

std::optional<LRESULT> BuilderDemoWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        const auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
        int width = 1100, height = 700;
        if (preview_) {
            // Previewing honours the form's minimum size, as the built app will.
            width = std::max(width, preview_->document().minimum_width());
            height = std::max(height, preview_->document().minimum_height() + static_cast<int>(topBar + statusBar));
        }
        info->ptMinTrackSize = {MulDiv(width, windowDpi, 96), MulDiv(height, windowDpi, 96)};
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL) {
            if (drag_.kind != DragKind::none) { end_drag(true); }
            else if (previewing()) { stop_preview(); }
            else if (selected_) { select(std::nullopt); SetFocus(hwnd()); }
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            LPCWSTR cursor = IDC_ARROW;
            if (drag_.kind == DragKind::move && drag_.moved) { cursor = IDC_SIZEALL; }
            else if (drag_.kind == DragKind::place && drag_.moved) { cursor = IDC_CROSS; }
            else if (hover_) {
                switch (hover_->kind) {
                case Hit::palette: case Hit::outline: case Hit::anchor_target: case Hit::anchor_clear: case Hit::checked: case Hit::snap: case Hit::pin:
                case Hit::preview_checkbox: case Hit::preview_slider:
                    cursor = IDC_HAND;
                    break;
                case Hit::handle: cursor = hover_->id == 0 || hover_->id == 2 ? IDC_SIZENWSE : IDC_SIZENESW; break;
                case Hit::form_corner: cursor = IDC_SIZENWSE; break;
                case Hit::widget: break;
                }
            }
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        const auto point = pointer(lparam);
        SetFocus(hwnd());
        const auto hit = hit_test(point.x, point.y);
        if (previewing()) {
            if (preview_->pointer_down(point) == builder::FormView::Pointer::dragging) { begin_drag(DragKind::slider, 0, 0, point); }
            invalidate();
            return 0;
        }
        if (!hit) {
            if (workspace_.contains(point.x, point.y) && selected_) { select(std::nullopt); }
            return 0;
        }
        switch (hit->kind) {
        case Hit::palette: begin_drag(DragKind::place, hit->id, 0, point); invalidate(); break;
        case Hit::outline: select(hit->id); break;
        case Hit::anchor_target: cycle_anchor(builder::edges[hit->id]); break;
        case Hit::anchor_clear: release_anchor(builder::edges[hit->id]); break;
        case Hit::checked:
            if (const auto widget = selected_ ? document().find(*selected_) : nullptr) { changed(); widget->checked = !widget->checked; invalidate(); }
            break;
        case Hit::snap: toggle_snap(); break;
        case Hit::widget:
            if (selected_ != hit->id) { select(hit->id); }
            begin_drag(DragKind::move, hit->id, 0, point);
            break;
        case Hit::handle: if (selected_) { begin_drag(DragKind::resize, *selected_, hit->id, point); } break;
        case Hit::pin: if (selected_) { begin_drag(DragKind::pin, *selected_, hit->id, point); invalidate(); } break;
        case Hit::form_corner: begin_drag(DragKind::form, 0, 0, point); invalidate(); break;
        case Hit::preview_checkbox: case Hit::preview_slider: break;
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const auto point = pointer(lparam);
        if (drag_.kind != DragKind::none) { update_drag(point); return 0; }
        const auto hit = hit_test(point.x, point.y);
        const bool changedHover = hit.has_value() != hover_.has_value() || (hit && (hit->kind != hover_->kind || hit->id != hover_->id));
        if (changedHover) { hover_ = hit; invalidate(); }
        if (!tracking_) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd(), 0};
            THROW_IF_WIN32_BOOL_FALSE(TrackMouseEvent(&tracking));
            tracking_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hover_) { hover_.reset(); invalidate(); }
        return 0;
    case WM_LBUTTONUP:
        if (drag_.kind != DragKind::none) { update_drag(pointer(lparam)); end_drag(false); }
        return 0;
    case WM_CAPTURECHANGED:
        if (drag_.kind != DragKind::none && reinterpret_cast<HWND>(lparam) != hwnd()) { end_drag(true); }
        return 0;
    case WM_CANCELMODE:
        end_drag(true);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd(), &point);
        const auto position = pointer(MAKELPARAM(point.x, point.y));
        if (!previewing() && outline_.contains(position.x, position.y)) {
            outlineScroll_ -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * outlineRow * 3;
            invalidate();
        }
        return 0;
    }
    case WM_KEYDOWN: {
        const bool control = control_down();
        const int step = shift_down() ? 8 : 1;
        if (wparam == VK_F5) { if (previewing()) { stop_preview(); } else { start_preview(); } return 0; }
        if (previewing()) { break; }
        switch (wparam) {
        case VK_DELETE: remove_selected(); return 0;
        case VK_LEFT: nudge(-step, 0); return 0;
        case VK_RIGHT: nudge(step, 0); return 0;
        case VK_UP: nudge(0, -step); return 0;
        case VK_DOWN: nudge(0, step); return 0;
        case 'Z': if (control) { if (shift_down()) { redo(); } else { undo(); } return 0; } break;
        case 'Y': if (control) { redo(); return 0; } break;
        case 'D': if (control) { duplicate_selected(); return 0; } break;
        case 'S': if (control) { if (shift_down()) { save_as(); } else { save(); } return 0; } break;
        case 'O': if (control) { open(); return 0; } break;
        case 'N': if (control) { new_document(); return 0; } break;
        case 'B': if (control) { build_app(); return 0; } break;
        case VK_OEM_6: if (control) { bring_forward(); return 0; } break;
        case VK_OEM_4: if (control) { send_backward(); return 0; } break;
        }
        break;
    }
    }
    return std::nullopt;
}
