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

// A Windows Graphics Capture session of a window, a monitor, or a visual, polled for frames. Get
// the item from the system picker, from IGraphicsCaptureItemInterop, or from
// GraphicsCaptureItem::CreateFromVisual. Frames are BGRA textures on the device given to start;
// the capture follows the target's size. Call everything on the UI thread.
class ScreenCapture {
public:
    ScreenCapture() = default;
    ~ScreenCapture() { close(); }
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;
    // True when this system supports Windows Graphics Capture.
    [[nodiscard]] static bool supported();
    // Starts capturing the item with the device, replacing any current session. Throws
    // HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) without capture support.
    void start(const capture::GraphicsCaptureItem&, ID3D11Device*);
    // Restarts the current item's capture on another device, after a graphics device replacement.
    // Throws RO_E_CLOSED when no session is active.
    void recreate(ID3D11Device*);
    // Stops the session and releases the item, the frame pool, and the device. Close every frame
    // taken from next_frame first.
    void close() noexcept;
    // The newest frame, skipping older ones, or null when none is waiting, no session is active,
    // or the target was closed. Null also follows a target resize, while the frame pool adapts to
    // the new size. Close the frame before calling again, recreating, or closing.
    [[nodiscard]] capture::Direct3D11CaptureFrame next_frame();
    [[nodiscard]] bool active() const noexcept { return session_ != nullptr; }
    // True once the captured window or monitor has gone away; the session stays until close().
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
