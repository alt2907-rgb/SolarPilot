#include "output/ShellyPlugOutput.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "core/Logger.h"

namespace solarpilot::output {

ShellyPlugOutput::ShellyPlugOutput(const char* host, uint8_t switchId,
                                   uint32_t timeoutMs)
    : host_(host), switchId_(switchId), timeoutMs_(timeoutMs) {}

void ShellyPlugOutput::setState(bool isOn) {
  if (hasState_ && isOn_ == isOn) {
    return;
  }

  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url),
           "http://%s/rpc/Switch.Set?id=%u&on=%s",
           host_, switchId_, isOn ? "true" : "false");

  http.begin(url);
  http.setTimeout(static_cast<int>(timeoutMs_));

  const int httpCode = http.GET();
  http.end();

  if (httpCode == HTTP_CODE_OK) {
    hasState_ = true;
    isOn_ = isOn;
    if (isOn_) {
      core::Logger::info("[SHELLY] Steckdose EIN");
    } else {
      core::Logger::info("[SHELLY] Steckdose AUS");
    }
  } else {
    // Log the error but do not update stored state so we retry next call.
    char msg[128];
    snprintf(msg, sizeof(msg),
             "[SHELLY] Kommunikationsfehler: HTTP %d (URL: %s)", httpCode, url);
    core::Logger::error(msg);
  }
}

}  // namespace solarpilot::output
