#include <composia/Signal.hpp>
#include <iostream>
#include <stdexcept>

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
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
