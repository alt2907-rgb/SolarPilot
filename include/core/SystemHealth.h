#pragma once

#include <Arduino.h>

namespace solarpilot::core {

enum class HealthState { kOk, kDegraded, kUnavailable };

struct SystemHealthSnapshot {
  bool wifiConnected;
  bool sourceConnected;
  bool shellyOutputEnabled;
  bool outputRetryPending;
  bool hasValidMeasurement;
  uint32_t lastValidMeasurementAgeMs;
  uint32_t sourceFailedCycles;
  HealthState overall;
};

class SystemHealth {
 public:
  void noteMeasurement(uint32_t nowMs);
  void setSourceFailedCycles(uint32_t cycles);
  SystemHealthSnapshot snapshot(bool wifiConnected, bool sourceConnected,
                                bool shellyOutputEnabled,
                                bool outputRetryPending,
                                uint32_t nowMs,
                                uint32_t staleAfterMs) const;
  static const char* stateToString(HealthState state);

 private:
  bool hasValidMeasurement_ = false;
  uint32_t lastValidMeasurementMs_ = 0;
  uint32_t sourceFailedCycles_ = 0;
};

}  // namespace solarpilot::core
