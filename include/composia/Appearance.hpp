#pragma once

#include <cstdint>

namespace composia {

// What Windows tells applications about how they should look. Colors are 0xRRGGBB, the form
// ScopedSurfaceDraw::solid_brush and D2D1::ColorF take. Composia draws nothing with it except
// Button's default look: the application decides how to follow it. Application::appearance()
// holds the current settings and reports changes.
struct Appearance {
    // The classic system colors (GetSysColor). Under high contrast they are the theme's colors,
    // and the only ones an application should draw with.
    struct Colors {
        std::uint32_t window{}, windowText{};          // Content background and text.
        std::uint32_t buttonFace{}, buttonText{};      // Controls.
        std::uint32_t highlight{}, highlightText{};    // Selection, and hot or pressed controls.
        std::uint32_t grayText{};                      // Disabled text.
        std::uint32_t hotlight{};                      // Links.
        bool operator==(const Colors&) const = default;
    };

    bool dark{};                      // Apps use dark colors (Settings > Personalization > Colors).
    bool highContrast{};              // A contrast theme is on: draw with colors only.
    bool animations{true};            // Animation effects are on (Settings > Accessibility).
    float textScale{1.0f};            // The text size setting, 1 to 2.25 (Settings > Accessibility).
    std::uint32_t accent{0x0078D4};   // The accent color.
    Colors colors{};

    // Reads the settings now. Dark mode, the accent, and the text scale come from UISettings,
    // which needs COM on the calling thread, as the Application's UI thread has; without it they
    // keep their defaults. Never throws.
    [[nodiscard]] static Appearance current() noexcept;
    bool operator==(const Appearance&) const = default;
};

}
