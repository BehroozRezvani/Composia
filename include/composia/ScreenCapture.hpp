#pragma once

#include <composia/Native.hpp>
#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <atomic>
#include <memory>

namespace composia {
namespace capture = winrt::Windows::Graphics::Capture;

class ScreenCapture {
public:
    ScreenCapture() = default;
    ~ScreenCapture() { close(); }
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;
    [[nodiscard]] static bool supported();
    void start(const capture::GraphicsCaptureItem&, ID3D11Device*);
    void recreate(ID3D11Device*);
    void close() noexcept;
    [[nodiscard]] capture::Direct3D11CaptureFrame next_frame();
    [[nodiscard]] bool active() const noexcept { return session_ != nullptr; }
    [[nodiscard]] bool target_closed() const noexcept { return closed_ && closed_->load(); }
    [[nodiscard]] const capture::GraphicsCaptureItem& item() const noexcept { return item_; }
    [[nodiscard]] const capture::Direct3D11CaptureFramePool& frame_pool() const noexcept { return pool_; }
    [[nodiscard]] const capture::GraphicsCaptureSession& session() const noexcept { return session_; }

private:
    capture::GraphicsCaptureItem item_{nullptr};
    capture::Direct3D11CaptureFramePool pool_{nullptr};
    capture::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device_{nullptr};
    capture::GraphicsCaptureItem::Closed_revoker closedEvent_;
    std::shared_ptr<std::atomic_bool> closed_;
    winrt::Windows::Graphics::SizeInt32 size_{};
};

}
