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
    return 0;
}
}
int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"behaviour", behaviour}, {"virtualization", virtualization}});
}
