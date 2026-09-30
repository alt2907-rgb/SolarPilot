#include "core/SystemHealth.h"

namespace solarpilot::core {

void SystemHealth::noteGoodWeReading(uint32_t nowMs) {
  hasValidGoodWeReading_ = true;
  lastValidGoodWeReadingMs_ = nowMs;
}

void SystemHealth::setGoodWeFailedCycles(uint32_t cycles) {
  goodWeFailedCycles_ = cycles;
}

SystemHealthSnapshot SystemHealth::snapshot(
    bool wifiConnected, bool goodWeConnected, bool shellyOutputEnabled,
    bool outputRetryPending, uint32_t nowMs, uint32_t staleAfterMs) const {
  const uint32_t ageMs =
      hasValidGoodWeReading_
          ? static_cast<uint32_t>(nowMs - lastValidGoodWeReadingMs_)
          : 0;

  HealthState overall = HealthState::kOk;
  if (!wifiConnected || !goodWeConnected || !hasValidGoodWeReading_ ||
      (hasValidGoodWeReading_ && ageMs >= staleAfterMs)) {
    overall = HealthState::kUnavailable;
  } else if (goodWeFailedCycles_ > 0 || outputRetryPending) {
    overall = HealthState::kDegraded;
  }

  return SystemHealthSnapshot{
      wifiConnected,
      goodWeConnected,
      shellyOutputEnabled,
      outputRetryPending,
      hasValidGoodWeReading_,
      ageMs,
      goodWeFailedCycles_,
      overall,
  };
}

const char* SystemHealth::stateToString(HealthState state) {
  switch (state) {
    case HealthState::kOk:
      return "OK";
    case HealthState::kDegraded:
      return "GESTOERT";
    case HealthState::kUnavailable:
      return "NICHT_VERFUEGBAR";
  }
  return "UNBEKANNT";
}

}  // namespace solarpilot::core
