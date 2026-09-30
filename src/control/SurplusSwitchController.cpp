#include "control/SurplusSwitchController.h"

#include "core/Logger.h"

namespace solarpilot::control {

SurplusSwitchController::SurplusSwitchController(
    const SurplusSwitchConfig& config, output::ISwitchOutput& output)
    : config_(config), output_(output) {}

void SurplusSwitchController::update(float gridPowerW, uint32_t nowMs) {
  hasValidReading_ = true;
  lastValidReadMs_ = nowMs;
  failSafeShutdownPending_ = false;

  if (!isOn_) {
    offQualificationActive_ = false;

    if (gridPowerW >= config_.switchOnThresholdW) {
      if (!onQualificationActive_) {
        onQualificationActive_ = true;
        onQualifiedSinceMs_ = nowMs;
      }

      if (elapsedSince(onQualifiedSinceMs_, config_.switchOnDelayMs, nowMs)) {
        if (output_.setState(true)) {
          isOn_ = true;
          onQualificationActive_ = false;
          offQualificationActive_ = false;
          offQualifiedSinceMs_ = 0;
        }
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
      if (output_.setState(false)) {
        isOn_ = false;
        offQualificationActive_ = false;
        onQualificationActive_ = false;
        onQualifiedSinceMs_ = 0;
      }
    }
  } else {
    offQualificationActive_ = false;
  }
}

bool SurplusSwitchController::isOn() const { return isOn_; }

void SurplusSwitchController::noteReadFailure(uint32_t nowMs) {
  if (!isOn_ || !hasValidReading_) {
    return;
  }

  if (!elapsedSince(lastValidReadMs_, config_.failSafeTimeoutMs, nowMs)) {
    return;
  }

  if (!failSafeShutdownPending_) {
    core::Logger::infof(
        "[SAFETY] Keine gültigen GoodWe-Daten seit %u s – Fail-safe "
        "fordert AUS an.",
        static_cast<unsigned>(config_.failSafeTimeoutMs / 1000U));
    failSafeShutdownPending_ = true;
  }

  if (output_.setState(false)) {
    isOn_ = false;
    resetQualificationState();
    failSafeShutdownPending_ = false;
  }
}

void SurplusSwitchController::resetQualificationState() {
  onQualificationActive_ = false;
  offQualificationActive_ = false;
  onQualifiedSinceMs_ = 0;
  offQualifiedSinceMs_ = 0;
}

bool SurplusSwitchController::elapsedSince(uint32_t startMs,
                                           uint32_t durationMs,
                                           uint32_t nowMs) {
  return static_cast<uint32_t>(nowMs - startMs) >= durationMs;
}

}  // namespace solarpilot::control
