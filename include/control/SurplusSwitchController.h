#pragma once

#include <stdint.h>

#include "output/ISwitchOutput.h"

namespace solarpilot::control {

struct SurplusSwitchConfig {
  float switchOnThresholdW;
  float switchOffThresholdW;
  uint32_t switchOnDelayMs;
  uint32_t switchOffDelayMs;
};

class SurplusSwitchController {
 public:
  SurplusSwitchController(const SurplusSwitchConfig& config,
                          output::ISwitchOutput& output);

  void update(float gridPowerW, uint32_t nowMs);
  bool isOn() const;

 private:
  static bool elapsedSince(uint32_t startMs, uint32_t durationMs,
                           uint32_t nowMs);

  SurplusSwitchConfig config_;
  output::ISwitchOutput& output_;

  bool isOn_ = false;
  bool onQualificationActive_ = false;
  bool offQualificationActive_ = false;
  uint32_t onQualifiedSinceMs_ = 0;
  uint32_t offQualifiedSinceMs_ = 0;
};

}  // namespace solarpilot::control
