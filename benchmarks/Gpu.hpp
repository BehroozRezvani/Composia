#pragma once
#include "Scene.hpp"
#include <d2d1.h>
#include <d3d11.h>
#include <wil/com.h>
#include <wil/result.h>

namespace bench {
inline wil::com_ptr<ID3D11Texture2D> texture(ID3D11Device* device, UINT w, UINT h, bool staging = false) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.ArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
    d.CPUAccessFlags = staging ? D3D11_CPU_ACCESS_READ : 0;
    d.BindFlags = staging ? 0 : D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    wil::com_ptr<ID3D11Texture2D> result;
    THROW_IF_FAILED(device->CreateTexture2D(&d, nullptr, result.put()));
    return result;
}
inline unsigned read(ID3D11DeviceContext* ctx, ID3D11Texture2D* staging, ID3D11Texture2D* texture, UINT x, UINT y) {
    const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
    ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, texture, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    THROW_IF_FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    const auto value = *static_cast<const unsigned*>(mapped.pData) & 0xffffff;
    ctx->Unmap(staging, 0);
    return value;
}
template<class Brush>
void rectangles(ID2D1RenderTarget* target, int frame, Brush brush) {
    scene(frame, [&](RECT r, int index) {
        target->FillRectangle(D2D1::RectF(float(r.left), float(r.top), float(r.right), float(r.bottom)), brush(index));
    });
}
}
