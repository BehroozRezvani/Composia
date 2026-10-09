#pragma once

#include <composia/Log.hpp>
#include <composia/Native.hpp>
#include <winrt/base.h>
#include <exception>
#include <string>
#include <string_view>

// A small file log for the demos, one line per event and written straight through, that also
// receives Composia's diagnostic events. Lines look like
// "2026-10-09T05:40:12.345 level=info event=graphics_device_created generation=1 driver=hardware".
// It formats by hand, without std::format or the CRT's time formatting, to keep the demos small.
// Before open_log, and in tests that never call it, lines go to the debugger output.
namespace demo {

inline wil::unique_hfile logFile;

inline std::string hex(HRESULT value) {
    constexpr char digits[] = "0123456789ABCDEF";
    const auto bits = static_cast<unsigned long>(value);
    std::string text{"0x00000000"};
    for (int index = 0; index != 8; ++index) {
        text[static_cast<std::size_t>(2 + index)] = digits[(bits >> (28 - 4 * index)) & 0xF];
    }
    return text;
}

inline void write_log(std::string_view level, std::string_view message) noexcept {
    try {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char stamp[]{"0000-00-00T00:00:00.000"};
        const auto put = [&](std::size_t offset, unsigned value, std::size_t digits) {
            for (auto index = digits; index-- != 0; value /= 10) { stamp[offset + index] = static_cast<char>('0' + value % 10); }
        };
        put(0, now.wYear, 4);
        put(5, now.wMonth, 2);
        put(8, now.wDay, 2);
        put(11, now.wHour, 2);
        put(14, now.wMinute, 2);
        put(17, now.wSecond, 2);
        put(20, now.wMilliseconds, 3);
        std::string line{stamp};
        line.append(" level=").append(level).append(" ").append(message).push_back('\n');
        if (logFile) {
            DWORD written{};
            WriteFile(logFile.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        } else {
            OutputDebugStringA(line.c_str());
        }
    } catch (...) {
        // A log line is never worth stopping the demo for.
    }
}

// Creates or truncates the file and routes Composia's own events into it.
inline void open_log(const wchar_t* path) {
    logFile.reset(CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    THROW_LAST_ERROR_IF(!logFile);
    composia::set_log_handler([](composia::LogLevel level, std::string_view message) {
        write_log(level == composia::LogLevel::warning ? "warning" : "info", message);
    });
}

// Records the exception being handled as the reason the demo stops; call it from a catch block.
inline void log_fatal() noexcept {
    try {
        try {
            throw;
        } catch (const winrt::hresult_error& error) {
            write_log("critical", "event=fatal hresult=" + hex(error.code().value) + " message=" + winrt::to_string(error.message()));
        } catch (const std::exception& error) {
            write_log("critical", std::string{"event=fatal message="} + error.what());
        }
    } catch (...) {
        write_log("critical", "event=fatal message=unknown_exception");
    }
}

}
