#include <composia/AnimationHelpers.hpp>

namespace composia::animations {

composition::ContainerVisual container(const composition::Compositor& compositor, numerics::float2 size) {
    auto visual = compositor.CreateContainerVisual();
    visual.Size(size);
    return visual;
}

composition::SpriteVisual sprite(const composition::Compositor& compositor, numerics::float2 size,
    winrt::Windows::UI::Color color) {
    auto visual = compositor.CreateSpriteVisual();
    visual.Size(size);
    visual.Brush(compositor.CreateColorBrush(color));
    return visual;
}

void implicit_offset(const composition::Visual& visual, std::chrono::milliseconds duration) {
    auto compositor = visual.Compositor();
    auto animation = compositor.CreateVector3KeyFrameAnimation();
    animation.Target(L"Offset");
    animation.InsertExpressionKeyFrame(1.0f, L"this.FinalValue");
    animation.Duration(duration);
    auto implicit = compositor.CreateImplicitAnimationCollection();
    implicit.Insert(L"Offset", animation);
    visual.ImplicitAnimations(implicit);
}

void center_in_parent(const composition::Visual& visual, const composition::Visual& parent) {
    auto expression = visual.Compositor().CreateExpressionAnimation(
        L"Vector3((parent.Size.X - this.Target.Size.X) * 0.5, (parent.Size.Y - this.Target.Size.Y) * 0.5, 0)");
    expression.SetReferenceParameter(L"parent", parent);
    visual.StartAnimation(L"Offset", expression);
}

}
