#include <composia/Layout.hpp>
#include <composia/Signal.hpp>
#include <composia/TextLayout.hpp>
#include "support/TestSupport.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

// The parts of Composia that need no window, graphics device, or desktop: signals, layout, and
// text measurement.
using namespace composia;
using testing::require;
using testing::throws;

namespace {
int signals(const testing::Options&) {
    Signal<int> signal;
    int observed{};
    auto listener = signal.connect([&](int value) { observed += value; });
    signal.emit(3);
    require(observed == 3, "Connected listener did not run");
    auto moved = std::move(listener);
    signal.emit(4);
    require(observed == 7, "Moving the connection disconnected it");
    moved.disconnect();
    moved.disconnect();  // Disconnecting twice is harmless.
    signal.emit(10);
    require(observed == 7, "Disconnected listener ran");
    {
        auto scoped = signal.connect([&](int value) { observed += value; });
        signal.emit(1);
    }
    signal.emit(100);
    require(observed == 8, "A destroyed Connection did not disconnect its listener");
    Connection reassigned = signal.connect([&](int value) { observed += value; });
    reassigned = signal.connect([&](int value) { observed -= value; });
    signal.emit(1);
    require(observed == 7, "Reassigning a Connection did not disconnect its previous listener");
    auto& alias = reassigned;
    reassigned = std::move(alias);
    signal.emit(1);
    require(observed == 6, "Self-assignment disconnected the listener");
    Connection empty;
    empty.disconnect();

    Connection second;
    auto first = signal.connect([&](int) { second.disconnect(); });
    second = signal.connect([](int) { throw std::runtime_error("Listener removed during dispatch ran"); });
    signal.emit(1);

    Signal<> failureSignal;
    bool secondNotified{};
    auto failing = failureSignal.connect([] { throw std::runtime_error("subscriber failure"); });
    auto following = failureSignal.connect([&] { secondNotified = true; });
    require(throws<std::runtime_error>([&] { failureSignal.emit(); }) && secondNotified,
        "Subscriber failure prevented remaining notifications");

    Signal<bool> stateful;
    std::vector<int> counts;
    auto counter = stateful.connect([&, count = 0](bool nested) mutable {
        counts.push_back(++count);
        if (nested) { stateful.emit(false); }
    });
    stateful.emit(true);
    stateful.emit(false);
    require(counts == std::vector{1, 2, 3}, "Callback state was lost between nested or successive emissions");

    Signal<> changing;
    Connection original, replacement;
    std::vector<int> calls;
    original = changing.connect([&] {
        original.disconnect();
        replacement = changing.connect([&, count = 0]() mutable { calls.push_back(++count); });
        calls.push_back(0);
    });
    changing.emit();
    require(calls == std::vector{0}, "A new callback ran in the emission that connected it");
    changing.emit();
    changing.emit();
    require(calls == std::vector{0, 1, 2}, "Replacing a callback during emission lost its state or lifetime");
    return 0;
}

int stacking(const testing::Options&) {
    const auto rows = layout::stack({10, 20, 100, 90}, std::array{40.0f, 60.0f}, 10);
    require(rows.size() == 2 && rows[0].y == 20 && rows[0].height == 40 && rows[1].y == 70 && rows[1].height == 40 && rows[1].width == 100,
        "Vertical layout failed to respect available space");
    const auto columns = layout::stack({10, 20, 90, 100}, std::array{40.0f, 60.0f}, 10, layout::Axis::horizontal);
    require(columns[1].x == 60 && columns[1].width == 40 && columns[1].height == 100, "Horizontal layout failed to respect available space");
    const auto crowded = layout::stack({0, 0, 50, 70}, std::array{40.0f, 40.0f, 40.0f}, 10);
    require(crowded[1].y == 50 && crowded[1].height == 20 && crowded[2].y == 70 && crowded[2].height == 0,
        "Items past the far edge were not shortened, down to zero");
    require(layout::stack({0, 0, 10, 10}, std::span<const float>{}).empty(), "No lengths did not give no items");
    require(rows[0].contains(10, 20) && !rows[0].contains(110, 20) && !rows[0].contains(9, 20) && !rows[0].contains(10, 60),
        "Hit-test boundaries are incorrect");
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    const auto invalid = [](auto&& action) { return throws<std::invalid_argument>(action); };
    require(invalid([&] { (void)layout::stack({0, 0, 100, 100}, std::array{infinity}); }), "A non-finite length was accepted");
    require(invalid([&] { (void)layout::stack({0, 0, 100, 100}, std::array{-1.0f}); }), "A negative length was accepted");
    require(invalid([&] { (void)layout::stack({std::nanf(""), 0, 100, 100}, std::array{1.0f}); }), "A non-finite origin was accepted");
    require(invalid([&] { (void)layout::stack({0, 0, -1, 100}, std::array{1.0f}); }), "A negative width was accepted");
    require(invalid([&] { (void)layout::stack({0, 0, 100, 100}, std::array{1.0f}, -2); }), "A negative gap was accepted");
    return 0;
}

int text(const testing::Options&) {
    // Text measurement needs DirectWrite only, not a window or a graphics device.
    wil::com_ptr<IDWriteFactory7> factory;
    THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory7), reinterpret_cast<IUnknown**>(factory.put())));
    TextLayout shortText{factory.get(), L"Hi", 20};
    TextLayout longText{factory.get(), L"Hello, wide world", 20};
    shortText.resize(1000, 200);
    longText.resize(1000, 200);
    const auto shortSize = shortText.metrics(), longSize = longText.metrics();
    require(shortSize.width > 0 && longSize.width > shortSize.width && shortSize.lineCount == 1 && std::abs(shortSize.height - longSize.height) <= 0.01f,
        "Single-line text was not measured");
    longText.resize(60, 400);
    const auto wrapped = longText.metrics();
    require(wrapped.lineCount >= 2 && wrapped.height > longSize.height && wrapped.width <= 60.5f, "Wrapped text was not measured at the new width");
    require(longText.layout()->GetMaxWidth() == 60 && longText.layout()->GetMaxHeight() == 400, "The layout box was not applied");

    TextLayout proportional{factory.get(), L"iiiiiiii", 20};
    TextLayout monospaced{factory.get(), L"iiiiiiii", 20, DWRITE_FONT_WEIGHT_BOLD, L"Consolas", L"fr-FR"};
    proportional.resize(1000, 200);
    monospaced.resize(1000, 200);
    require(monospaced.metrics().width > proportional.metrics().width * 1.5f, "The requested font family was not used");
    const auto& format = monospaced.format();
    wchar_t family[32]{}, locale[16]{};
    THROW_IF_FAILED(format->GetFontFamilyName(family, 32));
    THROW_IF_FAILED(format->GetLocaleName(locale, 16));
    // DirectWrite reports locale names in lowercase.
    require(format->GetFontSize() == 20 && format->GetFontWeight() == DWRITE_FONT_WEIGHT_BOLD &&
        std::wstring_view{family} == L"Consolas" && _wcsicmp(locale, L"fr-FR") == 0,
        "The text format did not keep the size, weight, family, and locale");

    const auto invalid = [](auto&& action) { return testing::rejects(E_INVALIDARG, action); };
    require(invalid([&] { TextLayout layout{factory.get(), L"x", 12, DWRITE_FONT_WEIGHT_NORMAL, L""}; }), "An empty font family was accepted");
    require(invalid([&] { TextLayout layout{nullptr, L"x", 12}; }), "A missing factory was accepted");
    require(invalid([&] { TextLayout layout{factory.get(), L"x", 0}; }), "A zero font size was accepted");
    require(invalid([&] { TextLayout layout{factory.get(), L"x", std::nanf("")}; }), "A non-finite font size was accepted");
    require(invalid([&] { shortText.resize(0, 10); }) && invalid([&] { shortText.resize(10, -1); }), "An empty layout box was accepted");
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"signal", signals},
        {"layout", stacking},
        {"text", text},
    });
}
