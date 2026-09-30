#include "output/ShellyPlugOutput.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "core/Logger.h"

namespace solarpilot::output {
namespace {

constexpr uint8_t kMaxSetStateAttempts = 3;
constexpr uint32_t kSetStateRetryDelayMs = 250;

}  // namespace

ShellyPlugOutput::ShellyPlugOutput(const char* host, uint8_t switchId,
                                   uint32_t timeoutMs)
    : host_(host), switchId_(switchId), timeoutMs_(timeoutMs) {}

bool ShellyPlugOutput::setState(bool isOn) {
  if (hasState_ && isOn_ == isOn) {
    return true;
  }

  char url[128];
  snprintf(url, sizeof(url),
           "http://%s/rpc/Switch.Set?id=%u&on=%s",
           host_, switchId_, isOn ? "true" : "false");

  for (uint8_t attempt = 1; attempt <= kMaxSetStateAttempts; ++attempt) {
    int httpCode = -1;
    if (testFailureEnabled_) {
      char message[112];
      snprintf(message, sizeof(message),
               "[TESTMODE] Shelly-Schaltversuch %u/%u fehlgeschlagen "
               "(Fehler simuliert).",
               static_cast<unsigned>(attempt),
               static_cast<unsigned>(kMaxSetStateAttempts));
      core::Logger::warn(message);
    } else {
      HTTPClient http;
      if (http.begin(url)) {
        http.setTimeout(static_cast<int>(timeoutMs_));
        httpCode = http.GET();
      }
      http.end();

      if (httpCode == HTTP_CODE_OK) {
        hasState_ = true;
        isOn_ = isOn;
        if (isOn_) {
          core::Logger::info("[SHELLY] Steckdose EIN");
        } else {
          core::Logger::info("[SHELLY] Steckdose AUS");
        }
        return true;
      }

      char message[160];
      snprintf(message, sizeof(message),
               "[SHELLY] Schaltversuch %u/%u fehlgeschlagen: HTTP %d "
               "(URL: %s)",
               static_cast<unsigned>(attempt),
               static_cast<unsigned>(kMaxSetStateAttempts), httpCode, url);
      core::Logger::warn(message);
    }

    if (attempt < kMaxSetStateAttempts) {
      delay(kSetStateRetryDelayMs);
    }
  }

  if (isOn) {
    core::Logger::error(
        "[ERROR] Shelly-EIN-Befehl endgültig fehlgeschlagen.");
  } else {
    core::Logger::error(
        "[SAFETY] [ERROR] Shelly-AUS-Befehl endgültig fehlgeschlagen; "
        "der Verbraucher könnte weiterhin eingeschaltet sein.");
  }
  return false;
}

void ShellyPlugOutput::setTestFailureEnabled(bool enabled) {
  testFailureEnabled_ = enabled;
}

}  // namespace solarpilot::output
