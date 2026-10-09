#include "Logging.hpp"
#include <mutex>

namespace composia {
namespace {
std::mutex& handler_mutex() {
    static std::mutex mutex;
    return mutex;
}

LogHandler& handler() {
    static LogHandler current;
    return current;
}
}

void set_log_handler(LogHandler replacement) {
    std::lock_guard lock{handler_mutex()};
    handler() = std::move(replacement);
}

namespace detail {

void log(LogLevel level, std::string_view message) noexcept {
    try {
        LogHandler current;
        {
            std::lock_guard lock{handler_mutex()};
            current = handler();
        }
        if (current) {
            current(level, message);
            return;
        }
        std::string line{level == LogLevel::warning ? "Composia warning: " : "Composia: "};
        line.append(message).push_back('\n');
        OutputDebugStringA(line.c_str());
    } catch (...) {
        // Logging never interrupts the operation it reports.
    }
}

std::string hex(HRESULT value) {
    constexpr char digits[] = "0123456789ABCDEF";
    const auto bits = static_cast<unsigned long>(value);
    std::string text{"0x00000000"};
    for (int index = 0; index != 8; ++index) {
        text[static_cast<std::size_t>(2 + index)] = digits[(bits >> (28 - 4 * index)) & 0xF];
    }
    return text;
}

}
}
