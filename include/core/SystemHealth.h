#pragma once

#include <Arduino.h>

namespace solarpilot::core {

enum class HealthState { kOk, kDegraded, kUnavailable };

struct SystemHealthSnapshot {
  bool wifiConnected;
  bool goodWeConnected;
  bool shellyOutputEnabled;
  bool outputRetryPending;
  bool hasValidGoodWeReading;
  uint32_t lastValidGoodWeAgeMs;
  uint32_t goodWeFailedCycles;
  HealthState overall;
};

class SystemHealth {
 public:
  void noteGoodWeReading(uint32_t nowMs);
  void setGoodWeFailedCycles(uint32_t cycles);
  SystemHealthSnapshot snapshot(bool wifiConnected, bool goodWeConnected,
                                bool shellyOutputEnabled,
                                bool outputRetryPending,
                                uint32_t nowMs,
                                uint32_t staleAfterMs) const;
  static const char* stateToString(HealthState state);

 private:
  bool hasValidGoodWeReading_ = false;
  uint32_t lastValidGoodWeReadingMs_ = 0;
  uint32_t goodWeFailedCycles_ = 0;
};

}  // namespace solarpilot::core
