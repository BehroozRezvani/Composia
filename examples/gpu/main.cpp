#include <composia/AnimationHelpers.hpp>
#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/SwapChainSurface.hpp>
#include <composia/TextLayout.hpp>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace composia;

namespace {

constexpr char shaderSource[] = R"(
cbuffer Frame : register(b0) { float4 frame; };  // cos and sin of the angle, aspect correction
struct Vertex { float2 position : POSITION; float3 color : COLOR; };
struct Pixel { float4 position : SV_POSITION; float3 color : COLOR; };
Pixel vs(Vertex input) {
    float2 p = float2(input.position.x * frame.x - input.position.y * frame.y,
        input.position.x * frame.y + input.position.y * frame.x);
    Pixel output;
    output.position = float4(p.x * frame.z, p.y, 0, 1);
    output.color = input.color;
    return output;
}
float4 ps(Pixel input) : SV_TARGET { return float4(input.color, 1); }
)";

wil::com_ptr<ID3DBlob> compile(const char* entry, const char* profile) {
    wil::com_ptr<ID3DBlob> code, errors;
    THROW_IF_FAILED(D3DCompile(shaderSource, sizeof(shaderSource) - 1, "triangle", nullptr, nullptr, entry, profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put()));
    return code;
}

// The Direct3D objects that draw a colored triangle. They belong to one device, so they are built
// again for a replacement device; the compiled shaders do not depend on the device.
class Triangle {
public:
    Triangle() : vertexCode_(compile("vs", "vs_4_0")), pixelCode_(compile("ps", "ps_4_0")) {}

    void draw(GraphicsDevice& graphics, ID3D11RenderTargetView* target, SIZE size, float angle) {
        if (generation_ != graphics.generation()) { build(graphics.d3d_device().get(), graphics.generation()); }
        const auto context = graphics.d3d_context().get();
        const float background[]{0.094f, 0.137f, 0.18f, 1};
        context->ClearRenderTargetView(target, background);
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(size.cx), static_cast<float>(size.cy), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(nullptr, 0);
        context->OMSetRenderTargets(1, &target, nullptr);
        const float frame[]{std::cos(angle), std::sin(angle), static_cast<float>(size.cy) / static_cast<float>(size.cx), 0};
        context->UpdateSubresource(constants_.get(), 0, nullptr, frame, 0, 0);
        constexpr UINT stride = 5 * sizeof(float), offset = 0;
        ID3D11Buffer* const vertices[]{vertices_.get()};
        ID3D11Buffer* const constants[]{constants_.get()};
        context->IASetVertexBuffers(0, 1, vertices, &stride, &offset);
        context->IASetInputLayout(layout_.get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader_.get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, constants);
        context->PSSetShader(pixelShader_.get(), nullptr, 0);
        context->Draw(3, 0);
    }

private:
    void build(ID3D11Device* device, std::uint64_t generation) {
        THROW_IF_FAILED(device->CreateVertexShader(vertexCode_->GetBufferPointer(), vertexCode_->GetBufferSize(), nullptr, vertexShader_.put()));
        THROW_IF_FAILED(device->CreatePixelShader(pixelCode_->GetBufferPointer(), pixelCode_->GetBufferSize(), nullptr, pixelShader_.put()));
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        THROW_IF_FAILED(device->CreateInputLayout(elements, 2, vertexCode_->GetBufferPointer(), vertexCode_->GetBufferSize(), layout_.put()));
        constexpr float corners[]{0.0f, 0.8f, 0.435f, 0.902f, 0.784f, 0.69f, -0.6f, 0.38f, 0.537f, 0.94f, -0.69f, -0.6f, 0.94f, 0.42f, 0.69f};
        const D3D11_BUFFER_DESC vertexDesc{sizeof(corners), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        const D3D11_SUBRESOURCE_DATA data{corners, 0, 0};
        THROW_IF_FAILED(device->CreateBuffer(&vertexDesc, &data, vertices_.put()));
        const D3D11_BUFFER_DESC constantDesc{16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        THROW_IF_FAILED(device->CreateBuffer(&constantDesc, nullptr, constants_.put()));
        generation_ = generation;
    }

    wil::com_ptr<ID3DBlob> vertexCode_, pixelCode_;
    wil::com_ptr<ID3D11VertexShader> vertexShader_;
    wil::com_ptr<ID3D11PixelShader> pixelShader_;
    wil::com_ptr<ID3D11InputLayout> layout_;
    wil::com_ptr<ID3D11Buffer> vertices_, constants_;
    std::uint64_t generation_{};
};

IDWriteFactory7* text_factory(Application& app) { return app.graphics().text_factory().get(); }

}

// Direct3D 11 content in the visual tree, on Windows 10 and later, without a render loop. Dragging
// or the arrow keys turn the triangle, and each change presents one frame; so do a resize and a
// graphics device replacement. Composition keeps the last frame on screen and runs the rounded clip
// and the pulsing dot on its own, so the frame count stays still while nothing changes.
class GpuWindow final : public Window {
public:
    explicit GpuWindow(Application& app)
        : Window(app, L"Composia GPU content", 900, 620),
          target_(app.compositor(), app.graphics(), hwnd()),
          frame_(app, {1, 1}),
          title_(text_factory(app), L"GPU content", 28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD),
          subtitle_(text_factory(app),
              L"Direct3D 11 draws into a swap chain that sits in the visual tree. Drag or press the arrow keys to turn the triangle: "
              L"each change presents one frame, and Composition keeps it on screen. The rounded clip and the pulsing dot run on the "
              L"compositor without new frames.", 14.0f),
          status_(text_factory(app), L"", 14.0f) {
        auto compositor = app.compositor();
        panel_ = compositor.CreateSpriteVisual();
        panel_.Brush(frame_.brush());
        clipShape_ = compositor.CreateRoundedRectangleGeometry();
        clipShape_.CornerRadius({14, 14});
        panel_.Clip(compositor.CreateGeometricClip(clipShape_));
        target_.root().Children().InsertAtTop(panel_);
        live_ = animations::sprite(compositor, {12, 12}, {255, 111, 230, 200});
        auto dot = compositor.CreateRoundedRectangleGeometry();
        dot.Size({12, 12});
        dot.CornerRadius({6, 6});
        live_.Clip(compositor.CreateGeometricClip(dot));
        animations::pulse(live_);
        target_.root().Children().InsertAtTop(live_);
        arrange();
    }

protected:
    void on_resize() override {
        arrange();
        invalidate();
    }

    void on_paint() override {
        target_.render(*this, [this](ScopedSurfaceDraw& draw, numerics::float2 size) { draw_canvas(draw, size); });
        if (stale_) { present(); }
    }

    // Recovery invalidates every window; the next paint presents on the new device.
    void on_graphics_recreated() override { stale_ = true; }

    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM lparam) override {
        switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
            info->ptMinTrackSize = {MulDiv(480, windowDpi, 96), MulDiv(420, windowDpi, 96)};
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;  // The dialog navigation in the message loop would take arrow keys otherwise.
        case WM_LBUTTONDOWN: {
            const auto point = pointer_position(lparam);
            if (!panelArea_.contains(point.x, point.y)) { break; }
            capture_pointer();
            dragX_ = point.x;
            return 0;
        }
        case WM_MOUSEMOVE:
            if (pointer_captured()) {
                const auto x = pointer_position(lparam).x;
                turn((x - dragX_) * 0.01f);
                dragX_ = x;
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            release_pointer();
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                turn(wparam == VK_LEFT ? -0.1f : 0.1f);
                return 0;
            }
            break;
        default:
            break;
        }
        return std::nullopt;
    }

private:
    void arrange() {
        const auto bounds = client_bounds();
        panelArea_ = {32, 120, std::max(1.0f, bounds.width - 64), std::max(1.0f, bounds.height - 184)};
        statusArea_ = {32, panelArea_.y + panelArea_.height + 16, std::max(1.0f, bounds.width - 64), 24};
        panel_.Offset({panelArea_.x, panelArea_.y, 0});
        panel_.Size({panelArea_.width, panelArea_.height});
        clipShape_.Size({panelArea_.width, panelArea_.height});
        live_.Offset({bounds.width - 44, 38, 0});
        // The swap chain matches the panel in pixels, so Composition shows it unscaled.
        const auto scale = this->scale();
        frame_.resize({std::max(1L, std::lround(panelArea_.width * scale)), std::max(1L, std::lround(panelArea_.height * scale))});
        stale_ = true;
    }

    void turn(float radians) {
        angle_ += radians;
        present();
    }

    void present() {
        frame_.present([this](ID3D11RenderTargetView* target, ID3D11Texture2D*) {
            triangle_.draw(application().graphics(), target, frame_.size(), angle_);
        });
        stale_ = false;
        ++frames_;
        status_ = TextLayout{text_factory(application()), L"Frames presented: " + std::to_wstring(frames_), 14.0f};
        invalidate(statusArea_);
    }

    void draw_canvas(ScopedSurfaceDraw& draw, numerics::float2 size) {
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(0x101923));
        const auto text = [&](TextLayout& layout, layout::Rect area, UINT32 color) {
            layout.resize(std::max(1.0f, area.width), std::max(1.0f, area.height));
            dc->DrawTextLayout({area.x, area.y}, layout.layout().get(), draw.solid_brush(color));
        };
        text(title_, {32, 24, size.x - 96, 40}, 0xEAF2F4);
        text(subtitle_, {32, 64, size.x - 64, 48}, 0x93A9B5);
        text(status_, statusArea_, 0x6FE6C8);
    }

    CompositionWindowTarget target_;
    SwapChainSurface frame_;
    Triangle triangle_;
    TextLayout title_, subtitle_, status_;
    composition::SpriteVisual panel_{nullptr}, live_{nullptr};
    composition::CompositionRoundedRectangleGeometry clipShape_{nullptr};
    layout::Rect panelArea_{}, statusArea_{};
    float angle_{};
    float dragX_{};
    unsigned frames_{};
    bool stale_{true};
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int showCommand) {
    const std::wstring_view arguments{commandLine};
    const bool warp = arguments.find(L"--warp") != std::wstring_view::npos;
    try {
        Application app{warp};
        int result{};
        {
            GpuWindow window{app};
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const winrt::hresult_error&) {
    } catch (const std::exception&) {
    } catch (...) {
    }
    MessageBoxW(nullptr, L"Composia could not continue.", L"Composia GPU content", MB_OK | MB_ICONERROR);
    return 1;
}
