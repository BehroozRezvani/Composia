#pragma once

#include <composia/Composition.hpp>
#include <composia/Signal.hpp>
#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <cstdint>

namespace composia {

// The Direct3D 11, Direct2D, and DirectWrite objects every surface draws with, and the Composition
// graphics device built on them. Application owns one; reach it through Application::graphics().
// Hold the device objects only as long as the current generation: a replacement makes them stale.
class GraphicsDevice {
public:
    // Prefers a hardware device and falls back to WARP; forceWarp selects WARP directly.
    explicit GraphicsDevice(const composition::Compositor&, bool forceWarp = false);
    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    // Replaces the Direct3D and Direct2D devices and moves the Composition graphics device onto
    // them, so visuals and surfaces survive; surface content must be drawn again. Nothing changes
    // if creating the replacement fails. On success the generation advances and on_recreated runs.
    void recreate();
    // Runs after every replacement with the new generation. Keep the Connection for as long as the
    // callback should run.
    Connection on_recreated(std::function<void(std::uint64_t)> callback) { return recreated_.connect(std::move(callback)); }
    // True for the errors that mean the device is gone (removed, reset, hung, or a Direct2D target
    // to recreate), and for any error while the current device reports itself removed.
    [[nodiscard]] bool is_device_loss(HRESULT error) const noexcept;
    // Signaled when Direct3D removes the device; Application::run waits on it.
    [[nodiscard]] HANDLE removed_event() const noexcept { return removedEvent_.get(); }
    // Starts at 1 and advances with every replacement.
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] const wil::com_ptr<ID3D11Device5>& d3d_device() const noexcept { return d3d_; }
    [[nodiscard]] const wil::com_ptr<ID3D11DeviceContext>& d3d_context() const noexcept { return d3dContext_; }
    [[nodiscard]] const wil::com_ptr<ID2D1Factory7>& d2d_factory() const noexcept { return d2dFactory_; }
    [[nodiscard]] const wil::com_ptr<ID2D1Device6>& d2d_device() const noexcept { return d2d_; }
    // An offscreen Direct2D context, for work outside a surface; surfaces are drawn through the
    // context of a ScopedSurfaceDraw.
    [[nodiscard]] const wil::com_ptr<ID2D1DeviceContext6>& d2d_context() const noexcept { return d2dContext_; }
    [[nodiscard]] const wil::com_ptr<IDWriteFactory7>& text_factory() const noexcept { return textFactory_; }
    [[nodiscard]] const composition::CompositionGraphicsDevice& composition_device() const noexcept { return compositionDevice_; }

private:
    void unregister_device() noexcept;

    composition::Compositor compositor_{nullptr};
    composition::CompositionGraphicsDevice compositionDevice_{nullptr};
    wil::com_ptr<ID2D1Factory7> d2dFactory_;
    wil::com_ptr<IDWriteFactory7> textFactory_;
    wil::com_ptr<ID3D11Device5> d3d_;
    wil::com_ptr<ID3D11DeviceContext> d3dContext_;
    wil::com_ptr<ID2D1Device6> d2d_;
    wil::com_ptr<ID2D1DeviceContext6> d2dContext_;
    wil::unique_handle removedEvent_;
    DWORD removedCookie_{};
    bool registered_{};
    bool forceWarp_{};
    std::uint64_t generation_{};
    Signal<std::uint64_t> recreated_;
};

}
