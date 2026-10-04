#include "GpuScene.hpp"
#include <d3dcompiler.h>
#include <stdexcept>

namespace {
constexpr char shader[] = R"hlsl(
cbuffer Frame : register(b0) { float time; float aspect; float2 padding; };
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    Vertex output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float4 ps(Vertex input) : SV_Target {
    float2 uv = input.uv;
    float2 p = (uv - .5) * float2(aspect, 1);
    float3 color = lerp(float3(.025, .05, .12), float3(.12, .07, .24), uv.y);
    float2 sun = p - float2(.32 + .04 * sin(time * .19), -.09);
    float radius = length(sun);
    color += float3(.16, .34, .43) * exp(-radius * 4);
    float disc = 1 - smoothstep(.132, .136, radius);
    color = lerp(color, lerp(float3(.7, .96, .85), float3(.13, .53, .7), saturate(sun.y * 4 + .5)), disc);
    for (int i = 0; i < 5; ++i) {
        float n = (float)i;
        float curve = .24 * sin(p.x * 2.6 + time * .23 + n * .45)
                    + .09 * sin(p.x * 6.2 - time * .17 + n) + n * .042;
        float ribbon = exp(-abs(p.y - curve) * (36 + n * 5));
        float3 tint = lerp(float3(.14, .7, .68), float3(.48, .24, .8), n / 4);
        color += tint * ribbon * .34;
    }
    float2 grid = floor(uv * float2(180, 100));
    float star = step(.997, hash(grid));
    float2 cell = frac(uv * float2(180, 100)) - .5;
    color += star * exp(-dot(cell, cell) * 40) * (.3 + .2 * sin(time + hash(grid) * 30));
    float ridge = .30 + .06 * sin(p.x * 3 + .5) + .03 * sin(p.x * 8);
    color = lerp(color, float3(.025, .055, .09), smoothstep(ridge, ridge + .005, p.y));
    float foreground = .41 + .06 * sin(p.x * 3 - .8);
    color = lerp(color, float3(.015, .03, .055), smoothstep(foreground, foreground + .003, p.y));
    color *= 1 - .25 * dot(uv - .5, uv - .5);
    return float4(saturate(color), 1);
}
)hlsl";

wil::com_ptr<ID3DBlob> compile(const char* entry, const char* profile) {
    wil::com_ptr<ID3DBlob> code, errors;
    const auto result = D3DCompile(shader, sizeof(shader) - 1, "Composia.GpuScene", nullptr, nullptr,
        entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
    if (FAILED(result) && errors) { throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer())); }
    THROW_IF_FAILED(result);
    return code;
}
}

GpuScene::GpuScene(composia::GraphicsDevice& graphics) : context_(graphics.d3d_context()) {
    const auto device = graphics.d3d_device().get();
    const auto vs = compile("vs", "vs_5_0"), ps = compile("ps", "ps_5_0");
    THROW_IF_FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex_.put()));
    THROW_IF_FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixel_.put()));
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 16;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    THROW_IF_FAILED(device->CreateBuffer(&desc, nullptr, constants_.put()));
}

void GpuScene::draw(ID3D11RenderTargetView* target, SIZE size, float seconds) {
    const float values[]{seconds, static_cast<float>(size.cx) / static_cast<float>(size.cy), 0, 0};
    context_->UpdateSubresource(constants_.get(), 0, nullptr, values, 0, 0);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(size.cx), static_cast<float>(size.cy), 0, 1};
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(nullptr);
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context_->OMSetDepthStencilState(nullptr, 0);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.get(), nullptr, 0);
    context_->PSSetShader(pixel_.get(), nullptr, 0);
    const auto buffer = constants_.get();
    context_->PSSetConstantBuffers(0, 1, &buffer);
    context_->Draw(3, 0);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->Flush();
}
