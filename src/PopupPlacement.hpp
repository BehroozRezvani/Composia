#pragma once

#include <composia/PopupWindow.hpp>
#include <optional>

namespace composia::detail {

// Where a popup goes, from what is known about the screen; a pure function, so it is tested
// without windows. Sizes are DIPs at `dpi`; everything else is screen pixels.
struct PopupRequest {
    std::optional<RECT> anchor;            // Below (or above) this rectangle, or else at `point`.
    POINT point{};
    PopupWindow::Below below{};
    layout::Size size{};
    float gap{};                           // DIPs between the anchor and the popup.
    RECT work{};                           // The monitor's work area.
    std::optional<RECT> ownerArea;         // Below::trailing_inside_owner stays inside this.
    UINT dpi{96};
};

// The popup's window rectangle: no larger than the area it must stay in; at a point, below and
// to the right of it, flipped left or up at the area's edges; below an anchor when it fits there
// or there is no more room above, else above it, shortened to the room on that side.
[[nodiscard]] RECT place_popup(const PopupRequest&) noexcept;

// Whether this thread's popup hooks are installed: while a popup is open, and the message hook
// alone while a popup holds closed_by_anchor_press. For tests.
[[nodiscard]] bool popup_hooks_installed(bool sentMessages) noexcept;

}
