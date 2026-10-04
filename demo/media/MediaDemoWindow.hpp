#pragma once

#include "GpuScene.hpp"
#include "TextureFrames.hpp"
#include "VideoSource.hpp"
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <chrono>

class MediaDemoWindow : public composia::Window {
public:
    explicit MediaDemoWindow(composia::Application&);
    ~MediaDemoWindow() override;
    bool load_video(const std::filesystem::path&);
    [[nodiscard]] bool textures_supported() const noexcept { return supported_; }
    [[nodiscard]] unsigned scene_frames() const noexcept { return sceneFrames_ ? sceneFrames_->presented() : 0; }
    [[nodiscard]] unsigned video_frames() const noexcept { return video_.copied_frames(); }
    [[nodiscard]] HRESULT media_error() const noexcept { return mediaError_; }
    [[nodiscard]] ID3D11Texture2D* video_texture() const noexcept { return videoFrames_ ? videoFrames_->current_texture() : nullptr; }

protected:
    void on_resize() override;
    void on_paint() override;
    void on_graphics_recreated() override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;

private:
    void initialize_graphics();
    void tick();
    void arrange();
    void draw_overlay();
    void choose_video();
    void reset_video();
    void caption(std::wstring_view title, std::wstring_view message);

    composia::CompositionWindowTarget target_;
    composia::composition::ContainerVisual hero_{nullptr};
    composia::composition::SpriteVisual sceneVisual_{nullptr}, videoVisual_{nullptr}, dot_{nullptr}, progress_{nullptr};
    composia::composition::CompositionSurfaceBrush sceneBrush_{nullptr}, videoBrush_{nullptr};
    composia::composition::CompositionRoundedRectangleGeometry heroClip_{nullptr}, sceneClip_{nullptr};
    std::unique_ptr<GpuScene> scene_;
    std::unique_ptr<TextureFrames> sceneFrames_, videoFrames_;
    VideoSource video_;
    composia::Button openButton_, pauseButton_, resetButton_;
    composia::Connection openClick_, pauseClick_, resetClick_;
    std::vector<composia::TextLayout> labels_;
    std::unique_ptr<composia::TextLayout> title_, message_;
    wil::com_ptr<ID2D1SolidColorBrush> brush_;
    composia::numerics::float2 heroSize_{};
    std::wstring fileName_;
    std::chrono::steady_clock::time_point lastTick_ = std::chrono::steady_clock::now();
    float seconds_{};
    HRESULT mediaError_{S_OK};
    bool supported_{}, initialized_{}, scenePaused_{}, showingVideo_{};
};
