#include "AppPackager.hpp"
#include "BuilderDemoWindow.hpp"
#include "FormPlayerWindow.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

using builder::Anchor;
using builder::Document;
using builder::Edge;
using builder::Kind;
using builder::Target;

namespace {
void require(bool value, const char* reason) { if (!value) { throw std::runtime_error(reason); } }

bool close_to(float a, float b, float tolerance = 0.5f) { return std::abs(a - b) <= tolerance; }

composia::layout::Rect bounds_of(const Document& document, unsigned id, float width = 0, float height = 0) {
    const auto placements = width > 0 ? document.resolve(width, height) : document.resolve();
    const auto it = std::ranges::find(placements, id, &builder::Placement::id);
    require(it != placements.end(), "Widget was not placed");
    return it->bounds;
}

unsigned by_name(const Document& document, std::wstring_view name) {
    for (const auto& widget : document.widgets()) { if (widget.name == name) { return widget.id; } }
    throw std::runtime_error("Named widget is missing from the sample");
}

void model() {
    auto document = Document::sample();
    require(document.size() >= 10 && document.width() == 640 && document.height() == 440, "Sample form is unexpectedly small");
    require(document.minimum_width() == 480 && document.minimum_height() == 360, "Sample minimum size is wrong");
    {
        Document form;
        require(form.minimum_width() == Document::minimum_form && form.minimum_height() == Document::minimum_form, "A new form should start at the smallest allowed minimum");
        form.set_minimum(500, 100);
        require(form.minimum_width() == 500 && form.minimum_height() == Document::minimum_form, "Minimum should clamp to the allowed range");
        form.set_minimum(900, 900);
        require(form.minimum_width() == form.width() && form.minimum_height() == form.height(), "Minimum cannot exceed the form size");
        form.set_minimum(500, 400);
        form.resize(300, 300);
        require(form.width() == 500 && form.height() == 400, "The form cannot shrink below its minimum");
        form.resize(800, 600);
        require(form.width() == 800 && form.height() == 600 && form.minimum_width() == 500, "Growing the form should keep the minimum");
        const auto reloaded = Document::from_text(form.to_text());
        require(reloaded && reloaded->minimum_width() == 500 && reloaded->minimum_height() == 400, "Minimum size did not survive the text format");
        const auto legacy = Document::from_text("composia-ui 1\nform 300 200 \"old\"\n");
        require(legacy && legacy->minimum_width() == Document::minimum_form && legacy->minimum_height() == Document::minimum_form, "Files without a minimum should load");
        const auto reordered = Document::from_text("composia-ui 1\nminimum 280 190\nform 300 200 \"old\"\n");
        require(reordered && reordered->minimum_width() == 280 && reordered->minimum_height() == 190, "A minimum before the form line should still apply");
        require(!Document::from_text("composia-ui 1\nform 300 200 \"old\"\nminimum 10\n").has_value(), "A malformed minimum line was accepted");
    }
    const auto panel = by_name(document, L"account"), email = by_name(document, L"email"), signIn = by_name(document, L"signIn"),
        cancel = by_name(document, L"cancel"), footer = by_name(document, L"footer"), logo = by_name(document, L"logo");
    require(document.find(email)->parent == panel && document.depth(email) == 1 && document.is_ancestor(panel, email), "Sample nesting is wrong");
    const auto placements = document.resolve();
    require(placements.size() == document.size() && placements.front().id == panel, "Draw order should start with the panel");
    require(std::ranges::none_of(placements, &builder::Placement::cyclic), "Sample should resolve without cycles");

    // Parent anchors stretch and stick when the form changes size.
    const auto panelAt640 = bounds_of(document, panel), panelAt900 = bounds_of(document, panel, 900, 600);
    require(close_to(panelAt640.x, 32) && close_to(panelAt640.width, 576) && close_to(panelAt900.width, 836) && close_to(panelAt900.x, 32), "Panel did not stretch with the form");
    const auto emailAt900 = bounds_of(document, email, 900, 600);
    require(close_to(emailAt900.x, 32 + 24) && close_to(emailAt900.width, 836 - 48), "Nested field did not follow its panel");
    const auto signInAt900 = bounds_of(document, signIn, 900, 600);
    require(close_to(signInAt900.x + signInAt900.width, 900 - 32) && close_to(signInAt900.y + signInAt900.height, 600 - 32), "Button did not stick to the corner");
    const auto cancelAt900 = bounds_of(document, cancel, 900, 600);
    require(close_to(cancelAt900.x + cancelAt900.width, signInAt900.x - 8) && close_to(cancelAt900.y + cancelAt900.height, signInAt900.y + signInAt900.height),
        "Sibling anchors did not follow the target");
    const auto logoAt900 = bounds_of(document, logo, 900, 600), footerAt900 = bounds_of(document, footer, 900, 600);
    require(close_to(footerAt900.x, logoAt900.x + logoAt900.width + 16), "Left-to-right sibling anchor is wrong");

    // Fitting derives margins from a desired rectangle without moving the widget.
    const auto before = bounds_of(document, signIn);
    require(document.fit(signIn, {before.x - 40, before.y - 10, before.width + 20, before.height}), "Fit rejected a valid widget");
    const auto after = bounds_of(document, signIn);
    require(close_to(after.x, before.x - 40) && close_to(after.width, before.width + 20) && close_to(after.y, before.y - 10), "Fit did not land on the requested rectangle");
    require(document.find(signIn)->anchor(Edge::right).offset == 32 + 20 && document.find(signIn)->anchor(Edge::bottom).offset == 42, "Fit did not update margins");
    require(bounds_of(document, cancel).x + bounds_of(document, cancel).width == after.x - 8, "Sibling did not follow a fitted widget");

    // Anchor validation and release.
    require(!document.set_anchor(email, Edge::left, {Target::widget, signIn, Edge::right, 0}), "Anchor across parents was accepted");
    require(!document.set_anchor(cancel, Edge::left, {Target::widget, signIn, Edge::top, 0}), "Anchor across axes was accepted");
    require(!document.set_anchor(cancel, Edge::left, {Target::widget, cancel, Edge::right, 0}), "Self anchor was accepted");
    require(document.set_anchor(cancel, Edge::left, {Target::widget, signIn, Edge::right, 4}), "Valid sibling anchor was rejected");
    require(document.find(cancel)->anchor(Edge::left).target == Target::widget, "Anchor was not stored");
    require(document.release(cancel, Edge::left) && document.find(cancel)->anchor(Edge::left).target == Target::none, "Release failed");

    // Cycles are tolerated and flagged rather than hanging the solver.
    const auto a = document.add(Kind::button, 0, 10, 10), b = document.add(Kind::button, 0, 200, 10);
    require(a && b && document.set_anchor(a, Edge::left, {Target::widget, b, Edge::right, 8}) && document.set_anchor(b, Edge::left, {Target::widget, a, Edge::right, 8}),
        "Could not build a cycle");
    const auto cyclic = document.resolve();
    require(std::ranges::count_if(cyclic, &builder::Placement::cyclic) == 1, "Exactly one anchor in a two-widget cycle should be ignored");
    require(document.remove(b) && document.find(a)->anchor(Edge::left).target == Target::none, "Removing the target did not release the anchor");
    require(document.remove(a), "Remove failed");

    // Reparenting keeps the placement and drops sibling anchors; removal is recursive.
    const auto cancelBefore = bounds_of(document, cancel);
    require(document.reparent(cancel, panel) && document.find(cancel)->parent == panel, "Reparent failed");
    const auto cancelAfter = bounds_of(document, cancel);
    require(close_to(cancelAfter.x, cancelBefore.x) && close_to(cancelAfter.y, cancelBefore.y), "Reparent moved the widget");
    require(document.find(cancel)->anchor(Edge::right).target == Target::none && document.find(cancel)->anchor(Edge::bottom).target == Target::none,
        "Sibling anchors survived reparenting");
    require(!document.reparent(panel, panel) && !document.reparent(panel, cancel), "Reparent accepted an invalid parent");
    require(!document.add(Kind::label, signIn, 0, 0), "A button accepted children");
    const auto nested = document.children(panel).size();
    const auto copy = document.duplicate(panel);
    require(copy && document.children(copy).size() == nested && document.find(copy)->name != document.find(panel)->name, "Duplicate did not copy the subtree");
    require(document.find(copy)->x == document.find(panel)->x + 16, "Duplicate should sit beside the original");
    const auto total = document.size();
    require(document.remove(copy) && document.size() == total - nested - 1, "Removing a panel did not remove its children");

    // Z-order among siblings.
    const auto order = document.children(0);
    require(order.size() >= 3, "Expected several root widgets");
    require(document.raise(order[0]) && document.children(0)[1] == order[0] && document.lower(order[0]) && document.children(0)[0] == order[0], "Raise or lower failed");
    require(!document.lower(order[0]) && !document.raise(document.children(0).back()), "Raising past the ends was accepted");

    // Text round trip and rejection of bad input.
    const auto text = document.to_text();
    const auto loaded = Document::from_text(text);
    require(loaded.has_value() && loaded->to_text() == text && loaded->size() == document.size() && loaded->title() == document.title(), "Text round trip changed the document");
    require(loaded->resolve().size() == document.resolve().size(), "Loaded document does not resolve");
    for (const auto& widget : document.widgets()) {
        const auto twin = loaded->find(widget.id);
        require(twin && *twin == widget, "A widget changed in the round trip");
    }
    require(!Document::from_text("not a design").has_value() && !Document::from_text("composia-ui 1\nwidget 1 button 99 0 0 10 10 0 0 \"a\" \"b\"\n").has_value(),
        "Invalid text was accepted");
    require(!Document::from_text("composia-ui 1\nform 10 10 \"unterminated\n").has_value(), "Unterminated string was accepted");
    const auto quoted = Document::from_text("composia-ui 1\nform 300 200 \"Say \\\"hi\\\"\\nline\"\nwidget 1 label 0 1 2 30 20 0 0 \"n\" \"t\\\\t\"\nanchor 1 left parent 5\nanchor 1 top 7 bottom 0\n");
    require(quoted && quoted->title() == L"Say \"hi\"\nline" && quoted->find(1)->text == L"t\\t", "Escapes were not decoded");
    require(quoted->find(1)->anchor(Edge::left).target == Target::parent && quoted->find(1)->anchor(Edge::top).target == Target::none, "Anchors to missing targets should be dropped");
    require(builder::parse_int(L" 42 ") == 42 && builder::parse_int(L"-7") == -7 && !builder::parse_int(L"") && !builder::parse_int(L"4x"), "Integer parsing is wrong");

    // History coalesces keyed edits.
    builder::History history{Document::sample()};
    require(!history.can_undo() && !history.dirty(), "Fresh history should be clean");
    history.record("text");
    history.document().find(signIn)->text = L"A";
    history.record("text");
    history.document().find(signIn)->text = L"AB";
    history.record();
    history.document().resize(700, 500);
    require(history.dirty() && history.undo() && history.document().width() == 640 && history.document().find(signIn)->text == L"AB", "Undo restored the wrong snapshot");
    require(history.undo() && history.document().find(signIn)->text == L"Sign in" && !history.can_undo(), "Coalesced edits should undo together");
    require(history.redo() && history.document().find(signIn)->text == L"AB" && history.redo() && history.document().width() == 700 && !history.can_redo(), "Redo failed");
    history.mark_saved();
    require(!history.dirty(), "Marking saved did not clear dirty state");
}

void editor(bool warp) {
    composia::Application app{warp};
    {
        BuilderDemoWindow window{app};
        window.show();
        const auto paint = [&] { UpdateWindow(window.hwnd()); };
        const auto scale = [&] { return static_cast<float>(window.dpi()) / 96.0f; };
        const auto lparam = [&](float x, float y) { return MAKELPARAM(static_cast<int>(std::lround(x * scale())), static_cast<int>(std::lround(y * scale()))); };
        const auto press = [&](float x, float y) { SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, lparam(x, y)); };
        const auto move = [&](float x, float y) { SendMessageW(window.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, lparam(x, y)); };
        const auto release = [&](float x, float y) { SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, lparam(x, y)); };
        const auto click = [&](composia::layout::Rect r) {
            const float x = r.x + r.width / 2, y = r.y + r.height / 2;
            press(x, y);
            release(x, y);
        };
        const auto drag = [&](composia::layout::Rect r, float dx, float dy) {
            const float x = r.x + r.width / 2, y = r.y + r.height / 2;
            press(x, y);
            move(x + dx / 2, y + dy / 2);
            move(x + dx, y + dy);
            release(x + dx, y + dy);
        };
        const auto key = [&](WPARAM virtualKey) { SendMessageW(window.hwnd(), WM_KEYDOWN, virtualKey, 0); SendMessageW(window.hwnd(), WM_KEYUP, virtualKey, 0); };
        const auto type = [&](TextField& field, std::wstring_view text) {
            SendMessageW(field.hwnd(), WM_KEYDOWN, VK_END, 0);
            for (int i = 0; i != 16; ++i) { SendMessageW(field.hwnd(), WM_KEYDOWN, VK_BACK, 0); }
            for (const wchar_t c : text) { SendMessageW(field.hwnd(), WM_CHAR, c, 0); }
        };
        const auto region = [&](BuilderDemoWindow::Hit hit, unsigned id = 0) {
            paint();
            const auto bounds = window.region(hit, id);
            require(bounds.has_value(), "Expected a hit region from the last draw");
            return *bounds;
        };
        const auto bounds = [&](unsigned id) {  // Window DIPs.
            paint();
            const auto found = window.widget_bounds(id);
            require(found.has_value(), "Expected widget bounds from the last draw");
            return *found;
        };
        const auto form_bounds = [&](unsigned id) {  // Form DIPs.
            const auto r = bounds(id);
            return composia::layout::Rect{r.x - window.form_bounds().x, r.y - window.form_bounds().y, r.width, r.height};
        };
        require(app.post([&] {
            auto& document = window.document();
            paint();
            require(window.draw_count() > 0 && !window.selected() && !window.previewing(), "Initial state is wrong");
            const auto signIn = by_name(document, L"signIn"), cancel = by_name(document, L"cancel"), panel = by_name(document, L"account"),
                email = by_name(document, L"email"), remember = by_name(document, L"remember"), session = by_name(document, L"session");

            // Selection from the form and the outline.
            click(bounds(signIn));
            require(window.selected() == signIn && window.name_field().text() == L"signIn" && window.text_field().text() == L"Sign in", "Clicking a widget did not select it");
            paint();
            require(IsWindowVisible(window.delete_button().hwnd()) && window.delete_button().enabled(), "Actions did not appear for the selection");
            click(region(BuilderDemoWindow::Hit::outline, email));
            require(window.selected() == email && window.x_field().text() == L"24" && window.width_field().text() == L"528", "Outline selection or inspector values are wrong");
            press(window.form_bounds().x + 300, window.form_bounds().y + 420);
            release(window.form_bounds().x + 300, window.form_bounds().y + 420);
            require(!window.selected() && window.width_field().text() == L"640", "Clicking empty form space did not deselect");

            // Adding from the palette by click, then by drag into the panel.
            const auto count = document.size();
            click(region(BuilderDemoWindow::Hit::palette, 2));
            require(document.size() == count + 1 && window.selected() && document.find(*window.selected())->kind == Kind::button, "Palette click did not add a button");
            require(window.status().starts_with(L"Added"), "Status did not report the addition");
            const auto added = *window.selected();
            require(document.find(added)->parent == 0, "Adding with nothing selected should target the form");
            const auto panelBounds = bounds(panel);
            const auto paletteLabel = region(BuilderDemoWindow::Hit::palette, 1);
            drag(paletteLabel, panelBounds.x + 300 - (paletteLabel.x + paletteLabel.width / 2), panelBounds.y + 100 - (paletteLabel.y + paletteLabel.height / 2));
            require(document.size() == count + 2 && window.selected() && document.find(*window.selected())->kind == Kind::label, "Palette drag did not add a label");
            const auto label = *window.selected();
            require(document.find(label)->parent == panel, "Dragged widget did not land inside the panel");
            const auto labelBounds = bounds(label);
            require(labelBounds.x > panelBounds.x && labelBounds.y > panelBounds.y && labelBounds.x + labelBounds.width < panelBounds.x + panelBounds.width,
                "Dropped label is not inside the panel");
            // The auto-placed button sits over the panel; dragging it there moves it into the panel.
            window.select(added);
            drag(bounds(added), 16, 16);
            require(document.find(added)->parent == panel && window.status().starts_with(L"Moved button"), "Dropping onto a panel did not reparent");
            window.undo();
            require(document.find(added)->parent == 0, "Undo did not restore the parent after a drop");

            // Inspector fields.
            window.select(signIn);
            type(window.text_field(), L"Continue");
            require(document.find(signIn)->text == L"Continue", "Text field did not update the widget");
            window.select(remember);
            type(window.x_field(), L"100");
            require(close_to(form_bounds(remember).x - form_bounds(panel).x, 100) && document.find(remember)->anchor(Edge::left).offset == 100, "X field did not move the widget");
            type(window.height_field(), L"48");
            require(close_to(form_bounds(remember).height, 48), "Height field did not resize the widget");
            click(region(BuilderDemoWindow::Hit::checked, remember));
            require(!document.find(remember)->checked, "Checked toggle failed");
            window.select(session);
            type(window.value_field(), L"80");
            require(document.find(session)->value == 80, "Value field did not update the slider");

            // Undo, redo, duplicate, delete, z-order, and form size.
            const auto steps = document.size();
            window.select(cancel);
            window.duplicate_button().invoke();
            require(document.size() == steps + 1 && window.selected() != cancel && window.status().starts_with(L"Duplicated"), "Duplicate failed");
            window.undo_button().invoke();
            require(document.size() == steps && window.status() == L"Undone", "Undo did not remove the duplicate");
            window.redo_button().invoke();
            require(document.size() == steps + 1 && !window.selected(), "Redo did not restore the duplicate");
            window.select(document.widgets().back().id);
            key(VK_DELETE);
            require(document.size() == steps && !window.selected(), "Delete key failed");
            window.select(cancel);
            const auto order = document.children(0);
            window.bring_forward();
            require(document.children(0) != order, "Bring forward did not change the order");
            window.send_backward();
            require(document.children(0) == order, "Send backward did not restore the order");
            window.select(std::nullopt);
            type(window.width_field(), L"720");
            paint();
            require(document.width() == 720 && close_to(window.form_bounds().width, 720), "Form width field failed");
            const auto cornerHandle = region(BuilderDemoWindow::Hit::form_corner);
            drag(cornerHandle, 16, 24);
            require(document.width() == 736 && document.height() == 464, "Dragging the form corner did not resize it");
            require(close_to(form_bounds(signIn).x + form_bounds(signIn).width, 736 - 32), "Anchored button did not keep its margin when the form grew");
            require(window.minimum_width_field().text() == L"480" && window.minimum_height_field().text() == L"360", "Minimum size fields are not synced");
            type(window.minimum_width_field(), L"700");
            require(document.minimum_width() == 700 && document.minimum_height() == 360, "Minimum width field did not apply");
            type(window.width_field(), L"600");
            require(document.width() == 700, "The width field shrank the form below its minimum");
            drag(region(BuilderDemoWindow::Hit::form_corner), -60, -60);
            require(document.width() == 700 && document.height() == 408, "Dragging the corner shrank the form below its minimum");
            type(window.minimum_height_field(), L"900");
            require(document.minimum_height() == 408, "Minimum height exceeded the form height");
            window.undo();
            require(document.minimum_height() == 360, "Undo did not restore the minimum");
            type(window.minimum_width_field(), L"160");
            require(document.minimum_width() == 160, "Minimum width could not be lowered");
            const auto emailBefore = bounds(email);
            window.select(email);
            drag(emailBefore, 0, 0);
            require(close_to(bounds(email).x, emailBefore.x) && close_to(bounds(email).y, emailBefore.y), "A click without movement moved the widget");
            drag(bounds(email), 0, 230);
            require(document.find(email)->parent == 0 && document.find(email)->anchor(Edge::top).target == Target::parent, "Dragging out of the panel did not reparent to the form");
            require(window.status().starts_with(L"Moved email into the form"), "Reparent status is wrong");
            window.undo();
            require(document.find(email)->parent == panel, "Undo did not restore the parent");

            // Save, reopen, new.
            const auto path = std::filesystem::temp_directory_path() / L"composia-builder-test.cui";
            require(window.save_to(path.wstring()) && !window.history().dirty() && window.path() == path.wstring(), "Save failed");
            const auto saved = document.to_text();
            window.new_document();
            require(document.size() == 0 && window.path().empty() && window.status() == L"New form", "New did not clear the form");
            require(!window.open_from((path.parent_path() / L"composia-builder-missing.cui").wstring()), "Opening a missing file succeeded");
            require(window.open_from(path.wstring()) && document.to_text() == saved && window.status().starts_with(L"Opened"), "Reopen did not restore the design");
            std::filesystem::remove(path);

            // Preview with real controls that follow the window size.
            window.select(signIn);
            window.preview_button().invoke();
            paint();
            require(window.previewing() && window.preview_control(signIn) && window.preview_field(email), "Preview did not create controls");
            require(IsWindowVisible(window.stop_button().hwnd()) && !IsWindowVisible(window.preview_button().hwnd()) && !IsWindowVisible(window.name_field().hwnd()),
                "Preview did not swap the toolbar and hide the inspector");
            window.preview_control(signIn)->invoke();
            require(window.status() == L"Clicked \"Continue\"", "Preview button click was not reported");
            RECT buttonRect{};
            GetWindowRect(window.preview_control(signIn)->hwnd(), &buttonRect);
            MapWindowPoints(nullptr, window.hwnd(), reinterpret_cast<POINT*>(&buttonRect), 2);
            const auto client = window.client_pixels();
            require(close_to(static_cast<float>(buttonRect.right), static_cast<float>(client.cx) - 32 * scale(), 2 * scale()), "Preview control is not anchored to the window edge");
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 0, 0, static_cast<int>(1200 * scale()), static_cast<int>(760 * scale()), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            paint();
            GetWindowRect(window.preview_control(signIn)->hwnd(), &buttonRect);
            MapWindowPoints(nullptr, window.hwnd(), reinterpret_cast<POINT*>(&buttonRect), 2);
            const auto resizedClient = window.client_pixels();
            require(resizedClient.cx != client.cx && close_to(static_cast<float>(buttonRect.right), static_cast<float>(resizedClient.cx) - 32 * scale(), 2 * scale()),
                "Preview control did not follow the resized window");
            click(region(BuilderDemoWindow::Hit::preview_checkbox, remember));
            require(window.preview_document()->find(remember)->checked && !document.find(remember)->checked, "Preview checkbox should only change the preview copy");
            const auto slider = region(BuilderDemoWindow::Hit::preview_slider, session);
            press(slider.x + 8, slider.y + slider.height / 2);
            release(slider.x + 8, slider.y + slider.height / 2);
            require(window.preview_document()->find(session)->value == 0 && document.find(session)->value == 80, "Preview slider should only change the preview copy");
            SendMessageW(window.hwnd(), WM_COMMAND, IDCANCEL, 0);
            paint();
            require(!window.previewing() && !window.preview_control(signIn) && IsWindowVisible(window.preview_button().hwnd()), "Escape did not end the preview");

            // Moving, snapping, nudging, resizing, and pins on an empty form, away from other widgets.
            window.new_document();
            click(region(BuilderDemoWindow::Hit::palette, 2));
            const auto button = *window.selected();
            require(document.find(button)->kind == Kind::button && close_to(form_bounds(button).x, 24) && close_to(form_bounds(button).y, 24), "Button was not added at the first free spot");
            drag(bounds(button), 37, 21);
            require(window.snapping() && close_to(form_bounds(button).x, 64) && close_to(form_bounds(button).y, 48), "Move did not snap to the grid");
            window.toggle_snap();
            drag(bounds(button), 5, 3);
            require(!window.snapping() && close_to(form_bounds(button).x, 69) && close_to(form_bounds(button).y, 51), "Move with snapping off should be exact");
            window.toggle_snap();
            key(VK_RIGHT);
            require(close_to(form_bounds(button).x, 70), "Arrow nudge failed");
            paint();
            drag(window.handle_bounds(2), 24, 12);
            const auto resized = form_bounds(button);
            require(close_to(resized.x, 70) && close_to(resized.y, 51) && close_to(resized.width, 122) && close_to(resized.height, 45), "Corner resize did not snap the moving edges");
            require(window.width_field().text() == L"122" && window.height_field().text() == L"45", "Inspector did not follow the resize");
            paint();
            drag(window.handle_bounds(0), -6, -3);
            require(close_to(form_bounds(button).x, 64) && close_to(form_bounds(button).y, 48) && close_to(form_bounds(button).width, 128), "Top-left resize did not keep the opposite corner");

            paint();
            click(window.pin_bounds(Edge::right));
            require(document.find(button)->anchor(Edge::right).target == Target::parent && document.find(button)->anchor(Edge::right).offset == 640 - 192,
                "Pin click did not anchor to the parent with the current margin");
            paint();
            require(window.margin_field(Edge::right).text() == L"448" && IsWindowVisible(window.margin_field(Edge::right).hwnd()) && !IsWindowVisible(window.margin_field(Edge::left).hwnd()),
                "Margin field state is wrong");
            const auto other = window.add_widget(Kind::button, 0, 300, 300);
            window.select(button);
            const auto otherBounds = bounds(other);
            const auto pin = window.pin_bounds(Edge::bottom);
            drag(pin, otherBounds.x + 40 - (pin.x + pin.width / 2), otherBounds.y + 4 - (pin.y + pin.height / 2));
            const auto& bottom = document.find(button)->anchor(Edge::bottom);
            require(bottom.target == Target::widget && bottom.id == other && bottom.edge == Edge::top && bottom.offset == 300 - 96, "Pin drag did not anchor to the sibling edge");
            require(close_to(form_bounds(button).y + form_bounds(button).height, 96), "Sibling anchor moved the widget");
            type(window.margin_field(Edge::bottom), L"20");
            require(bottom.offset == 20 && close_to(form_bounds(button).y + form_bounds(button).height, 280), "Typing a margin did not apply it");
            paint();
            click(window.pin_bounds(Edge::right));
            require(document.find(button)->anchor(Edge::right).target == Target::none && close_to(form_bounds(button).x, 64), "Pin click did not release the anchor in place");
            click(region(BuilderDemoWindow::Hit::anchor_target, static_cast<unsigned>(Edge::left)));
            require(document.find(button)->anchor(Edge::left).target == Target::parent, "Cycling the anchor target did not reach the parent");
            click(region(BuilderDemoWindow::Hit::anchor_target, static_cast<unsigned>(Edge::left)));
            require(document.find(button)->anchor(Edge::left).target == Target::widget && document.find(button)->anchor(Edge::left).id == other,
                "Cycling the anchor target did not reach the sibling");
            click(region(BuilderDemoWindow::Hit::anchor_clear, static_cast<unsigned>(Edge::left)));
            require(document.find(button)->anchor(Edge::left).target == Target::none, "Clearing the anchor failed");
            window.select(other);
            click(window.pin_bounds(Edge::top));
            drag(window.pin_bounds(Edge::top), 0, 0);
            require(document.find(other)->anchor(Edge::top).target == Target::none, "A second click should release the pin");
            paint();
            const auto pinStart = window.pin_bounds(Edge::left);
            press(pinStart.x + pinStart.width / 2, pinStart.y + pinStart.height / 2);
            move(pinStart.x - 40, pinStart.y);
            SendMessageW(window.hwnd(), WM_COMMAND, IDCANCEL, 0);
            require(document.find(other)->anchor(Edge::left).target == Target::none, "Escape did not cancel the pin drag");

            // Device replacement and shutdown.
            const auto draws = window.draw_count();
            app.graphics().recreate();
            paint();
            require(window.draw_count() > draws, "Device replacement did not repaint");
            PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
        }), "Could not schedule the editor checks");
        require(app.run() == 0, "Editor loop failed");
    }
    app.close();
}
}

// Runs a command line to completion and returns its exit code.
int run_process(const std::wstring& commandLine) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring arguments = commandLine;
    require(CreateProcessW(nullptr, arguments.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "Could not start the process");
    const wil::unique_handle thread{process.hThread}, handle{process.hProcess};
    require(WaitForSingleObject(handle.get(), 30000) == WAIT_OBJECT_0, "The process did not exit");
    DWORD code{};
    require(GetExitCodeProcess(handle.get(), &code) != FALSE, "Could not read the exit code");
    return static_cast<int>(code);
}

void app(const std::filesystem::path& player, bool warp) {
    require(std::filesystem::exists(player), "The form player executable is missing");
    const auto playerBytes = builder::read_file(player);
    require(playerBytes && playerBytes->size() > 100000, "Could not read the form player");
    const auto temp = std::filesystem::temp_directory_path();
    const auto output = temp / L"composia-built-form.exe", design = temp / L"composia-built-form.cui", second = temp / L"composia-built-second.exe";
    const auto cleanup = wil::scope_exit([&] {
        std::error_code ignored;
        for (const auto& path : {output, design, second}) { std::filesystem::remove(path, ignored); }
    });
    auto document = Document::sample();
    document.set_title(L"Built form");
    std::wstring error;
    require(builder::write_app(*playerBytes, document, output, &error), "write_app failed");
    require(std::filesystem::file_size(output) > playerBytes->size(), "The built app should be larger than the player");
    {
        const wil::unique_hmodule module{LoadLibraryExW(output.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
        require(module != nullptr, "Could not load the built app as data");
        const auto embedded = builder::read_resource(module.get(), builder::design_resource);
        require(embedded && *embedded == document.to_text(), "The embedded design differs from the document");
        require(!builder::read_resource(module.get(), builder::player_resource), "A built app should not carry a player");
    }
    require(!builder::write_app("not an executable", document, second, &error) && !error.empty() && !std::filesystem::exists(second), "Bad player bytes were accepted");
    require(builder::app_file_name(L"Sign in: v2/\"x\"") == L"Sign in_ v2__x_.exe" && builder::app_file_name(L"  ") == L"Form.exe", "App file name sanitizing is wrong");

    // The built app validates its own design; the bare player has nothing to run unless given a file.
    require(run_process(L"\"" + output.wstring() + L"\" --validate") == 0, "The built app did not validate");
    require(run_process(L"\"" + player.wstring() + L"\" --validate") != 0, "The bare player validated without a design");
    {
        std::ofstream file{design, std::ios::binary};
        const auto text = document.to_text();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    require(run_process(L"\"" + player.wstring() + L"\" --validate \"" + design.wstring() + L"\"") == 0, "The player did not validate a design file");
    require(run_process(L"\"" + player.wstring() + L"\" --validate \"" + output.wstring() + L"\"") != 0, "The player validated an executable as a design");

    composia::Application app{warp};
    const auto scale = [&](const composia::Window& window) { return static_cast<float>(window.dpi()) / 96.0f; };
    const auto right_edge = [&](const composia::Window& control, const composia::Window& parent) {
        RECT rect{};
        GetWindowRect(control.hwnd(), &rect);
        MapWindowPoints(nullptr, parent.hwnd(), reinterpret_cast<POINT*>(&rect), 2);
        return static_cast<float>(rect.right);
    };
    {
        builder::FormPlayerWindow window{app, document};
        window.show();
        const auto paint = [&] { UpdateWindow(window.hwnd()); };
        const auto click = [&](composia::layout::Rect r) {
            const auto lparam = MAKELPARAM(static_cast<int>(std::lround((r.x + 8) * scale(window))), static_cast<int>(std::lround((r.y + r.height / 2) * scale(window))));
            SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, lparam);
            SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, lparam);
        };
        const auto region = [&](builder::FormView::Interactive kind, unsigned id) {
            paint();
            for (const auto& region : window.view().regions()) { if (region.kind == kind && region.id == id) { return region.bounds; } }
            throw std::runtime_error("Expected an interactive region from the last draw");
        };
        require(app.post([&] {
            paint();
            const auto& form = window.view().document();
            const auto signIn = by_name(form, L"signIn"), email = by_name(form, L"email"), remember = by_name(form, L"remember"), session = by_name(form, L"session");
            require(window.draw_count() > 0 && window.view().button(signIn) && window.view().field(email), "The player did not create controls");
            wchar_t title[64]{};
            GetWindowTextW(window.hwnd(), title, 64);
            require(std::wstring{title} == L"Built form", "The window title should be the design title");
            const auto client = window.client_pixels();
            require(close_to(static_cast<float>(client.cx) / scale(window), 640, 2) && close_to(static_cast<float>(client.cy) / scale(window), 440, 2), "The window should be sized by the design");
            require(close_to(right_edge(*window.view().button(signIn), window), static_cast<float>(client.cx) - 32 * scale(window), 2 * scale(window)), "The button is not anchored to the window edge");
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 0, 0, static_cast<int>(960 * scale(window)), static_cast<int>(640 * scale(window)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            paint();
            const auto grown = window.client_pixels();
            require(grown.cx > client.cx && close_to(right_edge(*window.view().button(signIn), window), static_cast<float>(grown.cx) - 32 * scale(window), 2 * scale(window)),
                "The button did not follow the resized window");
            require(close_to(window.view().widget_bounds(email)->width, static_cast<float>(grown.cx) / scale(window) - 64 - 48, 2), "The field did not stretch with the panel");
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 0, 0, static_cast<int>(300 * scale(window)), static_cast<int>(200 * scale(window)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            paint();
            const auto shrunk = window.client_pixels();
            require(close_to(static_cast<float>(shrunk.cx) / scale(window), 480, 2) && close_to(static_cast<float>(shrunk.cy) / scale(window), 360, 2),
                "The window shrank below the design's minimum size");
            require(close_to(right_edge(*window.view().button(signIn), window), static_cast<float>(shrunk.cx) - 32 * scale(window), 2 * scale(window)),
                "The button did not follow the window down to the minimum");
            const bool checked = form.find(remember)->checked;
            click(region(builder::FormView::Interactive::checkbox, remember));
            require(form.find(remember)->checked != checked, "Clicking the checkbox did not toggle it");
            click(region(builder::FormView::Interactive::slider, session));
            require(form.find(session)->value == 0, "Clicking the slider start did not set it to zero");
            const auto draws = window.draw_count();
            app.graphics().recreate();
            paint();
            require(window.draw_count() > draws, "Device replacement did not repaint the player");
            PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
        }), "Could not schedule the player checks");
        require(app.run() == 0, "The player loop failed");
    }
    {
        BuilderDemoWindow window{app};
        window.show();
        builder::set_player_path(player);
        require(app.post([&] {
            UpdateWindow(window.hwnd());
            require(window.build_app(second) && std::filesystem::exists(second), "The builder did not build the app");
            require(window.status().starts_with(L"Built composia-built-second.exe") && window.built_path() == second, "The build was not reported");
            UpdateWindow(window.hwnd());
            require(IsWindowVisible(window.run_button().hwnd()), "The run button did not appear after a build");
            require(run_process(L"\"" + second.wstring() + L"\" --validate") == 0, "The builder's app did not validate");
            window.new_document();
            UpdateWindow(window.hwnd());
            require(!IsWindowVisible(window.run_button().hwnd()) && window.built_path().empty(), "The run offer outlived the next action");
            builder::set_player_path(temp / L"composia-missing-player.exe");
            require(!window.build_app(second) && window.status().starts_with(L"Could not read the form player"), "A missing player was not reported");
            PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
        }), "Could not schedule the builder checks");
        require(app.run() == 0, "The builder loop failed");
    }
    app.close();
}

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected a test name");
        const std::string_view name{argv[1]};
        const auto warp = [&](int index) { return argc > index && std::string_view{argv[index]} == "--warp"; };
        if (name == "model") { model(); }
        else if (name == "editor") { editor(warp(2)); }
        else if (name == "app") {
            require(argc >= 3, "Expected the form player path");
            app(std::filesystem::path{argv[2]}, warp(3));
        }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
