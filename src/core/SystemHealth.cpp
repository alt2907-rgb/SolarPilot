#include "core/SystemHealth.h"

namespace solarpilot::core {

void SystemHealth::noteMeasurement(uint32_t nowMs) {
  hasValidMeasurement_ = true;
  lastValidMeasurementMs_ = nowMs;
}

void SystemHealth::setSourceFailedCycles(uint32_t cycles) {
  sourceFailedCycles_ = cycles;
}

SystemHealthSnapshot SystemHealth::snapshot(
    bool wifiConnected, bool sourceConnected, bool shellyOutputEnabled,
    bool outputRetryPending, uint32_t nowMs, uint32_t staleAfterMs) const {
  const uint32_t ageMs =
      hasValidMeasurement_
          ? static_cast<uint32_t>(nowMs - lastValidMeasurementMs_)
          : 0;

  HealthState overall = HealthState::kOk;
  if (!wifiConnected || !sourceConnected || !hasValidMeasurement_ ||
      (hasValidMeasurement_ && ageMs >= staleAfterMs)) {
    overall = HealthState::kUnavailable;
  } else if (sourceFailedCycles_ > 0 || outputRetryPending) {
    overall = HealthState::kDegraded;
  }

  return SystemHealthSnapshot{
      wifiConnected,
      sourceConnected,
      shellyOutputEnabled,
      outputRetryPending,
      hasValidMeasurement_,
      ageMs,
      sourceFailedCycles_,
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
