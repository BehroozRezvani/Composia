#include <composia/AnimationHelpers.hpp>

namespace composia::animations {
using namespace std::chrono_literals;

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

void bob(const composition::Visual& visual, numerics::float3 origin, float distance) {
    auto animation = visual.Compositor().CreateVector3KeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, origin);
    animation.InsertKeyFrame(0.5f, {origin.x, origin.y - distance, origin.z});
    animation.InsertKeyFrame(1.0f, origin);
    animation.Duration(2400ms);
    animation.IterationBehavior(composition::AnimationIterationBehavior::Forever);
    visual.StartAnimation(L"Offset", animation);
}

void pulse(const composition::Visual& visual) {
    auto animation = visual.Compositor().CreateScalarKeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, 0.45f);
    animation.InsertKeyFrame(0.5f, 1.0f);
    animation.InsertKeyFrame(1.0f, 0.45f);
    animation.Duration(1800ms);
    animation.IterationBehavior(composition::AnimationIterationBehavior::Forever);
    visual.StartAnimation(L"Opacity", animation);
}

void center_in_parent(const composition::Visual& visual, const composition::Visual& parent) {
    auto expression = visual.Compositor().CreateExpressionAnimation(
        L"Vector3((parent.Size.X - this.Target.Size.X) * 0.5, (parent.Size.Y - this.Target.Size.Y) * 0.5, 0)");
    expression.SetReferenceParameter(L"parent", parent);
    visual.StartAnimation(L"Offset", expression);
}

}
