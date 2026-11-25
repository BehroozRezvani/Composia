#include <composia/Controls.hpp>
#include "support/TestSupport.hpp"

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
int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"behaviour", behaviour}, {"virtualization", virtualization}, {"popup", popup},
        {"style", style}, {"scrollbar", scrollbar}});
}
