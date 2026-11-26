#pragma once

#include <composia/Composition.hpp>
#include <composia/Element.hpp>

namespace composia {

class ElementWindow;

// A place in a retained tree for Composition content that the application builds and animates
// itself, such as a progress bar, an indicator that glides between rows, or a picture of part
// of the tree during a transition. The tree keeps the visual over the element's bounds in the
// ElementWindow that hosts it, above the tree's own drawing, and ancestors clip it. It is hidden
// while the element is clipped or scrolled out of view, while it is detached, and while the tree
// has no window. The visual's parent takes the element's size, so a RelativeSizeAdjustment of
// {1, 1} fills it. Animations on the visual run in the compositor, as anywhere else.
//
// The element takes no pointer input: presses, hover and the wheel reach the elements under it.
class HostedVisual final : public Element {
public:
    // The visual must not have a parent; it becomes a child of the element's own container.
    explicit HostedVisual(composition::Visual visual);
    ~HostedVisual() override;

    [[nodiscard]] const composition::Visual& visual() const noexcept { return visual_; }

private:
    friend class ElementTree;
    void place(ElementWindow& host);
    void hide() noexcept;
    void on_detaching() noexcept override;

    composition::Visual visual_;
    composition::ContainerVisual frame_{nullptr};
    composition::InsetClip clip_{nullptr};
};

}
