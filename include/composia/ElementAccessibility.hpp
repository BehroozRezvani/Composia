#pragma once

#include <composia/Native.hpp>
#include <UIAutomation.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace composia {
class Element;
enum class SelectionAction { select, add, remove };

// Describe a retained control on the UI thread. Control types and property values use the
// Windows UI Automation conventions. Layout-only elements need no description.
struct ElementAccessibility {
    struct Value {
        std::wstring text{};
        std::function<void(std::wstring)> set{}; // Absent for read-only controls.
    };
    struct RangeValue {
        double value{}, minimum{}, maximum{100}, smallChange{1}, largeChange{10};
        std::function<void(double)> set{};
    };
    struct Selection {
        bool multiple{}, required{};
        std::function<void()> changed{}; // After a successful select_item operation.
    };

    std::wstring name{}, automationId{}, helpText{}, localizedControlType{}, itemStatus{};
    long controlType{UIA_CustomControlTypeId};
    std::weak_ptr<Element> labeledBy{};
    std::vector<std::weak_ptr<Element>> describedBy{}, controllerFor{};
    std::function<void()> invoke{};
    std::variant<std::monostate, Value, RangeValue> value{};
    std::optional<Selection> selection{}; // Owns items under it, through layout-only wrappers and groups.
    std::optional<bool> selected{};       // Present on a selection item.
    // Optional Text pattern for a custom document with its own text layout. TextBox supplies this natively.
    std::function<wil::com_ptr<ITextProvider>()> textProvider{};
    struct Switch {
        bool on{};
        std::function<void(bool)> set{};
    };
    std::optional<Switch> toggle{}, expansion{};
    // An item's depth in a tree or an outline, from 1 for the top level; 0 for none.
    int level{};
};
}
