#pragma once

#include "GpuScene.hpp"
#include "TextureFrames.hpp"
#include "VideoSource.hpp"
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/ScreenCapture.hpp>
#include <chrono>

class MediaDemoWindow : public composia::Window {
public:
    explicit MediaDemoWindow(composia::Application&);
    ~MediaDemoWindow() override;
    bool load_video(const std::filesystem::path&);
    bool start_capture(const composia::capture::GraphicsCaptureItem&);
    [[nodiscard]] bool capturing() const noexcept { return capture_.active(); }
    [[nodiscard]] unsigned capture_frames() const noexcept { return captureFrames_; }
    [[nodiscard]] ID3D11Texture2D* capture_texture() const noexcept { return capturing() && contentFrames_ ? contentFrames_->current_texture() : nullptr; }
    [[nodiscard]] bool textures_supported() const noexcept { return supported_; }
    [[nodiscard]] unsigned scene_frames() const noexcept { return sceneFrames_ ? sceneFrames_->presented() : 0; }
    [[nodiscard]] unsigned video_frames() const noexcept { return video_.copied_frames(); }
    [[nodiscard]] HRESULT media_error() const noexcept { return mediaError_; }
    [[nodiscard]] ID3D11Texture2D* video_texture() const noexcept { return video_.active() && contentFrames_ ? contentFrames_->current_texture() : nullptr; }

protected:
    void on_resize() override;
    void on_paint() override;
    void on_graphics_recreated() override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;

private:
    void initialize_graphics();
    void update_controls();
    void tick();
    void arrange();
    void draw_overlay();
    void choose_video();
    void choose_capture();
    void poll_picker();
    void cancel_picker() noexcept;
    void draw_capture();
    void capture_failed(HRESULT);
    void reset_media();
    void caption(std::wstring_view title, std::wstring_view message);

    composia::CompositionWindowTarget target_;
    composia::composition::ContainerVisual hero_{nullptr};
    composia::composition::SpriteVisual sceneVisual_{nullptr}, contentVisual_{nullptr}, dot_{nullptr}, progress_{nullptr};
    composia::composition::CompositionSurfaceBrush sceneBrush_{nullptr}, contentBrush_{nullptr};
    composia::composition::CompositionRoundedRectangleGeometry heroClip_{nullptr}, sceneClip_{nullptr};
    std::unique_ptr<GpuScene> scene_;
    std::unique_ptr<TextureFrames> sceneFrames_, contentFrames_;
    VideoSource video_;
    composia::ScreenCapture capture_;
    composia::capture::GraphicsCapturePicker picker_{nullptr};
    winrt::Windows::Foundation::IAsyncOperation<composia::capture::GraphicsCaptureItem> pickOperation_{nullptr};
    composia::Button captureButton_, stopButton_, openButton_, pauseButton_, resetButton_;
    composia::Connection captureClick_, stopClick_, openClick_, pauseClick_, resetClick_;
    std::vector<composia::TextLayout> labels_;
    std::unique_ptr<composia::TextLayout> title_, message_;
    wil::com_ptr<ID2D1SolidColorBrush> brush_;
    composia::numerics::float2 heroSize_{};
    std::wstring contentName_;
    std::chrono::steady_clock::time_point lastTick_ = std::chrono::steady_clock::now();
    float seconds_{};
    HRESULT mediaError_{S_OK};
    unsigned captureFrames_{};
    bool supported_{}, initialized_{}, scenePaused_{}, showingContent_{}, ticking_{}, captureNeedsDevice_{}, closeAfterPick_{};
};
