#include "core/WiFiManager.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>

#include "core/Logger.h"

namespace solarpilot::core {
namespace {
std::atomic<unsigned> lastDisconnectReason{0};
void configureStationSelection() {
  // FAST_SCAN stops at the first matching SSID, even if another mesh AP is
  // much stronger. Let the driver compare candidates only when connecting.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
}
void applyRadioSettings() {
  // The SuperMini AP became visible at this power in the hardware comparison.
  // Reapply after every driver restart; an OFF/STA cycle resets radio settings.
  // setSleep(false) returns false when its cached value already equals NONE.
  // Preserve that setting across events, then verify the actual driver state.
  WiFi.setSleep(false);
  wifi_ps_type_t sleepMode = WIFI_PS_MIN_MODEM;
  const bool sleepDisabled = esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK &&
      esp_wifi_get_ps(&sleepMode) == ESP_OK && sleepMode == WIFI_PS_NONE;
  const bool powerApplied = WiFi.setTxPower(WIFI_POWER_8_5dBm);
  Logger::infof("[WLAN-DIAG] Funk: Energiesparen aus=%d, 8.5 dBm gesetzt=%d",
                sleepDisabled, powerApplied);
}
}

bool WiFiManager::connect(const char* ssid, const char* password,
                          uint32_t timeoutMs) const {
  if (ssid == nullptr || ssid[0] == '\0') {
    Logger::error("WLAN-SSID fehlt. Bitte in AppConfig.h setzen.");
    return false;
  }

  static bool eventsRegistered = false;
  if (!eventsRegistered) {
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
      // The callback runs on another task: only capture a number here. All
      // serial/ring-buffer logging stays on the main loop task.
      lastDisconnectReason.store(info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    eventsRegistered = true;
  }

  WiFi.mode(WIFI_STA);
  configureStationSelection();
  WiFi.begin(ssid, password);
  applyRadioSettings();

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
  Logger::infof("[WLAN-DIAG] Statuscode=%d, letzter Abbruchgrund=%u",
                static_cast<int>(WiFi.status()), lastDisconnectReason.load());
  WiFi.reconnect();
}

void WiFiManager::restartStation(const char* ssid, const char* password) const {
  Logger::warn("[RECOVERY] Starte WLAN-Interface vollstaendig neu...");
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  configureStationSelection();
  WiFi.begin(ssid, password);
  applyRadioSettings();
}

void WiFiManager::disconnectForTest() const {
  WiFi.disconnect(false, false);
}

}  // namespace solarpilot::core
