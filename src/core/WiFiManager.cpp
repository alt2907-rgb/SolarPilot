#include "core/WiFiManager.h"

#include <WiFi.h>

#include "core/Logger.h"

namespace solarpilot::core {

bool WiFiManager::connect(const char* ssid, const char* password,
                          uint32_t timeoutMs) const {
  if (ssid == nullptr || ssid[0] == '\0') {
    Logger::error("WLAN-SSID fehlt. Bitte in AppConfig.h setzen.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Logger::info("Verbinde mit WLAN...");
  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < timeoutMs) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Logger::error("WLAN-Verbindung fehlgeschlagen.");
    return false;
  }

  Logger::infof("WLAN verbunden. IP: %s", WiFi.localIP().toString().c_str());
  return true;
}

bool WiFiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

}  // namespace solarpilot::core
