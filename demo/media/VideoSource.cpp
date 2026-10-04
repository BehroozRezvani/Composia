#include "VideoSource.hpp"
#include <shlwapi.h>
#include <algorithm>

void VideoSource::open(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path);
    if (!std::filesystem::is_regular_file(absolute)) { throw std::invalid_argument("Select a local video file"); }
    const auto name = absolute.wstring();
    std::wstring url(name.size() * 3 + 32, L'\0');
    DWORD length = static_cast<DWORD>(url.size());
    THROW_IF_FAILED(UrlCreateFromPathW(name.c_str(), url.data(), &length, 0));
    url.resize(length);
    close();
    state_ = std::make_shared<State>();
    consumed_ = copied_ = 0;
    paused_ = false;
    player_ = winrt::Windows::Media::Playback::MediaPlayer{};
    player_.CommandManager().IsEnabled(false);
    player_.IsVideoFrameServerEnabled(true);
    player_.IsLoopingEnabled(true);
    frameEvent_ = player_.VideoFrameAvailable(winrt::auto_revoke, [state = state_](const auto&, const auto&) noexcept {
        state->frames.fetch_add(1);
    });
    openedEvent_ = player_.MediaOpened(winrt::auto_revoke, [state = state_](const auto&, const auto&) noexcept {
        state->opened.store(true);
    });
    failedEvent_ = player_.MediaFailed(winrt::auto_revoke, [state = state_](const auto&, const auto& args) noexcept {
        try {
            const auto error = args.ExtendedErrorCode().value;
            state->error.store(FAILED(error) ? error : E_FAIL);
        }
        catch (...) { state->error.store(E_FAIL); }
    });
    player_.Source(winrt::Windows::Media::Core::MediaSource::CreateFromUri(winrt::Windows::Foundation::Uri{url}));
    player_.Play();
}

void VideoSource::close() noexcept {
    frameEvent_.revoke();
    openedEvent_.revoke();
    failedEvent_.revoke();
    try { if (player_) { player_.Close(); } }
    catch (...) { OutputDebugStringW(L"Composia media player shutdown failed.\n"); }
    player_ = nullptr;
    state_.reset();
}

bool VideoSource::copy_frame(const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface& target, bool restore) {
    if (!opened()) { return false; }
    const auto frame = state_->frames.load();
    if (frame == 0 || (!restore && frame == consumed_)) { return false; }
    player_.CopyFrameToVideoSurface(target);
    consumed_ = frame;
    ++copied_;
    return true;
}

void VideoSource::toggle_pause() {
    if (!player_) { return; }
    if (paused_) { player_.Play(); } else { player_.Pause(); }
    paused_ = !paused_;
}

SIZE VideoSource::size() const {
    if (!opened()) { return {}; }
    const auto session = player_.PlaybackSession();
    return {static_cast<LONG>(session.NaturalVideoWidth()), static_cast<LONG>(session.NaturalVideoHeight())};
}

double VideoSource::progress() const {
    if (!opened()) { return 0; }
    const auto session = player_.PlaybackSession();
    const auto duration = session.NaturalDuration().count();
    return duration > 0 ? std::clamp(static_cast<double>(session.Position().count()) / static_cast<double>(duration), 0.0, 1.0) : 0;
}
