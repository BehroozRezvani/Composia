#pragma once

#include <composia/Layout.hpp>
#include <cstdint>

namespace composia {

// Input as retained elements and key previews receive it. Positions are in DIPs.
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

// A request for a context menu: a right click, Shift+F10, or the menu key.
struct ContextMenuEvent {
    // Local to the current recipient. The pointer for a click; for the keyboard, the lower-left
    // corner of the focused element's visible part, where a menu can open below it.
    layout::Point position{};
    bool keyboard{};
};

struct KeyEvent {
    unsigned key{};                           // Windows virtual-key code.
    bool down{}, shift{}, control{}, alt{};
    std::intptr_t keyData{};                   // Win32 repeat count, scan code and transition flags.
};

}
