#include "MediaDemoWindow.hpp"
#include <composia/AnimationHelpers.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>

using namespace composia;

namespace {
constexpr UINT_PTR frameTimer = 20;
}

MediaDemoWindow::MediaDemoWindow(Application& app)
    : Window(app, L"Composia — Texture studio", 1120, 800),
      target_(app.compositor(), app.graphics(), hwnd()), captureButton_(*this, L"Capture…"),
      stopButton_(*this, L"Stop capture"), openButton_(*this, L"Open video"),
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
    contentBrush_ = compositor.CreateSurfaceBrush();
    contentBrush_.Stretch(composition::CompositionStretch::Uniform);
    contentVisual_ = compositor.CreateSpriteVisual();
    contentVisual_.Brush(contentBrush_);
    hero_.Children().InsertAtTop(contentVisual_);
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
    labels_.emplace_back(factory, L"Your screen, motion and type. Composed live.", 14);
    labels_.emplace_back(factory, L"LIVE COMPOSITION", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"Capture a window or display, or open a video. The GPU scene keeps moving alongside it.", 13);
    labels_.emplace_back(factory, L"GPU SCENE", 10, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    caption(L"Aurora field", L"An animated landscape, drawn entirely on the GPU.");
    openClick_ = openButton_.on_click([this] { choose_video(); });
    captureClick_ = captureButton_.on_click([this] { choose_capture(); });
    stopClick_ = stopButton_.on_click([this] { reset_media(); });
    pauseClick_ = pauseButton_.on_click([this] {
        if (video_.active()) { video_.toggle_pause(); }
        else { scenePaused_ = !scenePaused_; }
    });
    resetClick_ = resetButton_.on_click([this] { reset_media(); });
    initialize_graphics();
    DragAcceptFiles(hwnd(), TRUE);
    THROW_LAST_ERROR_IF(SetTimer(hwnd(), frameTimer, 16, nullptr) == 0);
    invalidate();
}

MediaDemoWindow::~MediaDemoWindow() {
    if (hwnd()) { KillTimer(hwnd(), frameTimer); }
    cancel_picker();
    capture_.close();
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
    captureButton_.enabled(supported_ && ScreenCapture::supported() && !pickOperation_);
    stopButton_.enabled(capture_.active());
    pauseButton_.enabled(supported_ && !capture_.active());
    resetButton_.enabled(supported_);
    if (supported_) {
        app.graphics().d3d_device().query<ID3D11Multithread>()->SetMultithreadProtected(TRUE);
        scene_ = std::make_unique<GpuScene>(app.graphics());
        sceneFrames_ = std::make_unique<TextureFrames>(app.compositor(), app.graphics(), SIZE{960, 540});
        captureNeedsDevice_ = capture_.active();
    } else {
        caption(L"Composition textures unavailable", L"This demo needs a Windows build and graphics driver that support composition textures.");
    }
    initialized_ = true;
}

void MediaDemoWindow::tick() {
    if (ticking_) { return; }
    ticking_ = true;
    const auto finish = wil::scope_exit([&] { ticking_ = false; });
    poll_picker();
    if (capture_.target_closed()) {
        reset_media();
        caption(L"Capture ended", L"The captured window or display is no longer available.");
    }
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
        if (capture_.active()) {
            try {
                if (captureNeedsDevice_) {
                    capture_.recreate(application().graphics().d3d_device().get());
                    captureNeedsDevice_ = false;
                }
                draw_capture();
            }
            catch (const winrt::hresult_error& error) {
                if (application().graphics().is_device_loss(error.code())) { throw; }
                capture_failed(error.code());
            } catch (const wil::ResultException& error) {
                if (application().graphics().is_device_loss(error.GetErrorCode())) { throw; }
                capture_failed(error.GetErrorCode());
            }
            return;
        }
        if (FAILED(video_.error())) {
            mediaError_ = video_.error();
            spdlog::warn("event=video_failed hresult=0x{:08X}", static_cast<unsigned>(mediaError_));
            reset_media();
            caption(L"Could not play this video", L"Try another local file with a codec installed on this PC.");
            return;
        }
        if (!video_.opened()) { return; }
        const auto native = video_.size();
        if (native.cx <= 0 || native.cy <= 0) {
            mediaError_ = E_INVALIDARG;
            reset_media();
            caption(L"No video track", L"Choose a file that contains video.");
            return;
        }
        const float scale = std::min({1.0f, 1280.0f / native.cx, 720.0f / native.cy});
        const SIZE size{std::max(1L, static_cast<LONG>(native.cx * scale)), std::max(1L, static_cast<LONG>(native.cy * scale))};
        if (!contentFrames_ || contentFrames_->size().cx != size.cx || contentFrames_->size().cy != size.cy) {
            contentBrush_.Surface(nullptr);
            contentFrames_ = std::make_unique<TextureFrames>(application().compositor(), application().graphics(), size);
        }
        if (auto frame = contentFrames_->acquire(); frame && video_.copy_frame(frame->video, contentFrames_->presented() == 0)) {
            application().graphics().d3d_context()->Flush();
            contentFrames_->present(*frame, contentBrush_);
            if (!showingContent_) {
                showingContent_ = true;
                caption(contentName_, L"Local video + live GPU picture-in-picture. Text is a separate composition layer.");
                arrange();
            }
        }
        progress_.Size({heroSize_.x * static_cast<float>(video_.progress()), 3});
    });
}

bool MediaDemoWindow::load_video(const std::filesystem::path& path) {
    if (!supported_) { return false; }
    try {
        reset_media();
        video_.open(path);
        mediaError_ = S_OK;
        contentName_ = path.filename().wstring();
        showingContent_ = false;
        scenePaused_ = false;
        contentBrush_.Surface(nullptr);
        contentFrames_.reset();
        caption(L"Opening video…", L"The GPU scene continues while the video loads.");
        arrange();
        return true;
    } catch (const winrt::hresult_error& error) { mediaError_ = error.code(); }
    catch (const wil::ResultException& error) { mediaError_ = error.GetErrorCode(); }
    catch (const std::exception&) { mediaError_ = E_INVALIDARG; }
    reset_media();
    caption(L"Could not open this file", L"Choose a local video file that can be read by this PC.");
    return false;
}

bool MediaDemoWindow::start_capture(const capture::GraphicsCaptureItem& item) {
    if (!item || !supported_) { return false; }
    try {
        reset_media();
        capture_.start(item, application().graphics().d3d_device().get());
        captureNeedsDevice_ = false;
        captureFrames_ = 0;
        mediaError_ = S_OK;
        contentName_ = item.DisplayName();
        stopButton_.enabled(true);
        pauseButton_.enabled(false);
        caption(L"Starting capture…", L"The selected window or display will appear here.");
        return true;
    } catch (const winrt::hresult_error& error) { capture_failed(error.code()); }
    catch (const wil::ResultException& error) { capture_failed(error.GetErrorCode()); }
    return false;
}

void MediaDemoWindow::choose_capture() {
    if (pickOperation_) { return; }
    try {
        picker_ = capture::GraphicsCapturePicker{};
        THROW_IF_FAILED(picker_.as<IInitializeWithWindow>()->Initialize(hwnd()));
        pickOperation_ = picker_.PickSingleItemAsync();
        captureButton_.enabled(false);
    } catch (const winrt::hresult_error& error) { capture_failed(error.code()); }
    catch (const wil::ResultException& error) { capture_failed(error.GetErrorCode()); }
}

void MediaDemoWindow::poll_picker() {
    using winrt::Windows::Foundation::AsyncStatus;
    if (!pickOperation_ || pickOperation_.Status() == AsyncStatus::Started) { return; }
    auto operation = std::exchange(pickOperation_, nullptr);
    picker_ = nullptr;
    captureButton_.enabled(supported_ && ScreenCapture::supported());
    try {
        const auto finish = wil::scope_exit([&] { operation.Close(); });
        if (operation.Status() != AsyncStatus::Canceled) {
            if (const auto item = operation.GetResults()) { start_capture(item); }
        }
    } catch (const winrt::hresult_error& error) { capture_failed(error.code()); }
    catch (const wil::ResultException& error) { capture_failed(error.GetErrorCode()); }
}

void MediaDemoWindow::cancel_picker() noexcept {
    auto operation = std::exchange(pickOperation_, nullptr);
    picker_ = nullptr;
    try { if (operation) { operation.Cancel(); } }
    catch (...) { OutputDebugStringW(L"Composia capture picker cancellation failed.\n"); }
}

void MediaDemoWindow::capture_failed(HRESULT error) {
    mediaError_ = error;
    spdlog::warn("event=capture_failed hresult=0x{:08X}", static_cast<unsigned>(error));
    reset_media();
    caption(L"Capture unavailable", L"Try selecting another window or display.");
}

void MediaDemoWindow::draw_capture() {
    auto frame = capture_.next_frame();
    if (!frame) { return; }
    const auto release = wil::scope_exit([&] { frame.Close(); });
    const auto size = frame.ContentSize();
    wil::com_ptr<ID3D11Texture2D> source;
    THROW_IF_FAILED(frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()->GetInterface(IID_PPV_ARGS(source.put())));
    wil::com_ptr<ID3D11Device> sourceDevice;
    source->GetDevice(sourceDevice.put());
    THROW_HR_IF(E_UNEXPECTED, sourceDevice.get() != application().graphics().d3d_device().query<ID3D11Device>().get());
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    THROW_HR_IF(E_BOUNDS, static_cast<UINT>(size.Width) > desc.Width || static_cast<UINT>(size.Height) > desc.Height);
    if (!contentFrames_ || contentFrames_->size().cx != size.Width || contentFrames_->size().cy != size.Height) {
        contentBrush_.Surface(nullptr);
        contentFrames_ = std::make_unique<TextureFrames>(application().compositor(), application().graphics(), SIZE{size.Width, size.Height});
    }
    if (auto destination = contentFrames_->acquire()) {
        const D3D11_BOX region{0, 0, 0, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 1};
        const auto context = application().graphics().d3d_context().get();
        context->CopySubresourceRegion(destination->surface->texture().get(), 0, 0, 0, 0, source.get(), 0, &region);
        context->Flush();
        contentFrames_->present(*destination, contentBrush_);
        ++captureFrames_;
        if (!showingContent_) {
            showingContent_ = true;
            caption(contentName_, L"Live capture. Stop capture returns to the GPU scene.");
            arrange();
        }
    }
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

void MediaDemoWindow::reset_media() {
    cancel_picker();
    capture_.close();
    video_.close();
    contentBrush_.Surface(nullptr);
    contentFrames_.reset();
    showingContent_ = false;
    scenePaused_ = false;
    progress_.Size({0, 3});
    stopButton_.enabled(false);
    pauseButton_.enabled(supported_);
    captureButton_.enabled(supported_ && ScreenCapture::supported());
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
    contentVisual_.Size(heroSize_);
    contentVisual_.IsVisible(showingContent_);
    if (showingContent_) {
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
    const auto buttons = layout::stack({32, size.y - 76, size.x - 64, 44}, std::array{120.0f, 116.0f, 108.0f, 120.0f, 104.0f}, 8, layout::Axis::horizontal);
    captureButton_.set_bounds(buttons[0]);
    stopButton_.set_bounds(buttons[1]);
    openButton_.set_bounds(buttons[2]);
    pauseButton_.set_bounds(buttons[3]);
    resetButton_.set_bounds(buttons[4]);
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
    const float captionWidth = std::max(140.0f, heroSize_.x * (showingContent_ ? .59f : .65f));
    const float captionTop = 140 + heroSize_.y - 121;
    brush_->SetColor(D2D1::ColorF(0x0C121D, .82f));
    dc->FillRoundedRectangle(D2D1::RoundedRect({48, captionTop, 48 + captionWidth, captionTop + 101}, 12, 12), brush_.get());
    text(*title_, 66, captionTop + 12, captionWidth - 36, 37, 0xEAF2F4);
    text(*message_, 66, captionTop + 55, captionWidth - 36, 40, 0xB3C6D0);
    if (showingContent_) {
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
    contentBrush_.Surface(nullptr);
    sceneFrames_.reset();
    contentFrames_.reset();
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
    if (message == WM_DESTROY) { KillTimer(hwnd(), frameTimer); cancel_picker(); capture_.close(); video_.close(); return 0; }
    return std::nullopt;
}
