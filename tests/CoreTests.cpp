#include <composia/Signal.hpp>
#include <composia/Layout.hpp>
#include <array>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        composia::Signal<int> signal;
        int observed{};
        auto listener = signal.connect([&](int value) { observed += value; });
        signal.emit(3);
        if (observed != 3) { throw std::runtime_error("Connected listener did not run"); }
        auto moved = std::move(listener);
        signal.emit(4);
        if (observed != 7) { throw std::runtime_error("Moving the connection disconnected it"); }
        moved.disconnect();
        signal.emit(10);
        if (observed != 7) { throw std::runtime_error("Disconnected listener ran"); }
        composia::Connection second;
        auto first = signal.connect([&](int) { second.disconnect(); });
        second = signal.connect([](int) { throw std::runtime_error("Listener removed during dispatch ran"); });
        signal.emit(1);
        composia::Signal<> failureSignal;
        bool secondNotified{}, errorPropagated{};
        auto failing = failureSignal.connect([] { throw std::runtime_error("subscriber failure"); });
        auto following = failureSignal.connect([&] { secondNotified = true; });
        try { failureSignal.emit(); }
        catch (const std::runtime_error&) { errorPropagated = true; }
        if (!secondNotified || !errorPropagated) { throw std::runtime_error("Subscriber failure prevented remaining notifications"); }
        composia::Signal<bool> stateful;
        std::vector<int> counts;
        auto counter = stateful.connect([&, count = 0](bool nested) mutable {
            counts.push_back(++count);
            if (nested) { stateful.emit(false); }
        });
        stateful.emit(true);
        stateful.emit(false);
        if (counts != std::vector{1, 2, 3}) {
            throw std::runtime_error("Callback state was lost between nested or successive emissions");
        }
        composia::Signal<> changing;
        composia::Connection original, replacement;
        std::vector<int> calls;
        original = changing.connect([&] {
            original.disconnect();
            replacement = changing.connect([&, count = 0]() mutable { calls.push_back(++count); });
            calls.push_back(0);
        });
        changing.emit();
        if (calls != std::vector{0}) { throw std::runtime_error("A new callback ran in the emission that connected it"); }
        changing.emit();
        changing.emit();
        if (calls != std::vector{0, 1, 2}) {
            throw std::runtime_error("Replacing a callback during emission lost its state or lifetime");
        }
        const auto rows = composia::layout::stack({10, 20, 100, 90}, std::array{40.0f, 60.0f}, 10);
        if (rows.size() != 2 || rows[1].y != 70 || rows[1].height != 40 || rows[1].width != 100) {
            throw std::runtime_error("Vertical layout failed to respect available space");
        }
        const auto columns = composia::layout::stack({10, 20, 90, 100}, std::array{40.0f, 60.0f}, 10, composia::layout::Axis::horizontal);
        if (columns[1].x != 60 || columns[1].width != 40 || columns[1].height != 100) {
            throw std::runtime_error("Horizontal layout failed to respect available space");
        }
        if (!rows[0].contains(10, 20) || rows[0].contains(110, 20) || rows[0].contains(9, 20)) {
            throw std::runtime_error("Hit-test boundaries are incorrect");
        }
        bool invalidRejected{};
        try { (void)composia::layout::stack({0, 0, 100, 100}, std::array{std::numeric_limits<float>::infinity()}); }
        catch (const std::invalid_argument&) { invalidRejected = true; }
        if (!invalidRejected) { throw std::runtime_error("Non-finite layout input was accepted"); }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
