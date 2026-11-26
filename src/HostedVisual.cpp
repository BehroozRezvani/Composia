#include <composia/HostedVisual.hpp>
#include <composia/ElementWindow.hpp>
#include <stdexcept>

namespace composia {

HostedVisual::HostedVisual(composition::Visual visual) : visual_(std::move(visual)) {
    if (!visual_) { throw std::invalid_argument("Expected a visual"); }
    if (visual_.Parent()) { throw std::invalid_argument("The visual already has a parent"); }
    hostsVisual_ = true;
}

HostedVisual::~HostedVisual() {
    if (!frame_) { return; }
    try {
        if (const auto parent = frame_.Parent()) { parent.Children().Remove(frame_); }
        frame_.Children().RemoveAll();
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}

void HostedVisual::place(ElementWindow& host) {
    const auto box = viewport_bounds(), visible = viewport_bounds(true);
    if (visible.width <= 0 || visible.height <= 0) { hide(); return; }
    const auto& root = host.composition_target().root();
    if (!frame_) {
        const auto compositor = visual_.Compositor();
        frame_ = compositor.CreateContainerVisual();
        frame_.Comment(L"HostedVisual");
        clip_ = compositor.CreateInsetClip();
        frame_.Clip(clip_);
        frame_.Children().InsertAtTop(visual_);
    }
    // Above the tree's drawing in this window, moving from another window's tree if need be.
    if (const auto parent = frame_.Parent(); parent != root) {
        if (parent) { parent.Children().Remove(frame_); }
        root.Children().InsertAtTop(frame_);
    }
    frame_.Offset({box.x, box.y, 0});
    frame_.Size({box.width, box.height});
    clip_.LeftInset(visible.x - box.x);
    clip_.TopInset(visible.y - box.y);
    clip_.RightInset(box.x + box.width - visible.x - visible.width);
    clip_.BottomInset(box.y + box.height - visible.y - visible.height);
    frame_.IsVisible(true);
}

void HostedVisual::hide() noexcept {
    if (!frame_) { return; }
    try { frame_.IsVisible(false); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}

void HostedVisual::on_detaching() noexcept { hide(); }

}
