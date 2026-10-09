#include <composia/AnimationHelpers.hpp>
#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/ScreenCapture.hpp>
#include "support/TestSupport.hpp"
#include <chrono>
#include <iostream>

// The animation helpers, through the visuals they build and the composited pixels Composition
// produces from them, read back through a capture of the window's visual tree.
using namespace composia;
using testing::require;
using namespace std::chrono_literals;

namespace {
constexpr winrt::Windows::UI::Color red{255, 255, 0, 0}, green{255, 0, 255, 0};

class Stage final : public Window {
public:
    explicit Stage(Application& app) : Window(app, L"Animation helpers", 420, 300), target_(app.compositor(), app.graphics(), hwnd()) {}
    CompositionWindowTarget& target() noexcept { return target_; }
    std::function<void()> timer;

private:
    void on_resize() override { invalidate(); }
    void on_paint() override {
        (void)target_.render(*this, [](ScopedSurfaceDraw& draw, numerics::float2) { draw.context()->Clear(D2D1::ColorF(0x000000)); });
    }
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
        if (message == WM_TIMER && timer) { timer(); return 0; }
        return std::nullopt;
    }
    CompositionWindowTarget target_;
};

int helpers(const testing::Options& options) {
    if (!ScreenCapture::supported()) {
        std::cout << "skipped: Windows Graphics Capture is unavailable\n";
        return testing::skipped;
    }
    Application app{options.warp};
    {
        const auto compositor = app.compositor();
        Stage stage{app};
        auto& target = stage.target();

        // What each helper builds.
        auto box = animations::container(compositor, {200, 120});
        require(box.Size() == numerics::float2(200, 120) && box.Children().Count() == 0, "container() did not build an empty visual of the size");
        auto centered = animations::sprite(compositor, {40, 40}, red);
        require(centered.Size() == numerics::float2(40, 40) && centered.Brush().as<composition::CompositionColorBrush>().Color() == red,
            "sprite() did not build a visual of the size and color");
        auto moving = animations::sprite(compositor, {30, 30}, green);
        animations::implicit_offset(moving, 1500ms);
        const auto implicit = moving.ImplicitAnimations();
        require(implicit && implicit.HasKey(L"Offset") && implicit.Lookup(L"Offset").as<composition::KeyFrameAnimation>().Duration() == 1500ms,
            "implicit_offset() did not install an Offset animation of the duration");
        auto defaulted = animations::sprite(compositor, {1, 1}, green);
        animations::implicit_offset(defaulted);
        require(defaulted.ImplicitAnimations().Lookup(L"Offset").as<composition::KeyFrameAnimation>().Duration() == 300ms,
            "implicit_offset() does not default to 300 ms");

        // The tree: a box with a sprite kept centered in it, and a sprite that moves.
        box.Offset({20, 20, 0});
        target.root().Children().InsertAtTop(box);
        box.Children().InsertAtTop(centered);
        animations::center_in_parent(centered, box);
        moving.Offset({300, 20, 0});
        target.root().Children().InsertAtTop(moving);
        stage.show(SW_SHOWNOACTIVATE);
        UpdateWindow(stage.hwnd());

        ScreenCapture capture;
        capture.start(capture::GraphicsCaptureItem::CreateFromVisual(target.root()), app.graphics().d3d_device().get());
        int step{}, polls{};
        bool sawBetween{};
        stage.timer = [&] {
            require(++polls < 400, "The captured frames never showed the expected visuals");
            auto frame = capture.next_frame();
            if (!frame) { return; }
            const auto close = wil::scope_exit([&] { frame.Close(); });
            const auto size = frame.ContentSize();
            const auto logical = target.logical_size();
            const auto at = [&](float x, float y) {
                return testing::frame_pixel(app, frame, static_cast<UINT>(x / logical.x * static_cast<float>(size.Width)),
                    static_cast<UINT>(y / logical.y * static_cast<float>(size.Height)));
            };
            const auto is = [](testing::Pixel pixel, winrt::Windows::UI::Color color) {
                return testing::matches(pixel, (static_cast<std::uint32_t>(color.R) << 16) | (color.G << 8) | color.B);
            };
            const bool movingAtStart = is(at(315, 35), green), movingAtEnd = is(at(315, 175), green);
            if (step == 0) {
                // Centered in the 200 x 120 box at (20, 20): around (120, 80), with black around it.
                if (!is(at(120, 80), red) || is(at(30, 30), red) || !movingAtStart) { return; }
                box.Size({300, 200});         // The expression follows the parent's new size.
                moving.Offset({300, 160, 0});  // The implicit animation moves it there.
                step = 1;
            } else if (step == 1) {
                if (!movingAtStart && !movingAtEnd) { sawBetween = true; }
                if (!is(at(170, 120), red) || !movingAtEnd) { return; }
                require(!is(at(120, 80), red), "The centered sprite stayed where it was");
                require(sawBetween, "The offset change jumped instead of animating");
                std::cout << "centered=true follows_parent_size=true implicit_offset_animated=true frames=" << polls << '\n';
                step = 2;
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(stage.hwnd()));
            }
        };
        THROW_LAST_ERROR_IF(SetTimer(stage.hwnd(), 1, 30, nullptr) == 0);
        require(app.run() == 0 && step == 2, "The animation stages did not complete");
        capture.close();
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"helpers", helpers}});
}
