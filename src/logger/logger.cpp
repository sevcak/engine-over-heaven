#include "logger.hpp"
#include <cassert>
#include <fmt/format.h>
#include <iostream>

namespace logger
{
constexpr std::string_view get_log_prefix(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return "\x1b[36mTRACE:\x1b[0m";
    case LogLevel::Debug:
        return "\x1b[94mDEBUG:\x1b[0m";
    case LogLevel::Info:
        return "\x1b[92mINFO:\x1b[0m";
    case LogLevel::Warn:
        return "\x1b[93mWARNING:\x1b[0m";
    case LogLevel::Error:
        return "\x1b[31mERROR:\x1b[0m";
    case LogLevel::Fatal:
        return "\x1b[91mFATAL:\x1b[0m";
    }
    return "";
}

void log_message(LogLevel level, std::string_view message, std::source_location location)
{
    fmt::memory_buffer buf;
    if (logs_location(level)) {
        fmt::format_to(std::back_inserter(buf), "{} {}:{}:{} {}\n", get_log_prefix(level),
            location.file_name(), location.line(), location.column(), message);
    } else {
        fmt::format_to(std::back_inserter(buf), "{} {}\n", get_log_prefix(level), message);
    }
    fmt::print("{}", std::string_view(buf.data(), buf.size()));
}

} // namespace logger
