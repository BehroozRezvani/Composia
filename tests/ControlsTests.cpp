#include <composia/Controls.hpp>
#include "support/TestSupport.hpp"
#include <array>

using namespace composia;
namespace ui = composia::controls;
using testing::require;

namespace {
int behaviour(const testing::Options&) {
    ui::Observable<int> number{0};
    std::vector<int> changes;
    auto watch = number.on_change([&](int value) { changes.push_back(value); });
    number.set(2);
    number.set(2);
    watch.disconnect();
    number.set(3);
    require(changes == std::vector<int>{2}, "Observable duplicate or stale subscription");

    int clicks{};
    auto cmd = std::make_shared<ui::Command>([&] { ++clicks; });
    auto button = std::make_shared<ui::Button>(L"Apply", cmd);
    auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto toggle = std::make_shared<ui::Toggle>(L"Mute");
    auto slider = std::make_shared<ui::Slider>(L"Volume", 0, 100, 25);
    root->add(button); root->add(toggle); root->add(slider);
    ElementTree tree(root);
    tree.set_viewport({160, 100});
    using Kind = PointerEvent::Kind;
    using Mouse = PointerEvent::Button;
    tree.pointer({Kind::down, {8, 8}, Mouse::left});
    require(button->state().pressed && button->state().focused, "Press or focus missing");
    tree.pointer({Kind::move, {8, 42}});
    require(!button->state().pressed && toggle->state().hovered, "Dragging off a button");
    tree.pointer({Kind::up, {8, 42}, Mouse::left});
    require(!clicks && !toggle->checked(), "Release activated a different control");
    tree.key({VK_SPACE, true}); tree.key({VK_SPACE, false});
    require(clicks == 1, "Keyboard activation");
    cmd->enabled.set(false);
    require(button->state().disabled && !button->focused() && !cmd->execute(), "Disabled command");
    button->set_enabled(false);
    cmd->enabled.set(true);
    require(button->state().disabled, "Command availability erased a local disabled state");
    tree.pointer({Kind::down, {8, 42}, Mouse::left});
    tree.pointer({Kind::up, {8, 42}, Mouse::left});
    require(toggle->checked() && toggle->state().selected, "Toggle did not latch");
    tree.pointer({Kind::down, {80, 78}, Mouse::left});
    tree.pointer({Kind::move, {190, 10}});
    tree.pointer({Kind::up, {190, 10}, Mouse::left});
    require(slider->value() == 100 && !slider->state().pressed, "Slider drag capture");
    tree.key({VK_LEFT, true});
    require(slider->value() == 99, "Slider keyboard value");
    return 0;
}

int style(const testing::Options&) {
    Appearance dark;
    dark.dark = true;
    dark.accent = 0xffd700;
    dark.textScale = 1.5f;
    auto t = ui::StyleTokens::from(dark);
    require(t.foreground == 0xffffff && t.background == 0x2b2b2b && t.accent == 0xffd700 &&
        t.accentForeground == 0x000000 && t.fontSize == 21, "Dark appearance tokens");
    Appearance contrast;
    contrast.highContrast = true;
    contrast.colors = {.window = 0x000000, .windowText = 0xfffffe, .buttonFace = 0x000001, .buttonText = 0xffff00,
        .highlight = 0x00ffff, .highlightText = 0x000002, .grayText = 0x00ff00, .hotlight = 0x8080ff};
    t = ui::StyleTokens::from(contrast);
    require(t.background == 0x000001 && t.hover == 0x000001 && t.foreground == 0xffff00 && t.accent == 0x00ffff &&
        t.accentForeground == 0x000002 && t.disabled == 0x00ff00 && t.focus == 0xfffffe, "High contrast tokens");

    const ui::StyleTokens brand{0x111111, 0x222222, 0x333333};
    auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto scope = std::make_shared<ui::StyleScope>(brand, ElementLayout{ElementLayout::Kind::stack});
    auto inherits = std::make_shared<ui::Button>(L"Inherits", [] {});
    auto own = std::make_shared<ui::Button>(L"Own", [] {});
    own->set_tokens(ui::StyleTokens{0x444444});
    auto panel = std::make_shared<ui::ScrollView>(L"Panel");
    auto inner = std::make_shared<ui::Toggle>(L"Inner");
    panel->set_content(inner);
    panel->set_tokens(ui::StyleTokens{0x555555});
    auto outside = std::make_shared<ui::Slider>(L"Outside", 0, 1, 0);
    scope->add(inherits);
    scope->add(own);
    scope->add(panel);
    root->add(scope);
    root->add(outside);
    ElementTree tree(root);
    require(inherits->tokens() == brand && own->tokens().foreground == 0x444444 && inner->tokens().foreground == 0x555555 &&
        outside->tokens() == ui::StyleTokens{}, "Tokens were not inherited from the nearest ancestor with tokens");
    scope->set_tokens(std::nullopt);
    own->set_tokens(std::nullopt);
    require(inherits->tokens() == ui::StyleTokens{} && own->tokens() == ui::StyleTokens{} && !own->own_tokens(),
        "Clearing tokens did not inherit again");
    return 0;
}

int scrollbar(const testing::Options&) {
    using Kind = PointerEvent::Kind;
    using Mouse = PointerEvent::Button;
    auto view = std::make_shared<ui::ScrollView>(L"Messages", layout::Axis::vertical, 100);
    auto content = std::make_shared<Element>();
    content->set_preferred_size({96, 400});
    view->set_content(content);
    ElementTree tree(view);
    tree.set_viewport({96, 100});
    tree.update_layout();
    // The track spans 96 DIPs below a 2 DIP inset, and the thumb shows 100 of 400 DIPs: 24 long.
    require(view->scroll_limit() == 300 && tree.hit_test({90, 50}) == view.get() && tree.hit_test({40, 50}) == content.get(),
        "The scrollbar did not claim its edge");
    tree.pointer({Kind::down, {90, 10}, Mouse::left});
    tree.pointer({Kind::move, {90, 46}});
    tree.update_layout();
    require(view->scroll_offset() == 150, "Dragging the thumb half way did not scroll half way");
    tree.pointer({Kind::move, {90, 500}});
    tree.pointer({Kind::up, {90, 500}, Mouse::left});
    tree.update_layout();
    require(view->scroll_offset() == 300, "Dragging past the end did not stop at the limit");
    tree.pointer({Kind::down, {90, 5}, Mouse::left});
    tree.pointer({Kind::up, {90, 5}, Mouse::left});
    tree.update_layout();
    require(view->scroll_offset() == 200, "A press on the track above the thumb did not page up");
    tree.pointer({PointerEvent::Kind::wheel, {40, 50}, Mouse::none, 1});
    tree.update_layout();
    require(view->scroll_offset() < 200, "Wheel scrolling stopped working");
    view->set_scrollbar(false);
    require(tree.hit_test({90, 50}) == content.get(), "A hidden scrollbar claimed the pointer");
    view->set_scrollbar(true);
    content->set_preferred_size({96, 80});
    tree.update_layout();
    require(view->scroll_limit() == 0 && tree.hit_test({90, 50}) == content.get(), "Content that fits showed a scrollbar");
    return 0;
}

int context(const testing::Options&) {
    auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto button = std::make_shared<ui::Button>(L"Reply", [] {});
    auto list = std::make_shared<ui::List>(L"Messages", ui::Items::strings({L"A", L"B", L"C", L"D", L"E", L"F"}), 24, 96);
    root->add(button);
    root->add(list);
    ElementTree tree(root);
    tree.set_viewport({120, 128});
    require(!tree.context_menu(layout::Point{8, 8}), "A control without subscribers handled a request");
    std::vector<ContextMenuEvent> buttonMenus, listMenus;
    auto onButton = button->on_context_requested([&](const ContextMenuEvent& e) { buttonMenus.push_back(e); });
    auto onList = list->on_context_requested([&](const ContextMenuEvent& e) { listMenus.push_back(e); });
    require(tree.context_menu(layout::Point{8, 8}) && buttonMenus.size() == 1 && !buttonMenus[0].keyboard,
        "The button's subscriber missed a click's request");
    // The list starts 32 DIPs down, below the button; its fourth row spans 72 to 96 DIPs inside it.
    require(tree.context_menu(layout::Point{8, 32 + 80}) && list->selected() == 3 && listMenus.size() == 1,
        "A right click did not select the row under it");
    require(list->request_focus() && tree.context_menu(std::nullopt) && listMenus.size() == 2 &&
        listMenus[1].keyboard && listMenus[1].position.y == 96, "A keyboard request was not placed at the selected row");
    onButton.disconnect();
    require(!tree.context_menu(layout::Point{8, 8}), "A disconnected subscriber still handled requests");
    return 0;
}

int heights(const testing::Options&) {
    // A 48 DIP heading over two 24 DIP rows, ten times: each group spans 96 DIPs.
    auto items = ui::Items{30, [](std::size_t i) { return L"Row " + std::to_wstring(i); }};
    items.height = [](std::size_t i) { return i % 3 == 0 ? 48.f : 24.f; };
    auto list = std::make_shared<ui::List>(L"Folders", items, 24, 100);
    ElementTree tree(list);
    tree.set_viewport({200, 100});
    tree.update_layout();
    require(list->scroll_limit() == 860 && list->realized_count() == 4, "Rows of their own heights were not laid out");
    const auto* second = tree.hit_test({8, 60});
    require(second && second->viewport_bounds().y == 48 && second->viewport_bounds().height == 24,
        "A row did not take its own height");
    require(list->request_focus(), "List focus");
    tree.key({VK_END, true});
    tree.update_layout();
    require(list->selected() == 29 && list->scroll_offset() == 860, "End did not reveal the last row");
    tree.key({VK_HOME, true});
    tree.key({VK_NEXT, true});
    require(list->selected() == 3, "Page Down did not move a viewport's height");
    tree.key({VK_PRIOR, true});
    require(list->selected() == 0, "Page Up did not move a viewport's height");
    tree.update_layout();
    tree.context_menu(layout::Point{8, 50});
    require(list->selected() == 1, "A right click did not select the row of its own height under it");
    items.height = [](std::size_t) { return 0.f; };
    bool rejected{};
    try { list->set_items(items); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "A row without height was accepted");
    return 0;
}

int current(const testing::Options&) {
    // The headings Mail and Work take the keyboard but are never selected.
    auto items = ui::Items::strings({L"Mail", L"Inbox", L"Sent", L"Work", L"Projects", L"Archive"});
    items.selectable = [](std::size_t i) { return i != 0 && i != 3; };
    auto list = std::make_shared<ui::List>(L"Folders", items, 24, 144);
    std::vector<std::size_t> activated;
    std::vector<ContextMenuEvent> menus;
    auto onActivate = list->on_activate([&](std::size_t i) { activated.push_back(i); });
    auto onMenu = list->on_context_requested([&](const ContextMenuEvent& e) { menus.push_back(e); });
    ElementTree tree(list);
    tree.set_viewport({200, 144});
    tree.update_layout();
    require(list->request_focus(), "List focus");
    tree.key({VK_DOWN, true});
    require(list->current() == 0 && !list->selected(), "The keyboard selected a heading or passed it by");
    tree.key({VK_DOWN, true});
    tree.key({VK_DOWN, true});
    tree.key({VK_DOWN, true});
    require(list->current() == 3 && list->selected() == 2, "Moving onto a heading changed the selection");
    tree.key({VK_RETURN, true});
    require(activated == std::vector<std::size_t>{3}, "Enter did not activate the current heading");
    require(!list->select(3) && list->selected() == 2, "A heading could be selected");
    tree.update_layout();
    tree.context_menu(layout::Point{8, 10});
    require(list->current() == 0 && list->selected() == 2, "A right click on a heading selected it");
    require(tree.context_menu(std::nullopt) && menus.size() == 2 && menus[1].position.y == 24,
        "A keyboard request was not placed below the current row");
    const auto* heading = tree.hit_test({8, 10});
    const auto* folder = tree.hit_test({8, 30});
    require(heading && !heading->accessibility()->selected && folder && folder->accessibility()->selected,
        "A heading was exposed as a selection item, or a folder was not");
    tree.text_input(L"p");
    require(list->selected() == 4 && list->current() == 4, "Typing did not select the match after a heading");
    require(list->go_to(3) && list->current() == 3 && list->selected() == 4, "go_to did not rest on a heading");
    list->set_items(items);
    require(!list->current() && !list->selected(), "New items kept the current row");
    return 0;
}

int tree(const testing::Options&) {
    // Mail holds Inbox and Projects, and Projects holds Website; Projects starts folded.
    const std::vector<std::wstring> names{L"Mail", L"Inbox", L"Projects", L"Website"};
    const std::vector<std::size_t> levels{0, 1, 1, 2};
    bool projectsOpen{};
    const auto rows = [&] {
        auto shown = std::make_shared<std::vector<std::size_t>>(std::vector<std::size_t>{0, 1, 2});
        if (projectsOpen) { shown->push_back(3); }
        ui::Items items{shown->size(), [shown, &names](std::size_t i) { return names[(*shown)[i]]; }};
        items.level = [shown, &levels](std::size_t i) { return levels[(*shown)[i]]; };
        items.expanded = [shown, &projectsOpen](std::size_t i) -> std::optional<bool> {
            const auto node = (*shown)[i];
            if (node == 0) { return true; }
            if (node == 2) { return projectsOpen; }
            return std::nullopt;
        };
        return items;
    };
    auto list = std::make_shared<ui::List>(L"Folders", rows(), 24, 120);
    std::vector<std::pair<std::size_t, bool>> requests;
    auto onExpand = list->on_expand([&](std::size_t row, bool open) {
        requests.emplace_back(row, open);
        if (row != 2) { return; }
        projectsOpen = open;
        list->set_items(rows());
        list->go_to(2);
    });
    ElementTree tree(list);
    tree.set_viewport({200, 120});
    tree.update_layout();
    require(list->accessibility()->controlType == UIA_TreeControlTypeId, "A list with levels was not a tree");
    require(list->request_focus(), "List focus");
    tree.key({VK_DOWN, true});
    tree.key({VK_DOWN, true});
    tree.key({VK_DOWN, true});
    tree.key({VK_RIGHT, true});
    require(requests.size() == 1 && requests[0] == std::pair<std::size_t, bool>{2, true} && list->items().count == 4,
        "Right did not ask to open a folded row");
    tree.key({VK_RIGHT, true});
    require(list->selected() == 3, "Right on an open row did not move to its first child");
    tree.key({VK_LEFT, true});
    require(list->selected() == 2 && requests.size() == 1, "Left on a leaf did not move to its parent");
    tree.key({VK_LEFT, true});
    require(requests.size() == 2 && !requests[1].second && list->items().count == 3, "Left did not ask to fold an open row");
    tree.key({VK_LEFT, true});
    require(list->selected() == 0, "Left on a folded row did not move to its parent");
    tree.update_layout();
    const auto* projects = tree.hit_test({8, 60});
    const auto* inbox = tree.hit_test({8, 30});
    require(projects && projects->accessibility()->controlType == UIA_TreeItemControlTypeId &&
        projects->accessibility()->level == 2 && projects->accessibility()->expansion &&
        !projects->accessibility()->expansion->on && inbox && !inbox->accessibility()->expansion,
        "Tree rows did not expose their level and expansion");
    projects->accessibility()->expansion->set(true);
    require(requests.size() == 3 && requests[2] == std::pair<std::size_t, bool>{2, true},
        "Automation's Expand did not reach the owner");
    list->set_items(ui::Items::strings({L"Plain"}));
    require(list->accessibility()->controlType == UIA_ListControlTypeId, "A list without levels stayed a tree");
    return 0;
}

int replace(const testing::Options&) {
    const auto numbered = [](std::size_t count) {
        return ui::Items{count, [](std::size_t i) { return L"Row " + std::to_wstring(i); }};
    };
    auto list = std::make_shared<ui::List>(L"Folders", numbered(10), 24, 72);
    int selections{};
    auto onSelection = list->on_selection([&](ui::List::Index) { ++selections; });
    ElementTree tree(list);
    tree.set_viewport({200, 72});
    tree.update_layout();
    require(list->go_to(5), "go_to");
    tree.update_layout();
    require(list->scroll_offset() == 72 && selections == 1, "go_to did not reveal the row");
    list->replace_items(numbered(12), 4);
    tree.update_layout();
    const auto* row = tree.hit_test({8, 30});
    require(list->selected() == 4 && list->current() == 4 && list->scroll_offset() == 72 && selections == 1,
        "Replacing the rows moved the view or raised a selection change");
    require(row && row->accessibility()->selected == true, "The selected row was not exposed as selected");
    tree.pointer({PointerEvent::Kind::move, {8, 30}});
    list->replace_items(numbered(12), 4);
    tree.update_layout();
    const auto* hovered = tree.hit_test({8, 30});
    require(hovered && hovered->hovered(), "A row realized again under a still pointer was not hovered");
    require(list->request_focus(), "List focus");
    tree.key({VK_DOWN, true});
    require(list->selected() == 5, "The keyboard did not go on from the row given");
    list->replace_items(numbered(3), std::nullopt, 2);
    tree.update_layout();
    require(!list->selected() && list->current() == 2 && list->scroll_offset() == 0,
        "Fewer rows did not clamp the view or keep the current row");
    bool rejected{};
    try { list->replace_items(numbered(3), 3); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected && list->items().count == 3, "A selection past the rows was accepted");
    return 0;
}

int content(const testing::Options&) {
    // Even rows carry a button near their right end.
    std::vector<std::wstring> labels;
    for (int i = 0; i < 20; ++i) { labels.push_back(L"Row " + std::to_wstring(i)); }
    int presses{}, made{};
    ui::Items items{labels.size(), [&](std::size_t i) { return labels[i]; }};
    items.content = [&](std::size_t i) -> std::shared_ptr<Element> {
        if (i % 2) { return nullptr; }
        ++made;
        auto content = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::canvas});
        auto button = std::make_shared<ui::Button>(L"Sync " + std::to_wstring(i), [&] { ++presses; });
        button->set_preferred_size({40, 22});
        button->set_offset({150, 4});
        content->add(button);
        return content;
    };
    auto list = std::make_shared<ui::List>(L"Folders", items, 30, 90);
    ElementTree tree(list);
    tree.set_viewport({200, 90});
    tree.update_layout();
    require(made == 2, "Content was not made for the realized rows only");
    using Kind = PointerEvent::Kind;
    using Mouse = PointerEvent::Button;
    tree.pointer({Kind::down, {160, 10}, Mouse::left});
    tree.pointer({Kind::up, {160, 10}, Mouse::left});
    require(presses == 1 && !list->selected(), "A row's button did not take the press before the row");
    tree.pointer({Kind::down, {8, 10}, Mouse::left});
    tree.pointer({Kind::up, {8, 10}, Mouse::left});
    require(list->selected() == 0, "A press beside a row's button did not reach the row");
    require(list->index_at({8, 45}) == 1 && !list->index_at({8, -1}) && !list->index_at({8, 95}), "index_at");
    list->set_scroll_offset(30);
    tree.update_layout();
    require(list->index_at({8, 45}) == 2, "index_at did not follow the scroll position");
    labels[2] = L"Row 2, 5 unread";
    list->refresh(2);
    const Element* row = tree.hit_test({8, 40});
    while (row && !(row->accessibility() && row->accessibility()->name.starts_with(L"Row 2"))) { row = row->parent(); }
    require(row && row->accessibility()->name == L"Row 2, 5 unread", "refresh did not read the label again");
    return 0;
}

int popup(const testing::Options&) {
    auto content = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto dropdown = std::make_shared<ui::Dropdown>(L"Output", ui::Items::strings({L"Stereo", L"Mono", L"Surround"}), 20, 60);
    dropdown->set_selected(0);
    int actions{};
    auto action = std::make_shared<ui::Command>([&] { ++actions; });
    auto unavailable = std::make_shared<ui::Command>([] {});
    unavailable->enabled.set(false);
    auto menuItems = ui::Items::strings({L"Unavailable", L"Apply"});
    menuItems.command = [&](std::size_t i) { return i ? action : unavailable; };
    auto menu = std::make_shared<ui::Menu>(L"Actions", menuItems, 20, 60);
    content->add(dropdown); content->add(menu);
    auto host = std::make_shared<ui::PopupHost>(content);
    ElementTree tree(host);
    tree.set_viewport({180, 160});
    require(dropdown->open(), "Dropdown did not open");
    tree.update_layout();
    auto b = dropdown->popup().viewport_bounds();
    require(b.y == 32 && b.height == 60 && dropdown->focused(), "Popup placement or focus");
    tree.key({VK_DOWN, true}); tree.key({VK_ESCAPE, true});
    require(!dropdown->expanded() && dropdown->selected() == 0, "Escape committed a preview");
    tree.key({VK_F4, true}); tree.key({VK_DOWN, true}); tree.key({VK_RETURN, true});
    require(!dropdown->expanded() && dropdown->selected_text() == L"Mono", "Keyboard dropdown commit");
    tree.text_input(L"s"); tree.text_input(L"u");
    require(dropdown->selected_text() == L"Surround", "Incremental dropdown search");
    dropdown->open();
    tree.pointer({PointerEvent::Kind::down, {4, 148}, PointerEvent::Button::left});
    require(!dropdown->expanded(), "Outside click left popup open");
    dropdown->open(); menu->request_focus();
    require(!dropdown->expanded() && menu->open(), "Moving focus left two popups open");
    tree.key({VK_RETURN, true});
    require(actions == 1 && !menu->expanded() && menu->selected() == 1, "Menu command or disabled entry");
    dropdown->open();
    content->remove(*dropdown);
    tree.update_layout();
    require(host->children().size() == 1 && !dropdown->expanded(), "Detached trigger left a popup");
    return 0;
}

int virtualization(const testing::Options&) {
    std::size_t labels{};
    auto list = std::make_shared<ui::List>(L"Library", ui::Items{1000000, [&](std::size_t i) {
        ++labels;
        return L"Track " + std::to_wstring(i);
    }}, 24, 120);
    ElementTree tree(list);
    tree.set_viewport({200, 120});
    tree.update_layout();
    require(list->realized_count() == 5 && labels == 5, "List eagerly realized its source");
    require(list->request_focus(), "List focus");
    tree.key({VK_END, true});
    tree.update_layout();
    require(list->selected() == 999999 && list->scroll_offset() == list->scroll_limit() &&
            list->realized_count() == 5 && labels <= 10, "Keyboard jump scanned the list");
    list->set_scroll_offset(12);
    tree.update_layout();
    require(list->selected() == 999999 && list->realized_count() == 7, "Offscreen selection or partial row");
    const auto reads = labels;
    tree.set_viewport({240, 120});
    tree.update_layout();
    require(reads == labels, "Resizing recreated realized rows");
    list->set_items(ui::Items::strings({L"One", L"Two"}));
    tree.update_layout();
    require(!list->selected() && !list->scroll_offset() && list->realized_count() == 2, "Model reset");
    auto resetOnSelection = list->on_selection([&](ui::List::Index i) {
        if (i) { list->set_items({}); }
    });
    tree.key({VK_END, true});
    tree.update_layout();
    require(!list->selected() && !list->realized_count(), "Navigation reused a row removed by a selection observer");
    return 0;
}
}
// A menu or dropdown never chooses a row that cannot be selected, such as a heading, though the
// keyboard and the pointer can rest on it.
int choices(const testing::Options&) {
    auto items = ui::Items::strings({L"Sort by", L"Date", L"Sender"});
    items.selectable = [](std::size_t i) { return i != 0; };
    auto content = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto menu = std::make_shared<ui::Menu>(L"View", items, 20, 80);
    auto dropdown = std::make_shared<ui::Dropdown>(L"Sort", items, 20, 80);
    content->add(menu);
    content->add(dropdown);
    auto host = std::make_shared<ui::PopupHost>(content);
    ElementTree tree(host);
    tree.set_viewport({180, 200});
    std::vector<ui::List::Index> chosen;
    auto watch = menu->on_selection([&](ui::List::Index index) { chosen.push_back(index); });
    using Kind = PointerEvent::Kind;
    using Mouse = PointerEvent::Button;

    require(menu->open(), "The menu did not open");
    tree.update_layout();
    require(menu->popup().current() == 0, "The menu did not open on its heading");
    tree.key({VK_RETURN, true});
    require(menu->expanded() && chosen.empty(), "Enter on a heading chose it or closed the menu");
    // The heading spans 32 to 52, under the trigger.
    tree.pointer({Kind::down, {8, 40}, Mouse::left});
    tree.pointer({Kind::up, {8, 40}, Mouse::left});
    require(menu->expanded() && chosen.empty(), "Clicking a heading chose it or closed the menu");
    tree.key({VK_DOWN, true});
    tree.key({VK_RETURN, true});
    require(!menu->expanded() && chosen == std::vector<ui::List::Index>{1}, "Enter on a row did not choose it");
    // A menu's choice is an action: the same row again is raised again.
    require(menu->open(), "The menu did not open again");
    tree.key({VK_DOWN, true});
    tree.key({VK_RETURN, true});
    require(!menu->expanded() && chosen == std::vector<ui::List::Index>{1, 1}, "Choosing the same menu row again raised nothing");
    menu->open();
    tree.update_layout();
    tree.pointer({Kind::down, {8, 60}, Mouse::left});
    tree.pointer({Kind::up, {8, 60}, Mouse::left});
    require(!menu->expanded() && chosen == std::vector<ui::List::Index>{1, 1, 1}, "Clicking the same menu row again raised nothing");

    dropdown->set_selected(1);
    require(dropdown->open(), "The dropdown did not open");
    tree.key({VK_UP, true});
    tree.key({VK_RETURN, true});
    require(dropdown->expanded() && dropdown->selected() == 1 && dropdown->selected_text() == L"Date",
        "A dropdown committed its heading");
    tree.key({VK_ESCAPE, true});
    require(!dropdown->expanded() && dropdown->selected() == 1, "Escape after a heading changed the value");
    // A dropdown's choice is a value: choosing the row it has raises nothing.
    std::vector<ui::List::Index> sorted;
    auto watchSort = dropdown->on_selection([&](ui::List::Index index) { sorted.push_back(index); });
    require(dropdown->open(), "The dropdown did not open again");
    tree.key({VK_RETURN, true});
    require(!dropdown->expanded() && dropdown->selected() == 1 && sorted.empty(), "A dropdown raised a choice that kept its value");
    // A menu that starts with a separator opens on the row after it.
    auto led = ui::Items::strings({L"", L"Undo"});
    led.separator = [](std::size_t i) { return i == 0; };
    menu->set_items(led);
    require(menu->open() && menu->popup().current() == 1, "A menu opened on its leading separator");
    tree.key({VK_RETURN, true});
    require(!menu->expanded() && chosen.back() == 1, "Enter after a leading separator did not choose the row");
    return 0;
}

// Separator rows are lines between groups: never selected, current or chosen, passed by the
// keyboard, type-ahead and the pointer, and Separators to UI Automation.
int separators(const testing::Options&) {
    auto items = ui::Items::strings({L"Reply", L"Forward", L"Recent", L"Delete"});
    items.separator = [](std::size_t i) { return i == 2; };
    auto list = std::make_shared<ui::List>(L"Actions", items, 32, 200);
    std::size_t activations{};
    auto onActivate = list->on_activate([&](std::size_t) { ++activations; });
    ElementTree tree(list);
    tree.set_viewport({160, 200});
    tree.update_layout();
    require(list->row_bounds(2).height == ui::List::separatorHeight && list->row_bounds(3).y == 73,
        "A separator did not take its own height");
    const Element* separator{};
    for (const auto& row : list->children().front()->children().front()->children()) {
        if (row->bounds().y == 64) { separator = row.get(); }
    }
    require(separator && separator->accessibility()->controlType == UIA_SeparatorControlTypeId &&
        separator->accessibility()->name.empty() && !separator->accessibility()->invoke &&
        !separator->accessibility()->selected, "A separator row was not a plain Separator to UI Automation");
    require(list->request_focus() && list->go_to(1) && list->selected() == 1, "Could not select the row before the separator");
    tree.key({VK_DOWN, true});
    require(list->selected() == 3, "Down did not pass the separator");
    tree.key({VK_UP, true});
    require(list->selected() == 1, "Up did not pass the separator");
    require(!list->select(2) && !list->go_to(2) && list->selected() == 1, "A separator could be selected or gone to");
    list->select(0);
    tree.text_input(L"r");
    require(list->current() != 2, "Type-ahead landed on a separator");
    list->select(1);
    using Kind = PointerEvent::Kind;
    using Mouse = PointerEvent::Button;
    tree.pointer({Kind::down, {8, 68}, Mouse::left});
    tree.pointer({Kind::up, {8, 68}, Mouse::left});
    require(list->selected() == 1 && !activations, "A press on a separator selected or activated it");
    require(testing::throws<std::invalid_argument>([&] { list->replace_items(items, 2); }) &&
        testing::throws<std::invalid_argument>([&] { list->replace_items(items, std::nullopt, 2); }),
        "A separator was accepted as the selected or current row");

    // Separators at the ends: Home and End land on the rows inside them.
    auto ends = ui::Items::strings({L"", L"Mark read", L"", L"Archive", L""});
    ends.separator = [](std::size_t i) { return i % 2 == 0; };
    list->set_items(ends);
    tree.update_layout();
    tree.key({VK_HOME, true});
    require(list->current() == 1, "Home landed on a separator");
    tree.key({VK_END, true});
    require(list->current() == 3, "End landed on a separator");
    return 0;
}

// Rows tell UI Automation their place among their peers, which the realized rows alone cannot:
// in a flat list all the rows, in a tree the rows with the same parent, separators left out.
int positions(const testing::Options&) {
    auto items = ui::Items::strings({L"Reply", L"Forward", L"", L"Delete"});
    items.separator = [](std::size_t i) { return i == 2; };
    auto list = std::make_shared<ui::List>(L"Actions", items, 32, 400);
    ElementTree tree(list);
    tree.set_viewport({160, 400});
    tree.update_layout();
    const auto place = [&](std::size_t index) {
        for (const auto& row : list->children().front()->children().front()->children()) {
            if (row->bounds().y == static_cast<float>(list->row_bounds(index).y)) {
                return std::pair{row->accessibility()->positionInSet, row->accessibility()->sizeOfSet};
            }
        }
        return std::pair{-1, -1};
    };
    require(place(0) == std::pair{1, 3} && place(1) == std::pair{2, 3} && place(2) == std::pair{0, 0} &&
        place(3) == std::pair{3, 3}, "A flat list's rows misreported their place, or counted the separator");

    // Mail, its Inbox and Projects, Projects' Website, then Work.
    auto folders = ui::Items::strings({L"Mail", L"Inbox", L"Projects", L"Website", L"Work"});
    folders.level = [](std::size_t i) { return std::array<std::size_t, 5>{0, 1, 1, 2, 0}[i]; };
    list->set_items(folders);
    tree.update_layout();
    require(place(0) == std::pair{1, 2} && place(1) == std::pair{1, 2} && place(2) == std::pair{2, 2} &&
        place(3) == std::pair{1, 1} && place(4) == std::pair{2, 2}, "A tree's rows did not count their siblings");

    list->set_items(ui::Items::strings({L"One", L"Two", L"Three"}));
    tree.update_layout();
    require(place(2) == std::pair{3, 3}, "New rows kept the old count");

    // A million rows: the last knows its place without the others being read.
    std::size_t reads{};
    list->set_items({1000000, [&](std::size_t i) { ++reads; return std::to_wstring(i); }});
    tree.update_layout();
    require(list->request_focus(), "The list did not take the focus");
    tree.key({VK_END, true});
    tree.update_layout();
    require(place(999999) == std::pair{1000000, 1000000} && reads < 200, "A long list's last row misreported its place");
    return 0;
}

// A row with a check state is UI Automation's Toggle, which chooses it, and shows the state the
// owner gives it after each choice.
int checks(const testing::Options&) {
    bool wrap{};
    std::wstring replyKey = L"Ctrl+R";
    auto items = ui::Items::strings({L"Reply", L"Wrap lines"});
    items.checked = [&](std::size_t i) { return i == 1 ? std::optional<bool>{wrap} : std::nullopt; };
    items.accelerator = [&](std::size_t i) { return i == 0 ? replyKey : std::wstring{}; };
    auto list = std::make_shared<ui::List>(L"View", items, 32, 200);
    auto onActivate = list->on_activate([&](std::size_t i) { if (i == 1) { wrap = !wrap; } });
    ElementTree tree(list);
    tree.set_viewport({160, 200});
    tree.update_layout();
    const auto row = [&](std::size_t index) -> const Element& {
        for (const auto& child : list->children().front()->children().front()->children()) {
            if (child->bounds().y == list->row_bounds(index).y) { return *child; }
        }
        throw std::logic_error("The row is not realized");
    };
    require(!row(0).accessibility()->toggle && row(1).accessibility()->toggle && !row(1).accessibility()->toggle->on,
        "Rows did not report whether they can be checked");
    require(row(0).accessibility()->acceleratorKey == L"Ctrl+R" && row(1).accessibility()->acceleratorKey.empty(),
        "Rows did not report their accelerators");
    replyKey = L"Ctrl+Shift+R";
    list->refresh(0);
    require(row(0).accessibility()->acceleratorKey == L"Ctrl+Shift+R", "Refresh did not read the accelerator again");
    require(list->request_focus() && list->go_to(1), "Could not go to the checked row");
    tree.key({VK_RETURN, true});
    require(wrap && row(1).accessibility()->toggle->on, "Choosing the row did not show its new state");
    row(1).accessibility()->toggle->set(false);
    require(!wrap && !row(1).accessibility()->toggle->on, "UI Automation's Toggle did not choose the row");
    wrap = true;
    list->refresh(1);
    require(row(1).accessibility()->toggle->on, "Refresh did not read the check state again");
    // The owner may replace the rows while the choice is handled.
    auto replaced = list->on_activate([&](std::size_t) { list->set_items(ui::Items::strings({L"Other"})); });
    tree.key({VK_RETURN, true});
    require(list->items().count == 1, "Replacing the rows during a choice failed");
    return 0;
}

// A menu option that toggles in place leaves the menu open, showing its new state.
int stays_open(const testing::Options&) {
    bool wrap{};
    auto items = ui::Items::strings({L"Reply", L"Wrap lines", L"Delete"});
    items.checked = [&](std::size_t i) { return i == 1 ? std::optional<bool>{wrap} : std::nullopt; };
    items.staysOpen = [](std::size_t i) { return i == 1; };
    auto content = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto menu = std::make_shared<ui::Menu>(L"View", items, 20, 80);
    content->add(menu);
    auto host = std::make_shared<ui::PopupHost>(content);
    ElementTree tree(host);
    tree.set_viewport({180, 200});
    std::vector<std::size_t> chosen;
    auto watch = menu->on_selection([&](ui::List::Index i) {
        if (!i) { return; }
        chosen.push_back(*i);
        if (*i == 1) { wrap = !wrap; }
    });
    const auto toggled = [&] {
        for (const auto& row : menu->popup().children().front()->children().front()->children()) {
            if (row->accessibility()->name == L"Wrap lines") { return row->accessibility()->toggle && row->accessibility()->toggle->on; }
        }
        return false;
    };
    require(menu->open(), "The menu did not open");
    tree.update_layout();
    tree.key({VK_DOWN, true});
    tree.key({VK_RETURN, true});
    require(menu->expanded() && wrap && toggled(), "Choosing an option closed the menu or did not show its state");
    tree.key({VK_RETURN, true});
    require(menu->expanded() && !wrap && !toggled() && chosen == std::vector<std::size_t>{1, 1}, "Choosing the option again did not toggle it back");
    tree.key({VK_DOWN, true});
    tree.key({VK_RETURN, true});
    require(!menu->expanded() && chosen.back() == 2, "Choosing a command left the menu open");
    return 0;
}

// A list can be a menu to UI Automation on its own, as in an application's popup menu; a
// dropdown's popup is a list.
int roles(const testing::Options&) {
    auto items = ui::Items::strings({L"Inbox", L"Projects", L"Website"});
    items.level = [](std::size_t i) { return i == 2 ? std::size_t{1} : std::size_t{0}; };
    items.expanded = [](std::size_t i) { return i == 1 ? std::optional<bool>{true} : std::nullopt; };
    auto menu = std::make_shared<ui::List>(L"Move to", items, 32, 120, ui::ListRole::menu);
    ElementTree tree(menu);
    tree.set_viewport({160, 120});
    tree.update_layout();
    require(menu->role() == ui::ListRole::menu && menu->accessibility()->controlType == UIA_MenuControlTypeId,
        "A menu list was not a Menu");
    const auto& rows = menu->children().front()->children().front()->children();
    require(rows.size() == 3 && std::ranges::all_of(rows, [](const auto& row) {
        return row->accessibility()->controlType == UIA_MenuItemControlTypeId && !row->accessibility()->expansion;
    }), "A menu list's rows were not plain MenuItems");
    require(rows[2]->accessibility()->level == 2, "A menu list's rows lost their levels");
    auto plain = std::make_shared<ui::List>(L"Folders", items);
    require(plain->role() == ui::ListRole::list && plain->accessibility()->controlType == UIA_TreeControlTypeId,
        "A list with levels was not a Tree");
    auto dropdown = std::make_shared<ui::Dropdown>(L"Sort", ui::Items::strings({L"Date"}));
    require(dropdown->popup().role() == ui::ListRole::list && dropdown->popup().accessibility()->controlType == UIA_ListControlTypeId,
        "A dropdown's popup was not a List");
    return 0;
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"behaviour", behaviour}, {"virtualization", virtualization}, {"popup", popup}, {"choices", choices},
        {"separators", separators}, {"positions", positions}, {"checks", checks}, {"stays-open", stays_open}, {"roles", roles},
        {"style", style}, {"scrollbar", scrollbar}, {"context", context}, {"heights", heights},
        {"current", current}, {"tree", tree}, {"replace", replace},
        {"content", content}});
}
