#include <composia/Appearance.hpp>
#include <composia/Native.hpp>
#include <winrt/Windows.UI.ViewManagement.h>
#include "TestHooks.hpp"

namespace composia {
namespace {
std::uint32_t rgb(COLORREF color) noexcept {
    return (static_cast<std::uint32_t>(GetRValue(color)) << 16) | (static_cast<std::uint32_t>(GetGValue(color)) << 8) | GetBValue(color);
}

std::uint32_t rgb(winrt::Windows::UI::Color color) noexcept {
    return (static_cast<std::uint32_t>(color.R) << 16) | (static_cast<std::uint32_t>(color.G) << 8) | color.B;
}
}

Appearance Appearance::current() noexcept {
#ifdef COMPOSIA_TESTING
    if (detail::appearanceOverride) { return *detail::appearanceOverride; }
#endif
    Appearance result;
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) {
        result.highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
    BOOL animations{TRUE};
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) { result.animations = animations != FALSE; }
    result.colors = {
        .window = rgb(GetSysColor(COLOR_WINDOW)), .windowText = rgb(GetSysColor(COLOR_WINDOWTEXT)),
        .buttonFace = rgb(GetSysColor(COLOR_BTNFACE)), .buttonText = rgb(GetSysColor(COLOR_BTNTEXT)),
        .highlight = rgb(GetSysColor(COLOR_HIGHLIGHT)), .highlightText = rgb(GetSysColor(COLOR_HIGHLIGHTTEXT)),
        .grayText = rgb(GetSysColor(COLOR_GRAYTEXT)), .hotlight = rgb(GetSysColor(COLOR_HOTLIGHT)),
    };
    try {
        const winrt::Windows::UI::ViewManagement::UISettings settings;
        // Light text means a dark app background; Microsoft's guidance for Win32 apps tests it so.
        const auto text = settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Foreground);
        result.dark = 5 * text.G + 2 * text.R + text.B > 8 * 128;
        result.accent = rgb(settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent));
        result.textScale = static_cast<float>(settings.TextScaleFactor());
    } catch (...) {
        // Without UISettings these keep their defaults.
    }
    return result;
}

}
