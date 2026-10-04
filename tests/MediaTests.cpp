#include "MediaDemoWindow.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

std::uint64_t pixels(composia::GraphicsDevice& graphics, ID3D11Texture2D* texture) {
    require(texture != nullptr, "No texture was presented");
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr<ID3D11Texture2D> readback;
    THROW_IF_FAILED(graphics.d3d_device()->CreateTexture2D(&desc, nullptr, readback.put()));
    graphics.d3d_context()->CopyResource(readback.get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    THROW_IF_FAILED(graphics.d3d_context()->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped));
    const auto unmap = wil::scope_exit([&] { graphics.d3d_context()->Unmap(readback.get(), 0); });
    std::uint64_t hash = 14695981039346656037ull;
    unsigned colored{};
    for (UINT y = 0; y < desc.Height; y += 3) {
        const auto row = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; x += 3) {
            const auto pixel = row + x * 4;
            for (int channel = 0; channel < 3; ++channel) { hash = (hash ^ pixel[channel]) * 1099511628211ull; }
            if (pixel[0] > 32 || pixel[1] > 32 || pixel[2] > 32) { ++colored; }
            require(pixel[3] == 255, "Rendered pixels lost opaque alpha");
        }
    }
    require(colored > 100, "Rendered texture contains no visible image");
    return hash;
}

void verify_shader(composia::Application& app) {
    TextureFrames frames{app.compositor(), app.graphics(), SIZE{320, 180}};
    GpuScene shader{app.graphics()};
    auto frame = frames.acquire();
    require(frame != nullptr, "Fresh texture was not available for drawing");
    shader.draw(frame->target.get(), frames.size(), 0);
    const auto before = pixels(app.graphics(), frame->surface->texture().get());
    shader.draw(frame->target.get(), frames.size(), 4);
    require(pixels(app.graphics(), frame->surface->texture().get()) != before, "GPU scene pixels did not animate");
}

class TestWindow final : public MediaDemoWindow {
public:
    TestWindow(composia::Application& app, std::filesystem::path path)
        : MediaDemoWindow(app), path_(std::move(path)) {
        if (!path_.empty()) { require(load_video(path_), "Video fixture was rejected"); }
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), 21, 50, nullptr) == 0);
    }

private:
    void click(const wchar_t* name) {
        const auto button = FindWindowExW(hwnd(), nullptr, L"Composia.Window", name);
        require(button != nullptr, "Demo control was not found");
        SetFocus(button);
        SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
    }

    void check() {
        if (stage_ == 6) { return; }
        const auto now = std::chrono::steady_clock::now();
        require(now - started_ < std::chrono::seconds{15}, "Media demo stopped making progress");
        require(SUCCEEDED(media_error()), "Media playback reported an error");
        switch (stage_) {
        case 0:
            if (scene_frames() < 12 || (!path_.empty() && video_frames() < 6)) { return; }
            if (!path_.empty()) { videoPixels_ = pixels(application().graphics(), video_texture()); }
            click(L"Play / pause");
            changed_ = now;
            ++stage_;
            break;
        case 1:
            if (now - changed_ < std::chrono::milliseconds{400}) { return; }
            pausedFrames_ = path_.empty() ? scene_frames() : video_frames();
            changed_ = now;
            ++stage_;
            break;
        case 2:
            if (now - changed_ < std::chrono::milliseconds{400}) { return; }
            require((path_.empty() ? scene_frames() : video_frames()) == pausedFrames_, "Pause did not stop frame production");
            if (!path_.empty()) { pausedPixels_ = pixels(application().graphics(), video_texture()); }
            generation_ = application().graphics().generation();
            application().graphics().recreate();
            ++stage_;
            break;
        case 3:
            if (scene_frames() == 0 || (!path_.empty() && video_frames() <= pausedFrames_)) { return; }
            require(application().graphics().generation() == generation_ + 1, "Paused graphics were not recreated");
            if (!path_.empty()) {
                require(pixels(application().graphics(), video_texture()) == pausedPixels_, "Paused video frame was not restored");
            }
            pausedFrames_ = path_.empty() ? scene_frames() : video_frames();
            click(L"Play / pause");
            ++stage_;
            break;
        case 4:
            if ((path_.empty() ? scene_frames() : video_frames()) < pausedFrames_ + 6) { return; }
            if (!path_.empty()) {
                require(pixels(application().graphics(), video_texture()) != videoPixels_, "Decoded video pixels did not advance");
            }
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd(), nullptr, 0, 0, 800, 610, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            generation_ = application().graphics().generation();
            recoveryVideoFrames_ = video_frames();
            application().graphics().recreate();
            ++stage_;
            break;
        case 5:
            if (scene_frames() < 12 || (!path_.empty() && video_frames() < recoveryVideoFrames_ + 6)) { return; }
            require(application().graphics().generation() == generation_ + 1, "Graphics were not recreated");
            if (!path_.empty()) {
                (void)pixels(application().graphics(), video_texture());
                click(L"GPU only");
                require(video_texture() == nullptr, "GPU-only mode retained the video texture");
            }
            require(!load_video(path_ / L"nonexistent-composia-video.mp4"), "A missing file was accepted");
            require(FAILED(media_error()), "Missing video did not report a recoverable error");
            std::cout << "scene_frames=" << scene_frames() << " recovered_generation=" << application().graphics().generation()
                      << " video_checked=" << !path_.empty() << '\n';
            ++stage_;
            PostMessageW(hwnd(), WM_CLOSE, 0, 0);
            break;
        }
    }

    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_TIMER && wparam == 21) { check(); return 0; }
        return MediaDemoWindow::on_message(message, wparam, lparam);
    }
    std::filesystem::path path_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now(), changed_{};
    std::uint64_t videoPixels_{}, pausedPixels_{}, generation_{};
    unsigned stage_{}, pausedFrames_{}, recoveryVideoFrames_{};
};
}

int main(int argc, char** argv) {
    try {
        const bool warp = argc > 1 && std::string_view{argv[1]} == "--warp";
        composia::Application app{warp};
        if (!composia::TextureSurface::supported(app.compositor(), app.graphics().d3d_device().get())) {
            std::cout << "Composition textures are unsupported on this device\n";
            return 77;
        }
        verify_shader(app);
        std::filesystem::path fixture;
        if (argc > 2) {
            fixture = std::filesystem::current_path() / (L"test vidéo #" + std::to_wstring(GetCurrentProcessId()) + L".mp4");
            std::filesystem::copy_file(argv[2], fixture);
        }
        const auto cleanup = wil::scope_exit([&] { if (!fixture.empty()) { std::error_code error; std::filesystem::remove(fixture, error); } });
        {
            TestWindow window{app, fixture};
            window.show();
            require(app.run() == 0, "Media demo message loop failed");
        }
        app.close();
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
