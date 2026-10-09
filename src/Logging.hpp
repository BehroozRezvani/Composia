#pragma once

#include <composia/Log.hpp>
#include <composia/Native.hpp>
#include <string>
#include <string_view>

namespace composia::detail {

void log(LogLevel, std::string_view message) noexcept;
// An HRESULT as eight uppercase hexadecimal digits after 0x, as in 0x887A0005.
std::string hex(HRESULT value);

}
