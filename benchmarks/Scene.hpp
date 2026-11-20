#pragma once
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace bench {
constexpr int width = 640, height = 360;
constexpr unsigned palette[]{0x1a2430, 0x698ba6, 0xe8a752};

// A full repaint and 96 cells. The checked pixel (8,8) changes color on every frame.
template<class Fill>
void scene(int frame, Fill fill) {
    fill(RECT{0, 0, width, height}, 0);
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 12; ++col) {
            const int x = 6 + col * 53, y = 6 + row * 44;
            fill(RECT{x, y, x + 46, y + 37}, 1 + ((row + col + frame) & 1));
        }
    }
}

inline long long tick() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

// Each implementation returns only after a pixel of the completed frame has been read back.
template<class Renderer>
int run(int argc, wchar_t** argv) try {
    if (argc != 2) { return 2; }
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) { return 1; }
    Renderer renderer;
    auto paint = [&](int i) {
        if (renderer.paint(i) != palette[1 + (i & 1)]) { throw std::runtime_error("Unexpected frame pixel"); }
    };
    paint(0);
    FILETIME first{};
    GetSystemTimePreciseAsFileTime(&first);
    auto stamp = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    const auto startup = (stamp(first) - stamp(creation)) / 10000.0;
    for (int i = 1; i <= 12; ++i) { paint(i); }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) { return 1; }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::array<double, 120> times{};
    for (size_t i = 0; i < times.size(); ++i) {
        const auto start = tick();
        paint(static_cast<int>(i + 13));
        times[i] = (tick() - start) * 1000.0 / frequency.QuadPart;
    }
    std::sort(times.begin(), times.end());
    FILE* out{};
    if (_wfopen_s(&out, argv[1], L"w")) { return 1; }
    const int count = std::fprintf(out, "%.4f,%zu,%.4f,%.4f\n", startup, memory.PrivateUsage,
        (times[59] + times[60]) / 2, times[113]);
    return std::fclose(out) || count <= 0;
} catch (const std::exception& ex) {
    std::fprintf(stderr, "%s\n", ex.what());
    return 1;
}
}
