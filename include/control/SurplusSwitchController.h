#pragma once

#include <stdint.h>

#include "output/ISwitchOutput.h"

namespace solarpilot::control {

struct SurplusSwitchConfig {
  float switchOnThresholdW;
  float switchOffThresholdW;
  uint32_t switchOnDelayMs;
  uint32_t switchOffDelayMs;
  uint32_t outputRetryDelayMs;
  // Sicherheits-Fail-safe: wenn der Ausgang eingeschaltet ist und länger als
  // dieser Zeitraum keine gültige GoodWe-Netzleistung eintrifft, wird der
  // Ausgang zwangsweise ausgeschaltet (siehe SurplusSwitchController::noteReadFailure).
  uint32_t failSafeTimeoutMs;
};

class SurplusSwitchController {
 public:
  SurplusSwitchController(const SurplusSwitchConfig& config,
                          output::ISwitchOutput& output);

  void update(float gridPowerW, uint32_t nowMs);

  // Meldet einen fehlgeschlagenen/fehlenden GoodWe-Lesezyklus, ohne dass ein
  // Netzleistungswert vorliegt. Ein einzelner Fehlversuch schaltet den
  // Ausgang nicht sofort ab; erst wenn seit dem letzten gültigen Wert
  // config.failSafeTimeoutMs überschritten ist UND der Ausgang eingeschaltet
  // ist, wird sicherheitshalber ausgeschaltet. Danach greift zum Wiedereinschalten
  // wieder die normale Einschaltbedingung inklusive Einschaltverzögerung.
  void noteReadFailure(uint32_t nowMs);

  bool isOn() const;

 private:
  // Erwartet monotone Zeitbasis (z. B. millis()).
  static bool elapsedSince(uint32_t startMs, uint32_t durationMs,
                           uint32_t nowMs);

  bool trySetOutputState(bool isOn);
  void resetQualificationState();

  SurplusSwitchConfig config_;
  output::ISwitchOutput& output_;

  bool isOn_ = false;
  bool onQualificationActive_ = false;
  bool offQualificationActive_ = false;
  uint32_t onQualifiedSinceMs_ = 0;
  uint32_t offQualifiedSinceMs_ = 0;
  bool hasFailedOutputRequest_ = false;
  bool failedOutputRequestState_ = false;
  uint32_t failedOutputRequestMs_ = 0;

  bool hasValidReading_ = false;
  uint32_t lastValidReadMs_ = 0;
  bool failSafeShutdownPending_ = false;
};

}  // namespace solarpilot::control
