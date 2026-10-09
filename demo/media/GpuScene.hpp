#pragma once

#include <composia/GraphicsDevice.hpp>

class GpuScene {
public:
    explicit GpuScene(composia::GraphicsDevice&);
    void draw(ID3D11RenderTargetView*, SIZE, float seconds);

private:
    wil::com_ptr<ID3D11DeviceContext> context_;
    wil::com_ptr<ID3D11VertexShader> vertex_;
    wil::com_ptr<ID3D11PixelShader> pixel_;
    wil::com_ptr<ID3D11Buffer> constants_;
};
