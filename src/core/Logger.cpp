#include "core/Logger.h"

#include <stdarg.h>

namespace solarpilot::core {
namespace {
struct Entry { uint32_t sequence; uint32_t timeMs; char text[256]; };
constexpr size_t kCapacity = 64;
Entry entries[kCapacity] = {};
size_t nextEntry = 0;
size_t entryCount = 0;
uint32_t sequence = 0;
}

void Logger::begin(unsigned long baudRate) {
  Serial.begin(baudRate);
}

void Logger::info(const char* message) {
  write("INFO", message);
}

void Logger::warn(const char* message) {
  write("WARN", message);
}

void Logger::error(const char* message) {
  write("ERROR", message);
}

void Logger::write(const char* level, const char* message) {
  Entry& entry = entries[nextEntry];
  entry.sequence = ++sequence;
  entry.timeMs = millis();
  snprintf(entry.text, sizeof(entry.text), "[%s] %s", level, message);
  Serial.println(entry.text);
  nextEntry = (nextEntry + 1) % kCapacity;
  if (entryCount < kCapacity) ++entryCount;
}

String Logger::recentJson() {
  String json = "[";
  json.reserve(entryCount * 300 + 2);
  for (size_t i = 0; i < entryCount; ++i) {
    const Entry& entry = entries[(nextEntry + kCapacity - entryCount + i) % kCapacity];
    if (i) json += ',';
    json += "{\"id\":" + String(entry.sequence) + ",\"ms\":" + String(entry.timeMs) + ",\"text\":\"";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(entry.text); *p; ++p) {
      if (*p == '"' || *p == '\\') { json += '\\'; json += static_cast<char>(*p); }
      else if (*p < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", *p); json += escaped; }
      else json += static_cast<char>(*p);
    }
    json += "\"}";
  }
  return json + ']';
}

void Logger::infof(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  info(buffer);
}

}  // namespace solarpilot::core
