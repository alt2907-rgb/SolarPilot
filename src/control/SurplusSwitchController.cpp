#include "control/SurplusSwitchController.h"

namespace solarpilot::control {

SurplusSwitchController::SurplusSwitchController(
    const SurplusSwitchConfig& config, output::ISwitchOutput& output)
    : config_(config), output_(output) {}

void SurplusSwitchController::update(float gridPowerW, uint32_t nowMs) {
  if (!isOn_) {
    offQualificationActive_ = false;

    if (gridPowerW >= config_.switchOnThresholdW) {
      if (!onQualificationActive_) {
        onQualificationActive_ = true;
        onQualifiedSinceMs_ = nowMs;
      }

      if (elapsedSince(onQualifiedSinceMs_, config_.switchOnDelayMs, nowMs)) {
        isOn_ = true;
        onQualificationActive_ = false;
        output_.setState(true);
      }
    } else {
      onQualificationActive_ = false;
    }

    return;
  }

  onQualificationActive_ = false;

  if (gridPowerW <= config_.switchOffThresholdW) {
    if (!offQualificationActive_) {
      offQualificationActive_ = true;
      offQualifiedSinceMs_ = nowMs;
    }

    if (elapsedSince(offQualifiedSinceMs_, config_.switchOffDelayMs, nowMs)) {
      isOn_ = false;
      offQualificationActive_ = false;
      output_.setState(false);
    }
  } else {
    offQualificationActive_ = false;
  }
}

bool SurplusSwitchController::isOn() const { return isOn_; }

bool SurplusSwitchController::elapsedSince(uint32_t startMs,
                                           uint32_t durationMs,
                                           uint32_t nowMs) {
  return static_cast<uint32_t>(nowMs - startMs) >= durationMs;
}

}  // namespace solarpilot::control
