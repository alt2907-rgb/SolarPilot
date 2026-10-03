#pragma once

#include <Arduino.h>

namespace solarpilot::core {

class Logger {
 public:
  static void begin(unsigned long baudRate = 115200);
  static void info(const char* message);
  static void warn(const char* message);
  static void error(const char* message);
  static void infof(const char* format, ...);
  static String recentJson();
 private:
  static void write(const char* level, const char* message);
};

}  // namespace solarpilot::core
