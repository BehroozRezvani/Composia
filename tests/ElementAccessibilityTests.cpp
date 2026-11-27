#include <composia/ElementAccessibility.hpp>
#include <composia/ElementWindow.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextBox.hpp>
#include <composia/TextLayout.hpp>
#include "support/TestSupport.hpp"
#include <thread>

using namespace composia;
using testing::require;

namespace {
class Row : public Element {
public:
    Row(Application& app, std::wstring name, long role, std::wstring id)
        : label(app.graphics().text_factory().get(), name, 16) {
        set_accessibility({.name = std::move(name), .automationId = std::move(id), .controlType = role});
        set_preferred_size({160, 34});
    }
    void paint(ScopedSurfaceDraw& draw) override {
        const auto b = bounds();
        label.resize(b.width, b.height);
        draw.context()->FillRectangle({0, 0, b.width, b.height}, draw.solid_brush(focused() ? 0x287078 : 0x243348));
        if (auto* range = std::get_if<ElementAccessibility::RangeValue>(&accessibility()->value)) {
            draw.context()->FillRectangle({b.width / 2, 10, b.width - 12, 16}, draw.solid_brush(0x526070));
            const float end = b.width / 2 + float(range->value / 100) * (b.width / 2 - 12);
            draw.context()->FillRectangle({b.width / 2, 10, end, 16}, draw.solid_brush(0x99e3ce));
        }
        if (accessibility()->selected.value_or(false)) {
            draw.context()->FillRectangle({0, 0, 4, b.height}, draw.solid_brush(0x99e3ce));
        }
        draw.context()->DrawTextLayout({8, 5}, label.layout().get(), draw.solid_brush(0xf4f6fa));
    }

private:
    TextLayout label;
};
struct Controls {
    Application app;
    std::shared_ptr<Element> root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::flex});
    std::shared_ptr<Element> wrapper = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    std::shared_ptr<Element> list = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::scroll});
    std::shared_ptr<Element> rows = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    std::shared_ptr<Row> heading, slider, button, last;
    std::shared_ptr<TextBox> editor;
    std::unique_ptr<ElementWindow> window;
    unsigned calls{};
    explicit Controls(bool warp) : app(warp) {
        root->set_accessibility(
            {.name = L"Preferences", .automationId = L"preferences", .controlType = UIA_GroupControlTypeId});
        heading = std::make_shared<Row>(app, L"Sound level", UIA_TextControlTypeId, L"label");
        slider = std::make_shared<Row>(app, L"Volume", UIA_SliderControlTypeId, L"volume");
        auto s = *slider->accessibility();
        s.labeledBy = heading;
        s.describedBy = {heading};
        s.value = ElementAccessibility::RangeValue{
            25, 0, 100, 1, 10, [this](double level) {
            slider->set_accessible_value(level); }};
        slider->set_accessibility(std::move(s));
        slider->set_focusable(true);
        root->add(heading);
        wrapper->add(slider);
        root->add(wrapper);
        list->set_accessibility({.name = L"Playback devices",
                                 .automationId = L"devices",
                                 .controlType = UIA_ListControlTypeId,
                                 .selection = ElementAccessibility::Selection{false, true, {}}});
        list->set_preferred_size({160, 72});
        list->add(rows);
        root->add(list);
        for (int i = 0; i < 7; ++i) {
            last = std::make_shared<Row>(app, L"Device " + std::to_wstring(i + 1), UIA_ListItemControlTypeId,
                                         L"device" + std::to_wstring(i + 1));
            auto item = *last->accessibility();
            item.selected = (i == 0);
            item.level = 1 + i % 2;
            item.positionInSet = i + 1;
            item.sizeOfSet = 7;
            item.value = ElementAccessibility::Value{L"Stereo", {}};
            last->set_accessibility(std::move(item));
            last->set_focusable(true);
            rows->add(last);
        }
        auto divider = std::make_shared<Row>(app, L"", UIA_SeparatorControlTypeId, L"divider");
        divider->set_preferred_size({160, 9});
        rows->add(divider);
        editor = std::make_shared<TextBox>(TextBox::Options{.name = L"Notes"}, L"Surround sound");
        root->add(editor);
        button = std::make_shared<Row>(app, L"Apply", UIA_ButtonControlTypeId, L"apply");
        auto a = *button->accessibility();
        a.invoke = [this] { ++calls; };
        a.controllerFor = {slider};
        a.acceleratorKey = L"Ctrl+Enter";
        button->set_accessibility(std::move(a));
        button->set_focusable(true);
        button->set_tooltip(L"Apply the device settings");
        root->add(button);
        window = std::make_unique<ElementWindow>(app, L"Composia accessibility test", 360, 310, root);
        window->show(SW_SHOWNOACTIVATE);
        window->elements().update_layout();
        UpdateWindow(window->hwnd());
        window->focus_element(*slider);
    }
};
using Node = wil::com_ptr<IUIAutomationElement>;
struct PropertyChanges : winrt::implements<PropertyChanges, IUIAutomationPropertyChangedEventHandler> {
    wil::unique_event rangeChanged, editorRenamed;
    PropertyChanges() {
        rangeChanged.create();
        editorRenamed.create();
    }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID id,
                                                         VARIANT value) noexcept override {
        if (id == UIA_RangeValueValuePropertyId && value.vt == VT_R8 && value.dblVal == 62) {
            SetEvent(rangeChanged.get());
        } else if (id == UIA_NamePropertyId && value.vt == VT_BSTR && value.bstrVal &&
                   std::wstring_view(value.bstrVal) == L"Listening notes") {
            SetEvent(editorRenamed.get());
        }
        return S_OK;
    }
};
Node find(IUIAutomation* client, IUIAutomationElement* parent, PROPERTYID id, const wchar_t* text) {
    wil::unique_bstr b{SysAllocString(text)};
    VARIANT v{};
    v.vt = VT_BSTR;
    v.bstrVal = b.get();
    wil::com_ptr<IUIAutomationCondition> condition;
    THROW_IF_FAILED(client->CreatePropertyCondition(id, v, condition.put()));
    Node found;
    THROW_IF_FAILED(parent->FindFirst(TreeScope_Descendants, condition.get(), found.put()));
    require(!!found, "Accessible child missing");
    return found;
}
void same(IUIAutomation* client, IUIAutomationElement* a, IUIAutomationElement* b, const char* why) {
    BOOL equal{};
    THROW_IF_FAILED(client->CompareElements(a, b, &equal));
    require(equal, why);
}
template <class T> wil::com_ptr<T> pattern(IUIAutomationElement* node, int id) {
    wil::com_ptr<T> result;
    THROW_IF_FAILED(node->GetCurrentPatternAs(id, IID_PPV_ARGS(result.put())));
    require(!!result, "Missing UIA pattern");
    return result;
}
int inspect(const testing::Options& options) {
    Controls f(options.warp);
    const auto hwnd = f.window->hwnd();
    std::exception_ptr error;
    f.window->invalidate();
    UpdateWindow(hwnd);
    const auto b = f.slider->viewport_bounds();
    const auto scale = f.window->scale();
    const auto pixel = testing::surface_pixel(f.app, f.window->composition_target().surface(),
                                              LONG((b.x + b.width - 5) * scale), LONG((b.y + 5) * scale));
    require(testing::matches(pixel, 0x287078), "Keyboard focus was missing from the painted slider");
    std::jthread inspector([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto uninit = wil::scope_exit([] { winrt::uninit_apartment(); });
            wil::com_ptr<IUIAutomation> client;
            THROW_IF_FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(client.put())));
            Node pane;
            THROW_IF_FAILED(client->ElementFromHandle(hwnd, pane.put()));
            auto byId = [&](const wchar_t* id) {
                return find(client.get(), pane.get(), UIA_AutomationIdPropertyId, id);
            };
            auto group = byId(L"preferences"), slider = byId(L"volume"), list = byId(L"devices"),
                 item = byId(L"device7"), button = byId(L"apply");
            auto editor = find(client.get(), group.get(), UIA_NamePropertyId, L"Notes");
            wil::com_ptr<IUIAutomationTreeWalker> walker;
            THROW_IF_FAILED(client->get_ControlViewWalker(walker.put()));
            Node parent;
            THROW_IF_FAILED(walker->GetParentElement(slider.get(), parent.put()));
            same(client.get(), parent.get(), group.get(), "Layout-only wrapper exposed");
            THROW_IF_FAILED(walker->GetParentElement(editor.get(), parent.put()));
            same(client.get(), parent.get(), group.get(), "Rich Edit parent mismatch");
            Node sibling;
            THROW_IF_FAILED(walker->GetPreviousSiblingElement(editor.get(), sibling.put()));
            same(client.get(), sibling.get(), list.get(), "Mixed native/custom siblings");
            Node label;
            THROW_IF_FAILED(slider->get_CurrentLabeledBy(label.put()));
            same(client.get(), label.get(), byId(L"label").get(), "Label relation");
            wil::unique_bstr help;
            THROW_IF_FAILED(button->get_CurrentHelpText(help.put()));
            require(help && std::wstring_view{help.get()} == L"Apply the device settings",
                "The tooltip was not the help text of an element without one");
            wil::com_ptr<IUIAutomationElementArray> targets;
            THROW_IF_FAILED(button->get_CurrentControllerFor(targets.put()));
            Node target;
            THROW_IF_FAILED(targets->GetElement(0, target.put()));
            same(client.get(), target.get(), slider.get(), "Controller relation");
            auto range = pattern<IUIAutomationRangeValuePattern>(slider.get(), UIA_RangeValuePatternId);
            auto listener = winrt::make_self<PropertyChanges>();
            PROPERTYID properties[] = {UIA_RangeValueValuePropertyId, UIA_NamePropertyId};
            THROW_IF_FAILED(client->AddPropertyChangedEventHandlerNativeArray(
                group.get(), TreeScope_Subtree, nullptr, listener.get(), properties, 2));
            auto unsubscribe =
                wil::scope_exit([&] { client->RemovePropertyChangedEventHandler(group.get(), listener.get()); });
            SendMessageW(hwnd, WM_KEYDOWN, VK_RIGHT, 1);
            double current{};
            THROW_IF_FAILED(range->get_CurrentValue(&current));
            require(current == 26, "Keyboard range action");
            THROW_IF_FAILED(range->SetValue(62));
            THROW_IF_FAILED(range->get_CurrentValue(&current));
            require(current == 62 && FAILED(range->SetValue(101)), "Range write or bounds check");
            require(WaitForSingleObject(listener->rangeChanged.get(), 5000) == WAIT_OBJECT_0,
                    "Missing range property event");
            auto scroll = pattern<IUIAutomationScrollPattern>(list.get(), UIA_ScrollPatternId);
            THROW_IF_FAILED(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100));
            THROW_IF_FAILED(scroll->get_CurrentVerticalScrollPercent(&current));
            require(current == 100, "Scrolling did not use arranged extent");
            auto selection = pattern<IUIAutomationSelectionItemPattern>(item.get(), UIA_SelectionItemPatternId);
            auto value = pattern<IUIAutomationValuePattern>(item.get(), UIA_ValuePatternId);
            wil::unique_bstr mode;
            BOOL readOnly{};
            THROW_IF_FAILED(value->get_CurrentValue(mode.put()));
            THROW_IF_FAILED(value->get_CurrentIsReadOnly(&readOnly));
            wil::unique_bstr mono{SysAllocString(L"Mono")};
            require(std::wstring_view(mode.get()) == L"Stereo" && readOnly && FAILED(value->SetValue(mono.get())),
                    "Read-only item value");
            VARIANT level{};
            THROW_IF_FAILED(item->GetCurrentPropertyValue(UIA_LevelPropertyId, &level));
            require(level.vt == VT_I4 && level.lVal == 1, "An item's level was not exposed");
            VARIANT position{}, size{};
            THROW_IF_FAILED(item->GetCurrentPropertyValue(UIA_PositionInSetPropertyId, &position));
            THROW_IF_FAILED(item->GetCurrentPropertyValue(UIA_SizeOfSetPropertyId, &size));
            require(position.vt == VT_I4 && position.lVal == 7 && size.vt == VT_I4 && size.lVal == 7,
                    "An item's place in its set was not exposed");
            wil::unique_bstr accelerator;
            THROW_IF_FAILED(button->get_CurrentAcceleratorKey(accelerator.put()));
            require(accelerator && std::wstring_view(accelerator.get()) == L"Ctrl+Enter", "An accelerator key was not exposed");
            auto divider = byId(L"divider");
            BOOL content{TRUE}, control{FALSE};
            THROW_IF_FAILED(divider->get_CurrentIsContentElement(&content));
            THROW_IF_FAILED(divider->get_CurrentIsControlElement(&control));
            require(!content && control, "A separator was reported as content, or not as a control");
            THROW_IF_FAILED(selection->Select());
            require(FAILED(selection->RemoveFromSelection()), "Required selection was cleared");
            auto container = pattern<IUIAutomationSelectionPattern>(list.get(), UIA_SelectionPatternId);
            wil::com_ptr<IUIAutomationElementArray> selected;
            THROW_IF_FAILED(container->GetCurrentSelection(selected.put()));
            Node choice;
            THROW_IF_FAILED(selected->GetElement(0, choice.put()));
            same(client.get(), choice.get(), item.get(), "Selected option mismatch");
            THROW_IF_FAILED(item->SetFocus());
            BOOL focused{};
            THROW_IF_FAILED(item->get_CurrentHasKeyboardFocus(&focused));
            require(focused, "UIA logical focus");
            SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 1);
            THROW_IF_FAILED(editor->get_CurrentHasKeyboardFocus(&focused));
            require(focused, "Tab to native text fragment");
            auto text = pattern<IUIAutomationTextPattern>(editor.get(), UIA_TextPatternId);
            wil::com_ptr<IUIAutomationTextRange> doc;
            THROW_IF_FAILED(text->get_DocumentRange(doc.put()));
            wil::unique_bstr contents;
            THROW_IF_FAILED(doc->GetText(-1, contents.put()));
            require(std::wstring_view(contents.get()).starts_with(L"Surround sound"), "Text range");
            THROW_IF_FAILED(pattern<IUIAutomationInvokePattern>(button.get(), UIA_InvokePatternId)->Invoke());
            // Order this mutation after the queued Invoke and before the next UIA read.
            wil::unique_event done;
            done.create();
            std::exception_ptr geometryError;
            require(f.app.post([&] {
                const auto signal = wil::scope_exit([&] { done.SetEvent(); });
                try {
                    wil::com_ptr<IRawElementProviderFragmentRoot> provider;
                    THROW_IF_FAILED(f.window->automation_provider()->QueryInterface(IID_PPV_ARGS(provider.put())));
                    auto rect = f.slider->viewport_bounds(true);
                    POINT origin{};
                    ClientToScreen(hwnd, &origin);
                    wil::com_ptr<IRawElementProviderFragment> hit;
                    THROW_IF_FAILED(provider->ElementProviderFromPoint(origin.x + (rect.x + 2) * f.window->scale(),
                                                                       origin.y + (rect.y + 2) * f.window->scale(),
                                                                       hit.put()));
                    VARIANT id{};
                    const auto clear = wil::scope_exit([&] { VariantClear(&id); });
                    THROW_IF_FAILED(
                        hit.query<IRawElementProviderSimple>()->GetPropertyValue(UIA_AutomationIdPropertyId, &id));
                    require(id.vt == VT_BSTR && std::wstring_view(id.bstrVal) == L"volume",
                            "Screen hit returned wrong fragment");
                    require(f.window->focus_element(*f.editor), "Could not focus the editor before deactivation");
                    SetFocus(nullptr);
                    require(!f.window->focused() && !f.window->elements().focused(),
                            "Focus loss left a stale tab stop");
                    f.window->focus();
                    require(f.editor->focused(), "Returning HWND focus lost the last retained tab stop");
                    f.wrapper->set_enabled(false);
                    f.rows->remove(*f.last);
                    f.editor->set_accessibility({.name = L"Listening notes"});
                } catch (...) { geometryError = std::current_exception(); }
            }),
                    "Mutation scheduling");
            require(WaitForSingleObject(done.get(), 5000) == WAIT_OBJECT_0, "Mutation timeout");
            if (geometryError) {
                std::rethrow_exception(geometryError);
            }
            require(WaitForSingleObject(listener->editorRenamed.get(), 5000) == WAIT_OBJECT_0,
                    "Missing name change on the native text fragment");
            BOOL enabled{};
            THROW_IF_FAILED(slider->get_CurrentIsEnabled(&enabled));
            require(!enabled && FAILED(range->SetValue(50)), "Disabled ancestor accepted an action");
            require(FAILED(selection->Select()), "Detached pattern still usable");
        } catch (...) { error = std::current_exception(); }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    });
    f.app.run();
    inspector.join();
    if (error) {
        std::rethrow_exception(error);
    }
    require(f.calls == 1, "Invoke did not execute on application thread");
    return 0;
}
int fixture(const testing::Options& options) {
    Controls f(options.warp);
    std::cout << reinterpret_cast<UINT_PTR>(f.window->hwnd()) << std::endl;
    return f.app.run();
}
int selection_model(const testing::Options&) {
    auto group = std::make_shared<Element>(), nested = std::make_shared<Element>();
    unsigned changes{};
    group->set_accessibility({.selection = ElementAccessibility::Selection{true, true, [&] { ++changes; }}});
    nested->set_accessibility({.selection = ElementAccessibility::Selection{false, false, {}}});
    auto item = [] {
        auto e = std::make_shared<Element>();
        e->set_accessibility({.selected = false});
        return e;
    };
    auto a = item(), b = item(), c = item();
    group->add(a);
    group->add(b);
    group->add(nested);
    nested->add(c);
    require(a->select_item(SelectionAction::select) && b->select_item(SelectionAction::add) &&
                *a->accessibility()->selected && *b->accessibility()->selected,
            "Multi-selection");
    c->select_item(SelectionAction::select);
    require(a->select_item(SelectionAction::remove) && !b->select_item(SelectionAction::remove) &&
                *c->accessibility()->selected && changes == 2,
            "Selection group isolation or required selection");
    require(a->select_item(SelectionAction::select) && !*b->accessibility()->selected, "Exclusive selection");
    group->remove(*a);
    require(*b->accessibility()->selected, "Removing the selected item left a required group empty");
    return 0;
}
}
// A relationship can name an element in another window, as a field names its popup's list.
int relations(const testing::Options& options) {
    Application app{options.warp};
    auto trigger = std::make_shared<Row>(app, L"Trigger", UIA_ComboBoxControlTypeId, L"trigger");
    auto choices = std::make_shared<Row>(app, L"Choices", UIA_ListControlTypeId, L"choices");
    trigger->set_focusable(true);
    auto described = *trigger->accessibility();
    described.controllerFor = {choices};
    described.labeledBy = choices;
    trigger->set_accessibility(std::move(described));
    ElementWindow first{app, L"First", 200, 100, trigger};
    auto second = std::make_unique<ElementWindow>(app, L"Second", 200, 100, choices);
    first.show(SW_SHOWNOACTIVATE);
    second->show(SW_SHOWNOACTIVATE);
    const auto hwnd = first.hwnd();
    std::exception_ptr error;
    wil::unique_event destroyed{wil::EventOptions::ManualReset};
    std::jthread inspector([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto uninit = wil::scope_exit([] { winrt::uninit_apartment(); });
            wil::com_ptr<IUIAutomation> client;
            THROW_IF_FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(client.put())));
            Node pane;
            THROW_IF_FAILED(client->ElementFromHandle(hwnd, pane.put()));
            auto node = find(client.get(), pane.get(), UIA_AutomationIdPropertyId, L"trigger");
            wil::com_ptr<IUIAutomationElementArray> controlled;
            THROW_IF_FAILED(node->get_CurrentControllerFor(controlled.put()));
            int count{};
            if (controlled) { THROW_IF_FAILED(controlled->get_Length(&count)); }
            require(count == 1, "A relationship to another window's element was dropped");
            Node target;
            THROW_IF_FAILED(controlled->GetElement(0, target.put()));
            wil::unique_bstr name;
            THROW_IF_FAILED(target->get_CurrentName(name.put()));
            require(name && std::wstring_view(name.get()) == L"Choices", "The relationship named the wrong element");
            Node label;
            THROW_IF_FAILED(node->get_CurrentLabeledBy(label.put()));
            require(!!label, "A label in another window was dropped");
            app.post([&] {
                second.reset();
                destroyed.SetEvent();
            });
            require(WaitForSingleObject(destroyed.get(), 5000) == WAIT_OBJECT_0, "The second window was not destroyed");
            controlled.reset();
            THROW_IF_FAILED(node->get_CurrentControllerFor(controlled.put()));
            count = 0;
            if (controlled) { THROW_IF_FAILED(controlled->get_Length(&count)); }
            require(count == 0, "A relationship outlived the window of its element");
        } catch (...) { error = std::current_exception(); }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    });
    app.run();
    inspector.join();
    if (error) { std::rethrow_exception(error); }
    return 0;
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"inspect", inspect}, {"selection", selection_model}, {"fixture", fixture}, {"relations", relations}});
}
