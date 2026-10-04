#include "MediaDemoWindow.hpp"
#include <composia/AnimationHelpers.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <commdlg.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>

using namespace composia;

namespace {
constexpr UINT_PTR frameTimer = 20;
}

MediaDemoWindow::MediaDemoWindow(Application& app)
    : Window(app, L"Composia — Texture studio", 1120, 800),
      target_(app.compositor(), app.graphics(), hwnd()), openButton_(*this, L"Open video"),
      pauseButton_(*this, L"Play / pause"), resetButton_(*this, L"GPU only") {
    const auto compositor = app.compositor();
    auto background = animations::sprite(compositor, {1120, 800}, {255, 12, 18, 29});
    background.RelativeSizeAdjustment({1, 1});
    background.Size({0, 0});
    target_.root().Children().InsertAtBottom(background);
    hero_ = compositor.CreateContainerVisual();
    hero_.Offset({32, 140, 0});
    heroClip_ = compositor.CreateRoundedRectangleGeometry();
    heroClip_.CornerRadius({18, 18});
    hero_.Clip(compositor.CreateGeometricClip(heroClip_));
    auto base = animations::sprite(compositor, {0, 0}, {255, 3, 7, 13});
    base.RelativeSizeAdjustment({1, 1});
    hero_.Children().InsertAtBottom(base);
    sceneBrush_ = compositor.CreateSurfaceBrush();
    sceneBrush_.Stretch(composition::CompositionStretch::UniformToFill);
    videoBrush_ = compositor.CreateSurfaceBrush();
    videoBrush_.Stretch(composition::CompositionStretch::Uniform);
    videoVisual_ = compositor.CreateSpriteVisual();
    videoVisual_.Brush(videoBrush_);
    hero_.Children().InsertAtTop(videoVisual_);
    sceneVisual_ = compositor.CreateSpriteVisual();
    sceneVisual_.Brush(sceneBrush_);
    sceneClip_ = compositor.CreateRoundedRectangleGeometry();
    sceneClip_.CornerRadius({12, 12});
    sceneVisual_.Clip(compositor.CreateGeometricClip(sceneClip_));
    hero_.Children().InsertAtTop(sceneVisual_);
    target_.root().Children().InsertAtTop(hero_);
    target_.root().Children().Remove(target_.canvas());
    target_.root().Children().InsertAtTop(target_.canvas());
    dot_ = animations::sprite(compositor, {7, 7}, {255, 111, 230, 200});
    target_.root().Children().InsertAtTop(dot_);
    animations::pulse(dot_);
    progress_ = animations::sprite(compositor, {0, 3}, {255, 111, 230, 200});
    hero_.Children().InsertAtTop(progress_);
    const auto factory = app.graphics().text_factory().get();
    labels_.emplace_back(factory, L"COMPOSIA  /  COMPOSITION STUDY", 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"Every layer. One scene.", 32, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"Motion, film and type. Composed live.", 14);
    labels_.emplace_back(factory, L"LIVE COMPOSITION", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"Open or drop a local video. The GPU scene keeps moving alongside it.", 13);
    labels_.emplace_back(factory, L"GPU SCENE", 10, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    caption(L"Aurora field", L"An animated landscape, drawn entirely on the GPU.");
    openClick_ = openButton_.on_click([this] { choose_video(); });
    pauseClick_ = pauseButton_.on_click([this] {
        if (video_.active()) { video_.toggle_pause(); }
        else { scenePaused_ = !scenePaused_; }
    });
    resetClick_ = resetButton_.on_click([this] { reset_video(); });
    initialize_graphics();
    DragAcceptFiles(hwnd(), TRUE);
    THROW_LAST_ERROR_IF(SetTimer(hwnd(), frameTimer, 16, nullptr) == 0);
    invalidate();
}

MediaDemoWindow::~MediaDemoWindow() {
    if (hwnd()) { KillTimer(hwnd(), frameTimer); }
    video_.close();
}

void MediaDemoWindow::caption(std::wstring_view title, std::wstring_view message) {
    const auto factory = application().graphics().text_factory().get();
    title_ = std::make_unique<TextLayout>(factory, title, 26, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    message_ = std::make_unique<TextLayout>(factory, message, 13);
    THROW_IF_FAILED(title_->layout()->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
    invalidate();
}

void MediaDemoWindow::initialize_graphics() {
    auto& app = application();
    supported_ = TextureSurface::supported(app.compositor(), app.graphics().d3d_device().get());
    openButton_.enabled(supported_);
    pauseButton_.enabled(supported_);
    resetButton_.enabled(supported_);
    if (supported_) {
        app.graphics().d3d_device().query<ID3D11Multithread>()->SetMultithreadProtected(TRUE);
        scene_ = std::make_unique<GpuScene>(app.graphics());
        sceneFrames_ = std::make_unique<TextureFrames>(app.compositor(), app.graphics(), SIZE{960, 540});
    } else {
        caption(L"Composition textures unavailable", L"This demo needs a Windows build and graphics driver that support composition textures.");
    }
    initialized_ = true;
}

void MediaDemoWindow::tick() {
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    if (IsIconic(hwnd())) { return; }
    if (!scenePaused_) { seconds_ += std::min(delta, .1f); }
    application().render([&] {
        if (!initialized_) { initialize_graphics(); }
        if (!supported_) { return; }
        if (auto frame = sceneFrames_->acquire(); frame && (!scenePaused_ || sceneFrames_->presented() == 0)) {
            scene_->draw(frame->target.get(), sceneFrames_->size(), seconds_);
            sceneFrames_->present(*frame, sceneBrush_);
        }
        if (FAILED(video_.error())) {
            mediaError_ = video_.error();
            spdlog::warn("event=video_failed hresult=0x{:08X}", static_cast<unsigned>(mediaError_));
            reset_video();
            caption(L"Could not play this video", L"Try another local file with a codec installed on this PC.");
            return;
        }
        if (!video_.opened()) { return; }
        const auto native = video_.size();
        if (native.cx <= 0 || native.cy <= 0) {
            mediaError_ = E_INVALIDARG;
            reset_video();
            caption(L"No video track", L"Choose a file that contains video.");
            return;
        }
        const float scale = std::min({1.0f, 1280.0f / native.cx, 720.0f / native.cy});
        const SIZE size{std::max(1L, static_cast<LONG>(native.cx * scale)), std::max(1L, static_cast<LONG>(native.cy * scale))};
        if (!videoFrames_ || videoFrames_->size().cx != size.cx || videoFrames_->size().cy != size.cy) {
            videoBrush_.Surface(nullptr);
            videoFrames_ = std::make_unique<TextureFrames>(application().compositor(), application().graphics(), size);
        }
        if (auto frame = videoFrames_->acquire(); frame && video_.copy_frame(frame->video, videoFrames_->presented() == 0)) {
            application().graphics().d3d_context()->Flush();
            videoFrames_->present(*frame, videoBrush_);
            if (!showingVideo_) {
                showingVideo_ = true;
                caption(fileName_, L"Local video + live GPU picture-in-picture. Text is a separate composition layer.");
                arrange();
            }
        }
        progress_.Size({heroSize_.x * static_cast<float>(video_.progress()), 3});
    });
}

bool MediaDemoWindow::load_video(const std::filesystem::path& path) {
    if (!supported_) { return false; }
    try {
        video_.open(path);
        mediaError_ = S_OK;
        fileName_ = path.filename().wstring();
        showingVideo_ = false;
        scenePaused_ = false;
        videoBrush_.Surface(nullptr);
        videoFrames_.reset();
        caption(L"Opening video…", L"The GPU scene continues while the video loads.");
        arrange();
        return true;
    } catch (const winrt::hresult_error& error) { mediaError_ = error.code(); }
    catch (const wil::ResultException& error) { mediaError_ = error.GetErrorCode(); }
    catch (const std::exception&) { mediaError_ = E_INVALIDARG; }
    reset_video();
    caption(L"Could not open this file", L"Choose a local video file that can be read by this PC.");
    return false;
}

void MediaDemoWindow::choose_video() {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd();
    dialog.lpstrFilter = L"Video files\0*.mp4;*.m4v;*.mov;*.mkv;*.avi;*.wmv;*.webm\0All files\0*.*\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) { load_video(path.data()); }
}

void MediaDemoWindow::reset_video() {
    video_.close();
    videoBrush_.Surface(nullptr);
    videoFrames_.reset();
    showingVideo_ = false;
    scenePaused_ = false;
    progress_.Size({0, 3});
    caption(L"Aurora field", L"An animated landscape, drawn entirely on the GPU.");
    arrange();
}

void MediaDemoWindow::arrange() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0) { return; }
    target_.resize(pixels, dpi());
    const auto size = target_.logical_size();
    heroSize_ = {std::max(1.0f, size.x - 64), std::max(1.0f, size.y - 252)};
    hero_.Size(heroSize_);
    heroClip_.Size(heroSize_);
    videoVisual_.Size(heroSize_);
    videoVisual_.IsVisible(showingVideo_);
    if (showingVideo_) {
        const float width = std::min(272.0f, heroSize_.x * .30f);
        sceneVisual_.Size({width, width * .5625f});
        sceneVisual_.Offset({heroSize_.x - width - 20, heroSize_.y - width * .5625f - 20, 0});
    } else {
        sceneVisual_.Size(heroSize_);
        sceneVisual_.Offset({0, 0, 0});
    }
    sceneClip_.Size(sceneVisual_.Size());
    progress_.Offset({0, heroSize_.y - 3, 0});
    dot_.Offset({61, 163, 0});
    const auto buttons = layout::stack({32, size.y - 76, size.x - 64, 44}, std::array{156.0f, 156.0f, 136.0f}, 12, layout::Axis::horizontal);
    openButton_.set_bounds(buttons[0]);
    pauseButton_.set_bounds(buttons[1]);
    resetButton_.set_bounds(buttons[2]);
}

void MediaDemoWindow::draw_overlay() {
    ScopedSurfaceDraw draw{target_.surface(), application().graphics(), dpi()};
    const auto dc = draw.context().get();
    const auto size = target_.logical_size();
    dc->Clear(D2D1::ColorF(0, 0));
    if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0xEAF2F4), brush_.put())); }
    auto text = [&](TextLayout& label, float x, float y, float width, float height, UINT32 color) {
        label.resize(std::max(1.0f, width), height);
        brush_->SetColor(D2D1::ColorF(color));
        dc->DrawTextLayout({x, y}, label.layout().get(), brush_.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    text(labels_[0], 32, 25, size.x - 64, 22, 0x6FE6C8);
    text(labels_[1], 30, 54, size.x - 64, 44, 0xEAF2F4);
    text(labels_[2], 32, 103, size.x - 64, 24, 0x93A9B5);
    brush_->SetColor(D2D1::ColorF(0x0C121D, .8f));
    dc->FillRoundedRectangle(D2D1::RoundedRect({48, 152, 217, 181}, 14, 14), brush_.get());
    text(labels_[3], 76, 158, 138, 20, 0xEAF2F4);
    const float captionWidth = std::max(140.0f, heroSize_.x * (showingVideo_ ? .59f : .65f));
    const float captionTop = 140 + heroSize_.y - 121;
    brush_->SetColor(D2D1::ColorF(0x0C121D, .82f));
    dc->FillRoundedRectangle(D2D1::RoundedRect({48, captionTop, 48 + captionWidth, captionTop + 101}, 12, 12), brush_.get());
    text(*title_, 66, captionTop + 12, captionWidth - 36, 37, 0xEAF2F4);
    text(*message_, 66, captionTop + 55, captionWidth - 36, 40, 0xB3C6D0);
    if (showingVideo_) {
        const auto offset = sceneVisual_.Offset();
        text(labels_[5], 32 + offset.x + 12, 140 + offset.y + 10, 160, 20, 0xD8F4EC);
    }
    text(labels_[4], 32, size.y - 104, size.x - 64, 22, 0x93A9B5);
    draw.finish();
}

void MediaDemoWindow::on_resize() { invalidate(); }
void MediaDemoWindow::on_paint() {
    if (IsIconic(hwnd())) { return; }
    application().render([&] { arrange(); draw_overlay(); });
}
void MediaDemoWindow::on_graphics_recreated() {
    sceneBrush_.Surface(nullptr);
    videoBrush_.Surface(nullptr);
    sceneFrames_.reset();
    videoFrames_.reset();
    scene_.reset();
    brush_.reset();
    initialized_ = false;
}

std::optional<LRESULT> MediaDemoWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER && wparam == frameTimer) { tick(); return 0; }
    if (message == WM_DROPFILES) {
        const auto drop = reinterpret_cast<HDROP>(wparam);
        const auto cleanup = wil::scope_exit([&] { DragFinish(drop); });
        const auto length = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, 0, path.data(), length + 1);
        path.resize(length);
        load_video(path);
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        const auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
        info->ptMinTrackSize = {MulDiv(720, windowDpi, 96), MulDiv(570, windowDpi, 96)};
        return 0;
    }
    if (message == WM_DESTROY) { KillTimer(hwnd(), frameTimer); video_.close(); return 0; }
    return std::nullopt;
}
