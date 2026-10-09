#pragma once

#include <composia/Composition.hpp>
#include <chrono>

// The demos' looping motions, which Composition runs without the UI thread.
namespace demo {

// Moves the visual up by distance and back, forever.
inline void bob(const composia::composition::Visual& visual, composia::numerics::float3 origin, float distance) {
    auto animation = visual.Compositor().CreateVector3KeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, origin);
    animation.InsertKeyFrame(0.5f, {origin.x, origin.y - distance, origin.z});
    animation.InsertKeyFrame(1.0f, origin);
    animation.Duration(std::chrono::milliseconds{2400});
    animation.IterationBehavior(composia::composition::AnimationIterationBehavior::Forever);
    visual.StartAnimation(L"Offset", animation);
}

// Fades the visual's opacity in and out, forever.
inline void pulse(const composia::composition::Visual& visual) {
    auto animation = visual.Compositor().CreateScalarKeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, 0.45f);
    animation.InsertKeyFrame(0.5f, 1.0f);
    animation.InsertKeyFrame(1.0f, 0.45f);
    animation.Duration(std::chrono::milliseconds{1800});
    animation.IterationBehavior(composia::composition::AnimationIterationBehavior::Forever);
    visual.StartAnimation(L"Opacity", animation);
}

}
