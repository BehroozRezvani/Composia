#pragma once

#include <composia/Platform.hpp>
#include <atomic>
#include <cstdint>
#include <memory>

class DemoWindow;

class SmokeTest {
public:
    explicit SmokeTest(DemoWindow&);
    ~SmokeTest();
    void tick();

private:
    DemoWindow& window_;
    int stage_{};
    std::uint64_t generation_{};
    unsigned draws_{};
    std::shared_ptr<std::atomic_bool> animationCompleted_ = std::make_shared<std::atomic_bool>(false);
    composia::composition::CompositionScopedBatch batch_{nullptr};
    composia::composition::SpriteVisual probe_{nullptr};
    winrt::event_token completedToken_{};
};
