#pragma once

#include <functional>
#include <string_view>

namespace composia {

// info: normal events such as device creation; warning: failures Composia recovered from.
enum class LogLevel { info, warning };

// Receives Composia's diagnostic events, such as graphics device creation, loss, and recovery,
// as "event=name key=value" text. Without a handler they go to OutputDebugString. Composia logs
// on the UI thread; set the handler before creating the Application to see every event, and pass
// nullptr to restore the default. Exceptions from the handler are ignored, so logging never
// interrupts the operation it reports.
using LogHandler = std::function<void(LogLevel, std::string_view message)>;
void set_log_handler(LogHandler handler);

}
