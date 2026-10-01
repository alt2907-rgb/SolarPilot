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

void WiFiManager::requestReconnect() const {
  // WiFi.reconnect() only starts the station reconnect attempt; recovery is
  // observed asynchronously from loop(), so safety handling keeps running.
  WiFi.reconnect();
}

void WiFiManager::restartStation(const char* ssid, const char* password) const {
  Logger::warn("[RECOVERY] Starte WLAN-Interface vollstaendig neu...");
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}

void WiFiManager::disconnectForTest() const {
  WiFi.disconnect(false, false);
}

}  // namespace solarpilot::core
