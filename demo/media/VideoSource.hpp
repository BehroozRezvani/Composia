#pragma once

#include <composia/Composition.hpp>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <atomic>
#include <filesystem>
#include <memory>

class VideoSource {
public:
    VideoSource() = default;
    ~VideoSource() { close(); }
    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;
    void open(const std::filesystem::path&);
    void close() noexcept;
    bool copy_frame(const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface&, bool restore);
    void toggle_pause();
    [[nodiscard]] bool active() const noexcept { return player_ != nullptr; }
    [[nodiscard]] bool opened() const noexcept { return state_ && state_->opened.load(); }
    [[nodiscard]] HRESULT error() const noexcept { return state_ ? state_->error.load() : S_OK; }
    [[nodiscard]] SIZE size() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] unsigned copied_frames() const noexcept { return copied_; }

private:
    struct State { std::atomic_uint frames{}; std::atomic_bool opened{}; std::atomic<HRESULT> error{S_OK}; };
    std::shared_ptr<State> state_;
    winrt::Windows::Media::Playback::MediaPlayer player_{nullptr};
    winrt::Windows::Media::Playback::MediaPlayer::VideoFrameAvailable_revoker frameEvent_;
    winrt::Windows::Media::Playback::MediaPlayer::MediaOpened_revoker openedEvent_;
    winrt::Windows::Media::Playback::MediaPlayer::MediaFailed_revoker failedEvent_;
    unsigned consumed_{}, copied_{};
    bool paused_{};
};
