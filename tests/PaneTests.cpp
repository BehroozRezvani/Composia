#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/NativeControl.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include "support/TestSupport.hpp"
#include <windows.graphics.capture.interop.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

using namespace composia;
using testing::require;
using testing::rejects;

namespace {
using Hosting = CompositionWindowTarget::Hosting;
constexpr std::uint32_t background = 0x202830, listColor = 0x405060, readerColor = 0x183828;

class Pane final : public Window {
public:
    Pane(Application& app, HWND parent = nullptr, std::uint32_t color = background)
        : Window(app, L"Composia pane resize check", 800, 460, Content::composition, parent), target(*this, Hosting::shared), color(color) {}
    CompositionWindowTarget target;
    std::uint32_t color;
    bool rows{};
    unsigned paints{};
    std::function<void()> painting;

private:
    void on_resize() override { invalidate(); }
    void on_paint() override {
        ++paints;
        if (painting) { painting(); }
        target.render(*this, [&](ScopedSurfaceDraw& draw, numerics::float2 size) {
            const auto dc = draw.context();
            dc->Clear(D2D1::ColorF(color));
            if (rows) {
                for (float y = 28; y < size.y; y += 48) {
                    dc->FillRectangle({12, y, 28, y + 16}, draw.solid_brush(0xFFFFFF));
                    dc->FillRectangle({42, y, std::max(43.0f, size.x - 12), y + 3}, draw.solid_brush(0xC0D0E0));
                }
            }
        });
    }
};

void show(Pane& window) {
    SetWindowLongPtrW(window.hwnd(), GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN);
    THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 40, 40, 800, 460,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW));
}

int arguments(const testing::Options& options) {
    Application app{options.warp};
    {
        Window native{app, L"Native", 300, 200};
        require(rejects(E_INVALIDARG, [&] { CompositionWindowTarget bad{native, Hosting::shared}; }), "Shared target accepted a redirected HWND");
        Window orphan{app, L"Orphan", 100, 100, Window::Content::composition, native.hwnd()};
        require(rejects(E_INVALIDARG, [&] { CompositionWindowTarget bad{orphan, Hosting::shared}; }), "Shared child accepted a parent without a host");
        Pane parent{app};
        bool wrongThread{};
        std::thread worker([&] {
            wrongThread = testing::throws<std::logic_error>([&] { app.update(parent, [] {}); });
        });
        worker.join();
        require(wrongThread, "Update accepted a foreign thread");
        require(rejects(E_INVALIDARG, [&] { CompositionWindowTarget duplicate{parent, Hosting::shared}; }), "A second host was accepted");
        require(rejects(E_INVALIDARG, [&] {
            NativeControl bad{parent, L"STATIC", WS_VISIBLE, {}, 0, NativeControl::Painting::buffered_edit};
        }), "Edit buffering accepted another control class");
        Pane child{app, parent.hwnd()};
        const auto hwnd = child.hwnd();
        require(CompositionWindowTarget::hosts_children(hwnd), "Child did not register its host");
        require((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT)) ==
            (WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT), "Shared window styles were not applied");
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(parent.hwnd()));
        require(!child.hwnd() && !CompositionWindowTarget::hosts_children(hwnd), "Parent destruction retained a child host");
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { app.update(child, [] {}); }), "Update accepted a destroyed root");
    }
    app.close();
    return 0;
}

int geometry(const testing::Options& options) {
    Application app{options.warp};
    {
        Pane parent{app};
        Pane first{app, parent.hwnd()}, second{app, parent.hwnd()};
        show(parent);
        app.update(parent, [&] {
            first.set_bounds({17, 63, 201, 137});
            second.set_bounds({90, 80, 110, 120});
        });
        const auto frame = first.target.root().Parent();
        RECT bounds{};
        GetWindowRect(first.hwnd(), &bounds);
        POINT origin{bounds.left, bounds.top};
        ScreenToClient(parent.hwnd(), &origin);
        require(frame.Offset().x == origin.x && frame.Offset().y == origin.y, "Child visual and HWND positions differ");
        require(frame.Size().x == bounds.right - bounds.left && frame.Size().y == bounds.bottom - bounds.top, "Child clip size differs from the HWND");
        for (const UINT dpi : {96u, 120u, 144u, 192u}) {
            first.target.resize({201, 137}, dpi);
            require(std::abs(first.target.root().Scale().x - static_cast<float>(dpi) / 96) < 0.001f, "Target DPI scale is wrong");
            require(frame.Offset().x == origin.x && frame.Scale().x == 1, "DPI was applied twice to the pane position");
        }
        const auto region = CreateRectRgn(9, 4, 80, 90);
        THROW_IF_WIN32_BOOL_FALSE(SetWindowRgn(first.hwnd(), region, TRUE));
        app.update(parent, [] {});
        require(frame.Clip().as<composition::InsetClip>().LeftInset() == 9, "HWND region was not clipped in composition");
        first.show(SW_HIDE);
        require(!frame.IsVisible(), "Hidden pane remained visible in composition");
        first.show(SW_SHOWNOACTIVATE);
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(first.hwnd(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        composition::Visual top{nullptr};
        for (const auto visual : parent.target.target().Root().as<composition::ContainerVisual>().Children()) { top = visual; }
        require(top == frame, "Composition did not follow sibling stacking order");
    }
    app.close();
    return 0;
}

int updates(const testing::Options& options) {
    Application app{options.warp};
    {
        Pane parent{app}, child{app, parent.hwnd()};
        show(parent);
        app.update(parent, [&] { child.set_bounds({0, 0, 200, 200}); });
        const auto before = child.paints;
        app.update(parent, [&] {
            app.update(child, [&] { child.set_bounds({20, 40, 300, 210}); });
            require(child.paints == before, "Nested update painted before outer layout completed");
            app.commit();
            child.color = readerColor;
        });
        require(child.paints > before, "Update left its pending child paint in the queue");
        require(testing::matches(testing::surface_pixel(app, child.target.surface(), 5, 5), readerColor), "Update painted stale application state");
        require(testing::throws<std::runtime_error>([&] {
            app.update(parent, [] { throw std::runtime_error("layout failure"); });
        }), "Layout failure was swallowed");
        app.update(parent, [&] { child.color = listColor; child.invalidate(); });
        require(testing::matches(testing::surface_pixel(app, child.target.surface(), 5, 5), listColor), "Failed update left future commits suspended");
        child.painting = [] { throw std::runtime_error("paint failure"); };
        require(testing::throws<std::runtime_error>([&] { app.update(child, [] {}); }), "Paint failure was swallowed");
        child.painting = {};
        app.update(child, [] {});
        unsigned recovery{};
        const auto parentPaints = parent.paints;
        child.painting = [&] { if (recovery++ == 0) { app.graphics().recreate(); } };
        app.update(parent, [&] { child.invalidate(); });
        require(parent.paints >= parentPaints + 2, "Device replacement left an earlier pane unpainted");
        child.painting = {};
        child.painting = [&] { child.invalidate(); };
        require(testing::throws<std::logic_error>([&] { app.update(child, [] {}); }), "Unbounded paint invalidation was not rejected");
        child.painting = {};
        app.update(child, [] {});
    }
    app.close();
    return 0;
}

int restrictions(const testing::Options& options) {
    Application app{options.warp};
    {
        Pane parent{app}, other{app}, child{app, parent.hwnd()};
        show(parent);
        app.update(parent, [&] { child.set_bounds({0, 0, 200, 200}); });
        require(rejects(E_INVALIDARG, [&] {
            app.update(parent, [&] {
                const auto region = CreateEllipticRgn(0, 0, 160, 160);
                THROW_IF_WIN32_BOOL_FALSE(SetWindowRgn(child.hwnd(), region, TRUE));
            });
        }), "Nonrectangular clipping silently became a bounding rectangle");
        SetWindowRgn(child.hwnd(), nullptr, TRUE);
        app.update(parent, [] {});
        require(rejects(E_INVALIDARG, [&] {
            app.update(parent, [&] {
                SetParent(child.hwnd(), other.hwnd());
                child.set_bounds({1, 1, 200, 200});
            });
        }), "Reparenting silently kept the old visual parent");
    }
    app.close();
    return 0;
}

// Whole-window readback includes the native EDIT and HWND clipping, unlike CreateFromVisual.
struct Image {
    int width{}, height{};
    std::vector<std::uint32_t> pixels;
    Image(Application& app, const capture::Direct3D11CaptureFrame& frame) {
        const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        wil::com_ptr<ID3D11Texture2D> texture, staging;
        THROW_IF_FAILED(access->GetInterface(IID_PPV_ARGS(texture.put())));
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        THROW_IF_FAILED(app.graphics().d3d_device()->CreateTexture2D(&desc, nullptr, staging.put()));
        const auto context = app.graphics().d3d_context().get();
        context->CopyResource(staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        THROW_IF_FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto unmap = wil::scope_exit([&] { context->Unmap(staging.get(), 0); });
        width = frame.ContentSize().Width;
        height = frame.ContentSize().Height;
        pixels.resize(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y) {
            const auto row = reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
            std::copy_n(row, width, pixels.begin() + static_cast<std::size_t>(y) * width);
        }
    }
    std::uint32_t at(int x, int y) const { return pixels.at(static_cast<std::size_t>(y) * width + x) & 0xFFFFFF; }
};

int capture_panes(const testing::Options& options) {
    if (!ScreenCapture::supported()) { return testing::skipped; }
    Application app{options.warp};
    {
        Pane parent{app}, left{app, parent.hwnd(), listColor}, right{app, parent.hwnd(), readerColor};
        left.rows = true;
        NativeControl search{parent, L"EDIT", WS_VISIBLE | ES_AUTOHSCROLL, L"Search mail 0123456789", 0, NativeControl::Painting::buffered_edit};
        Button action{parent, L"Action"};
        require(action.composition_target().root().Parent().Parent() == parent.target.target().Root(), "Button did not join the shared host");
        search.set_colors(RGB(240, 240, 240), RGB(44, 44, 44));
        show(parent);
        const auto scale = parent.scale();
        action.set_bounds({700 / scale, 12 / scale, 80 / scale, 36 / scale});
        const auto arrange = [&](int split) {
            app.update(parent, [&] {
                left.set_bounds({0, 64 / scale, split / scale, 380 / scale});
                right.set_bounds({split / scale, 64 / scale, (800 - split) / scale, 380 / scale});
                search.set_bounds({20 / scale, 16 / scale, (split - 40) / scale, 28 / scale});
            });
        };
        arrange(300);
        const auto factory = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        capture::GraphicsCaptureItem item{nullptr};
        THROW_IF_FAILED(factory->CreateForWindow(parent.hwnd(), winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
        ScreenCapture capture;
        capture.start(item, app.graphics().d3d_device().get());
        capture.session().IsCursorCaptureEnabled(false);
        std::vector<std::uint32_t> text;
        unsigned frames{};
        const auto inspect = [&] {
            const auto frame = capture.next_frame();
            if (!frame) { return; }
            const auto close = wil::scope_exit([&] { frame.Close(); });
            Image image{app, frame};
            require(image.width == 800 && image.height == 460, "Unexpected whole-window capture bounds");
            if (frames == 0 && image.at(2, 70) != listColor) { return; }
            require(image.at(2, 70) == listColor, "An incomplete pane update became visible");
            int edge = 0;
            while (edge < 800 && image.at(edge, 70) == listColor) { ++edge; }
            require(edge >= 180 && edge <= 600, "List and reader did not meet at a valid divider");
            for (int y = 70; y < 440; y += 3) {
                for (int x = edge; x < std::min(edge + 36, 800); ++x) {
                    require(image.at(x, y) == readerColor, "Stale list content or a gap appeared under the reader");
                }
            }
            std::vector<std::uint32_t> crop;
            for (int y = 18; y < 40; ++y) {
                for (int x = 22; x < 130; ++x) { crop.push_back(image.at(x, y)); }
            }
            if (text.empty()) { text = crop; }
            else { require(text == crop, "Native search text changed pixels during pane resizing"); }
            ++frames;
        };
        const auto ready = GetTickCount64() + 3000;
        while (!frames && GetTickCount64() < ready) { inspect(); Sleep(5); }
        require(frames != 0, "Whole-window capture never presented the initial panes");
        app.update(parent, [&] {
            left.target.root().Offset({0, 40, 0});
            app.update(left, [&] { app.commit(); });
            const auto until = GetTickCount64() + 120;
            while (GetTickCount64() < until) { inspect(); Sleep(5); }
            left.target.root().Offset({0, 0, 0});
        });
        for (int pass = 0; pass < 2; ++pass) {
            for (int split = 180; split <= 600; split += 7) {
                SendMessageW(action.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(4, 4));
                arrange(split); inspect(); Sleep(8);
            }
            for (int split = 600; split >= 180; split -= 7) { arrange(split); inspect(); Sleep(8); }
        }
        require(frames >= 20, "Too few moving frames were checked");
        search.send(EM_SETSEL, 0, -1);
        search.send(EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Changed"));
        require(search.text() == L"Changed", "Buffered edit lost native text editing");
        search.send(EM_UNDO);
        require(search.text() == L"Search mail 0123456789", "Buffered edit lost undo");
        capture.close();
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"arguments", arguments}, {"geometry", geometry}, {"updates", updates},
        {"restrictions", restrictions}, {"capture", capture_panes}});
}
