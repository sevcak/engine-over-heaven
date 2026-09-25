#pragma once

#include <fmt/format.h>
#include <source_location>
#include <string_view>
#include <utility>
#include <type_traits>

enum class LogLevel
{
    Fatal,
    Error,
    Warn,
    Debug,
    Trace,
    Info
};

namespace logger
{

constexpr std::string_view get_log_level_string(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return "TRACE:";
    case LogLevel::Debug:
        return "DEBUG:";
    case LogLevel::Info:
        return "INFO:";
    case LogLevel::Warn:
        return "WARNING:";
    case LogLevel::Error:
        return "ERROR:";
    case LogLevel::Fatal:
        return "FATAL:";
    }
    return "";
}

constexpr bool logs_location(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
    case LogLevel::Trace:
    case LogLevel::Error:
    case LogLevel::Fatal:
        return true;
    case LogLevel::Warn:
    case LogLevel::Info:
        return false;
    }
    return false;
}

void log_message(LogLevel level, std::string_view message,
    std::source_location location = std::source_location::current());

template <typename... Args>
struct LogLocationWrapper
{
    fmt::format_string<Args...> fmt_str;
    std::source_location location;

    template <typename T>
    consteval LogLocationWrapper(
        const T &s, std::source_location loc = std::source_location::current())
        : fmt_str { s }, location { loc }
    {
    }
};

/**
 * @brief Log a fatal error message.
 *
 * Format the message with LogLevel::Fatal and output it with source location details.
 *
 * @tparam Args Types of the format arguments.
 * @param fmt Wrapper holding the format string and source location.
 * @param args Arguments to be formatted into the string.
 */
template <typename... Args>
void fatal(std::type_identity_t<LogLocationWrapper<Args...>> fmt, Args &&...args)
{
    log_message(LogLevel::Fatal, fmt::format(fmt.fmt_str, std::forward<Args>(args)...), fmt.location);
}

/**
 * @brief Log an error message.
 *
 * Format the message with LogLevel::Error and output it with source location details.
 *
 * @tparam Args Types of the format arguments.
 * @param fmt Wrapper holding the format string and source location.
 * @param args Arguments to be formatted into the string.
 */
template <typename... Args>
void error(std::type_identity_t<LogLocationWrapper<Args...>> fmt, Args &&...args)
{
    log_message(LogLevel::Error, fmt::format(fmt.fmt_str, std::forward<Args>(args)...), fmt.location);
}

/**
 * @brief Log a warning message.
 *
 * Format the message with LogLevel::Warn and output it.
 *
 * @tparam Args Types of the format arguments.
 * @param fmt Wrapper holding the format string and source location.
 * @param args Arguments to be formatted into the string.
 */
template <typename... Args>
void warn(std::type_identity_t<LogLocationWrapper<Args...>> fmt, Args &&...args)
{
    log_message(LogLevel::Warn, fmt::format(fmt.fmt_str, std::forward<Args>(args)...), fmt.location);
}

/**
 * @brief Log an informational message.
 *
 * Format the message with LogLevel::Info and output it.
 *
 * @tparam Args Types of the format arguments.
 * @param fmt Wrapper holding the format string and source location.
 * @param args Arguments to be formatted into the string.
 */
template <typename... Args>
void info(std::type_identity_t<LogLocationWrapper<Args...>> fmt, Args &&...args)
{
    log_message(LogLevel::Info, fmt::format(fmt.fmt_str, std::forward<Args>(args)...), fmt.location);
}

/**
 * @brief Log a debug message.
 *
 * Format the message with LogLevel::Debug and output it with source location details.
 *
 * @tparam Args Types of the format arguments.
 * @param fmt Wrapper holding the format string and source location.
 * @param args Arguments to be formatted into the string.
 */
template <typename... Args>
void debug(std::type_identity_t<LogLocationWrapper<Args...>> fmt, Args &&...args)
{
    log_message(LogLevel::Debug, fmt::format(fmt.fmt_str, std::forward<Args>(args)...), fmt.location);
}

/**
 * @brief Log a trace message.
 *
 * Format the message with LogLevel::Trace and output it with source location details.
 *
 * @tparam Args Types of the format arguments.
 * @param fmt Wrapper holding the format string and source location.
 * @param args Arguments to be formatted into the string.
 */
template <typename... Args>
void trace(std::type_identity_t<LogLocationWrapper<Args...>> fmt, Args &&...args)
{
    log_message(LogLevel::Trace, fmt::format(fmt.fmt_str, std::forward<Args>(args)...), fmt.location);
}
} // namespace logger

// * We use macros here as a necessary evil to ensure zero overhead for the debug and trace logs in release builds.
// * To have a consistent API, we also provide macros for all log levels, even those that are always enabled.

/**
 * @brief Log a fatal error message.
 *
 * Format the message using fmt syntax and log it with LogLevel::Fatal.
 * Automatically capture and output source location (file, line, column).
 *
 * @param ... Format string followed by optional formatting arguments.
 */
#define OH_LOG_FATAL(...) logger::fatal(__VA_ARGS__)

/**
 * @brief Log an error message.
 *
 * Format the message using fmt syntax and log it with LogLevel::Error.
 * Automatically capture and output source location (file, line, column).
 *
 * @param ... Format string followed by optional formatting arguments.
 */
#define OH_LOG_ERROR(...) logger::error(__VA_ARGS__)

/**
 * @brief Log a warning message.
 *
 * Format the message using fmt syntax and log it with LogLevel::Warn.
 * Does not include source location in the output.
 *
 * @param ... Format string followed by optional formatting arguments.
 */
#define OH_LOG_WARN(...) logger::warn(__VA_ARGS__)

/**
 * @brief Log an informational message.
 *
 * Format the message using fmt syntax and log it with LogLevel::Info.
 * Does not include source location in the output.
 *
 * @param ... Format string followed by optional formatting arguments.
 */
#define OH_LOG_INFO(...) logger::info(__VA_ARGS__)

#ifndef NDEBUG
/**
 * @brief Log a debug message.
 *
 * Format the message using fmt syntax and log it with LogLevel::Debug.
 * Automatically capture and output source location (file, line, column).
 *
 * @note In release builds (when @c NDEBUG is defined), this macro is disabled
 *       and expands to @c ((void)0).
 *
 * @param ... Format string followed by optional formatting arguments.
 */
#define OH_LOG_DEBUG(...) logger::debug(__VA_ARGS__)

/**
 * @brief Log a trace message.
 *
 * Format the message using fmt syntax and log it with LogLevel::Trace.
 * Automatically capture and output source location (file, line, column).
 *
 * @note In release builds (when @c NDEBUG is defined), this macro is disabled
 *       and expands to @c ((void)0).
 *
 * @param ... Format string followed by optional formatting arguments.
 */
#define OH_LOG_TRACE(...) logger::trace(__VA_ARGS__)
#else
#define OH_LOG_DEBUG(...) ((void)0)
#define OH_LOG_TRACE(...) ((void)0)
#endif
