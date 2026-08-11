#include "core/Logger.h"

#include <stdarg.h>

namespace solarpilot::core {

void Logger::begin(unsigned long baudRate) {
  Serial.begin(baudRate);
}

void Logger::info(const char* message) {
  Serial.print("[INFO] ");
  Serial.println(message);
}

void Logger::warn(const char* message) {
  Serial.print("[WARN] ");
  Serial.println(message);
}

void Logger::error(const char* message) {
  Serial.print("[ERROR] ");
  Serial.println(message);
}

void Logger::infof(const char* format, ...) {
  char buffer[160];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  info(buffer);
}

}  // namespace solarpilot::core
