#pragma once

#include <composia/Platform.hpp>
#include <composia/Signal.hpp>
#include <cstdint>

namespace composia {

class GraphicsDevice {
public:
    explicit GraphicsDevice(const composition::Compositor&, bool forceWarp = false);
    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    void recreate();
    Connection on_recreated(std::function<void(std::uint64_t)> callback) { return recreated_.connect(std::move(callback)); }
    [[nodiscard]] bool is_device_loss(HRESULT error) const noexcept;
    [[nodiscard]] HANDLE removed_event() const noexcept { return removedEvent_.get(); }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] const wil::com_ptr<ID3D11Device5>& d3d_device() const noexcept { return d3d_; }
    [[nodiscard]] const wil::com_ptr<ID3D11DeviceContext>& d3d_context() const noexcept { return d3dContext_; }
    [[nodiscard]] const wil::com_ptr<ID2D1Factory7>& d2d_factory() const noexcept { return d2dFactory_; }
    [[nodiscard]] const wil::com_ptr<ID2D1Device6>& d2d_device() const noexcept { return d2d_; }
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
