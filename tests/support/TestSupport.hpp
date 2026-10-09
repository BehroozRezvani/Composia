#pragma once

#include <composia/Application.hpp>
#include <composia/ScreenCapture.hpp>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.ui.composition.interop.h>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

// Shared by Composia's test executables: checks, scenario selection, and pixel readback.
namespace composia::testing {

// The exit code CTest reports as a skip (SKIP_RETURN_CODE), for checks this machine cannot run.
inline constexpr int skipped = 77;

inline void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

// True when the action fails with the expected HRESULT, from WIL or C++/WinRT.
template<class Action>
bool rejects(HRESULT expected, Action&& action) {
    try { std::forward<Action>(action)(); }
    catch (const wil::ResultException& error) { return error.GetErrorCode() == expected; }
    catch (const winrt::hresult_error& error) { return error.code() == expected; }
    return false;
}

// True when the action throws the given exception type.
template<class Exception, class Action>
bool throws(Action&& action) {
    try { std::forward<Action>(action)(); }
    catch (const Exception&) { return true; }
    return false;
}

struct Options {
    bool warp{};  // --warp: the WARP software device instead of hardware.
};

// A named check. It returns 0 when it passes, or skipped when this machine cannot run it.
struct Scenario {
    std::string_view name;
    std::function<int(const Options&)> run;
};

// Runs the scenario named by the first argument, or the only one, and reports a failure on
// stderr with exit code 1. Arguments: [scenario] [--warp].
inline int run(int argc, char** argv, std::initializer_list<Scenario> scenarios) {
    try {
        Options options;
        std::string_view name;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--warp") { options.warp = true; }
            else if (name.empty()) { name = argument; }
            else { throw std::invalid_argument("Unexpected test argument"); }
        }
        for (const auto& scenario : scenarios) {
            if (name.empty() ? scenarios.size() == 1 : scenario.name == name) { return scenario.run(options); }
        }
        throw std::invalid_argument("Unknown test scenario");
    } catch (const winrt::hresult_error& error) {
        std::cerr << winrt::to_string(error.message()) << " (0x" << std::hex << static_cast<std::uint32_t>(error.code()) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}

struct Pixel {
    int r{}, g{}, b{}, a{};
};

// True when the pixel is within tolerance of a 0xRRGGBB color.
inline bool matches(Pixel pixel, std::uint32_t rgb, int tolerance = 12) {
    return std::abs(pixel.r - static_cast<int>((rgb >> 16) & 0xFF)) < tolerance &&
        std::abs(pixel.g - static_cast<int>((rgb >> 8) & 0xFF)) < tolerance &&
        std::abs(pixel.b - static_cast<int>(rgb & 0xFF)) < tolerance;
}

// One BGRA pixel of a texture on the application's device, read back through a staging copy.
inline Pixel texture_pixel(Application& app, ID3D11Texture2D* texture, UINT x, UINT y) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    require(x < desc.Width && y < desc.Height, "The pixel is outside the texture");
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr<ID3D11Texture2D> staging;
    THROW_IF_FAILED(app.graphics().d3d_device()->CreateTexture2D(&desc, nullptr, staging.put()));
    const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
    const auto context = app.graphics().d3d_context().get();
    context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    THROW_IF_FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
    const auto bytes = static_cast<const unsigned char*>(mapped.pData);
    const Pixel pixel{bytes[2], bytes[1], bytes[0], bytes[3]};
    context->Unmap(staging.get(), 0);
    return pixel;
}

// One pixel of a captured frame, at frame pixel coordinates.
inline Pixel frame_pixel(Application& app, const capture::Direct3D11CaptureFrame& frame, UINT x, UINT y) {
    const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    wil::com_ptr<ID3D11Texture2D> texture;
    THROW_IF_FAILED(access->GetInterface(IID_PPV_ARGS(texture.put())));
    return texture_pixel(app, texture.get(), x, y);
}

// One pixel of a drawing surface or virtual drawing surface, at surface pixel coordinates.
inline Pixel surface_pixel(Application& app, const winrt::Windows::Foundation::IInspectable& surface, LONG x, LONG y) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    wil::com_ptr<ID3D11Texture2D> texture;
    THROW_IF_FAILED(app.graphics().d3d_device()->CreateTexture2D(&desc, nullptr, texture.put()));
    const RECT region{x, y, x + 1, y + 1};
    THROW_IF_FAILED(surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop2>()->CopySurface(texture.get(), 0, 0, &region));
    return texture_pixel(app, texture.get(), 0, 0);
}

}
