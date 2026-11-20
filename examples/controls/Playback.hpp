#pragma once
#include <composia/Application.hpp>
#include <composia/Controls.hpp>
#include <composia/ElementWindow.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextLayout.hpp>
#include <algorithm>
#include <cmath>

// A sample skin. All input, popup management and automation come from Composia::Controls.
namespace playback {
using namespace composia;
namespace ui = composia::controls;

class Panel final : public Element {
public:
    Panel() : Element({ElementLayout::Kind::stack}) {}
    void paint(ScopedSurfaceDraw& d) override {
        const auto b = bounds();
        d.context()->FillRectangle({0, 0, b.width, b.height}, d.solid_brush(0x15282a));
    }
};

class Label {
public:
    Label(Application& app, std::wstring_view text, float size)
        : layout_(app.graphics().text_factory().get(), text, size) {}
    void draw(ScopedSurfaceDraw& d, float x, float y, float width, unsigned color) {
        layout_.resize(std::max(1.f, width), 32);
        d.context()->DrawTextLayout({x, y}, layout_.layout().get(), d.solid_brush(color),
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

private:
    TextLayout layout_;
};

struct Playback {
    std::shared_ptr<ui::Dropdown> output;
    std::shared_ptr<ui::Slider> volume;
    std::shared_ptr<ui::Toggle> mute;
    std::shared_ptr<ui::PopupHost> root;

    explicit Playback(Application& app) {
        auto panel = std::make_shared<Panel>();
        auto names = std::vector<std::wstring>{L"Studio headphones", L"Desk speakers", L"HDMI output"};
        auto labels = std::make_shared<std::vector<Label>>();
        for (const auto& name : names) {
            labels->emplace_back(app, name, 16);
        }
        output = std::make_shared<ui::Dropdown>(L"Output device", ui::Items::strings(std::move(names)), 34, 102);
        output->set_selected(0);
        output->set_preferred_size({260, 68});
        ui::StyleTokens palette{0xe8f4f0, 0x234043, 0x8ee0c1, 12, 5};
        output->set_tokens(palette);
        output->set_popup_tokens(palette);
        output->set_popup_renderer(
            [](ScopedSurfaceDraw& d, const ui::Control& c, ui::State, const ui::StyleTokens&) {
                const auto b = c.bounds();
                d.context()->FillRectangle({0, 0, b.width, b.height}, d.solid_brush(0x15282a));
            });
        output->set_renderer(
            [labels, caption = Label(app, L"Output device", 12)](ScopedSurfaceDraw& d, const ui::Control& c,
                                                                 ui::State s, const ui::StyleTokens& t) mutable {
                const auto& field = static_cast<const ui::Dropdown&>(c);
                const float w = c.bounds().width;
                D2D1_ROUNDED_RECT face{{8, 6, w - 8, 64}, t.radius, t.radius};
                d.context()->FillRoundedRectangle(face, d.solid_brush(t.background));
                if (s.focused) {
                    d.context()->DrawRoundedRectangle(face, d.solid_brush(t.accent), 2);
                }
                caption.draw(d, 20, 11, w - 64, t.accent);
                if (auto i = field.selected()) {
                    labels->at(*i).draw(d, 20, 30, w - 70, t.foreground);
                }
                const float x = w - 30, dy = field.expanded() ? -4.f : 4.f;
                auto brush = d.solid_brush(t.foreground);
                d.context()->DrawLine({x - 5, 35 - dy / 2}, {x, 35 + dy / 2}, brush, 2);
                d.context()->DrawLine({x, 35 + dy / 2}, {x + 5, 35 - dy / 2}, brush, 2);
            });
        output->set_row_renderer([labels](ScopedSurfaceDraw& d, std::size_t i, std::wstring_view, layout::Size size,
                                          ui::State s, const ui::StyleTokens& t) {
            d.context()->FillRectangle({8, 0, size.width - 8, size.height},
                                       d.solid_brush(s.hovered ? 0x375c5c : t.background));
            if (s.selected) {
                d.context()->FillRectangle({8, 3, 12, size.height - 3}, d.solid_brush(t.accent));
            }
            labels->at(i).draw(d, 20, 6, size.width - 40, t.foreground);
        });
        panel->add(output);

        volume = std::make_shared<ui::Slider>(L"Volume", 0, 100, 45);
        volume->set_preferred_size({260, 58});
        volume->set_tokens(palette);
        volume->set_renderer([label = Label(app, L"Volume", 15)](ScopedSurfaceDraw& d, const ui::Control& c,
                                                                 ui::State s, const ui::StyleTokens& t) mutable {
            const auto width = c.bounds().width;
            label.draw(d, 18, 4, width - 36, t.foreground);
            const float x = std::clamp(float(static_cast<const ui::Slider&>(c).value() / 100) * width, 3.f,
                                       std::max(3.f, width - 3));
            const float y = 38;
            d.context()->DrawLine({0, y}, {width, y}, d.solid_brush(t.background), 4);
            d.context()->DrawLine({0, y}, {x, y}, d.solid_brush(t.accent), 4);
            d.context()->FillEllipse({{x, y}, 7, 7}, d.solid_brush(s.focused ? t.foreground : t.accent));
        });
        panel->add(volume);
        mute = std::make_shared<ui::Toggle>(L"Mute audio");
        mute->set_preferred_size({260, 42});
        mute->set_tokens(palette);
        mute->set_renderer([label = Label(app, L"Mute audio", 15)](ScopedSurfaceDraw& d, const ui::Control& c,
                                                                   ui::State s, const ui::StyleTokens& t) mutable {
            label.draw(d, 18, 9, c.bounds().width - 85, t.foreground);
            const float right = c.bounds().width - 18;
            D2D1_ROUNDED_RECT track{{right - 38, 9, right, 31}, 11, 11};
            d.context()->FillRoundedRectangle(track, d.solid_brush(s.selected ? t.accent : t.background));
            if (s.focused) {
                d.context()->DrawRoundedRectangle(track, d.solid_brush(t.accent));
            }
            d.context()->FillEllipse({{right - (s.selected ? 11.f : 27.f), 20}, 7, 7},
                                     d.solid_brush(s.selected ? t.background : t.foreground));
        });
        panel->add(mute);
        root = std::make_shared<ui::PopupHost>(panel);
    }
};
}
