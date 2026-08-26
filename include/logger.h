/**
 * @file logger.h
 * @brief Serial debug logging macros, compiled out entirely when
 * `DEBUG_SERIAL` is undefined.
 */
#pragma once
#include <Arduino.h>

// Comment out to silence all Serial debug prints made via LOG/LOG_DEBUG.
#define DEBUG_SERIAL

/// @brief Prints `file:line: msg` to Serial. Use via LOG_TRACE, not directly.
template<typename T>
inline void log(const char* file, int line, T msg) {
    Serial.print(file);
    Serial.print(':');
    Serial.print(line);
    Serial.print(": ");
    Serial.println(msg);   // picks the right overload for T
}

#ifdef DEBUG_SERIAL
/// Logs `msg` prefixed with the calling file/line. No-op when DEBUG_SERIAL is undefined.
#define LOG_TRACE(msg) do { log(__FILE__, __LINE__, (msg)); } while (0)
/// printf-style logging straight to Serial. No-op when DEBUG_SERIAL is undefined.
#define LOG_DEBUG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG_TRACE(msg)
#define LOG_DEBUG(...)
#endif