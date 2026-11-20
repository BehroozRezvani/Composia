#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>

using Microsoft::WRL::ComPtr;

int main() {
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3D11Device> devices[2];
    ComPtr<ID3D11DeviceContext> contexts[2];
    double milliseconds[2]{};

    for (unsigned i = 0; i < 2; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            devices[i].GetAddressOf(), nullptr, contexts[i].GetAddressOf());
        milliseconds[i] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (FAILED(hr)) {
            std::fprintf(stderr, "Device %u creation failed (HRESULT 0x%08lX)\n",
                i + 1, static_cast<unsigned long>(hr));
            return 1;
        }
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(devices[0].As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc))) {
        std::fputs("Could not identify the first device's adapter\n", stderr);
        return 1;
    }
    std::printf("PCI %04X:%04X  first %.3f ms  second %.3f ms\n",
        desc.VendorId, desc.DeviceId, milliseconds[0], milliseconds[1]);
}
