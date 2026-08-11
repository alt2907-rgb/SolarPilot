#pragma once

#include <stdint.h>

#if __has_include("config/LocalCredentials.h")
#include "config/LocalCredentials.h"
#else
namespace solarpilot::config {
inline constexpr char kLocalWifiSsid[] = "YOUR_WIFI_SSID";
inline constexpr char kLocalWifiPassword[] = "YOUR_WIFI_PASSWORD";
// Output mode: set to true to enable Shelly Plug M Gen3 control.
// Default is false (VirtualSocketOutput) so flashing the firmware cannot
// accidentally switch a real device before this is explicitly enabled.
inline constexpr bool kLocalShellyOutputEnabled = false;
inline constexpr char kLocalShellyHost[] = "YOUR_SHELLY_IP";
inline constexpr uint8_t kLocalShellySwitchId = 0;
}  // namespace solarpilot::config
#endif

namespace solarpilot::config {

struct AppConfig {
  static constexpr const char* kWifiSsid = kLocalWifiSsid;
  static constexpr const char* kWifiPassword = kLocalWifiPassword;

  static constexpr uint32_t kWifiConnectTimeoutMs = 20000;
  static constexpr uint32_t kInverterDiscoveryTimeoutMs = 5000;
  static constexpr uint32_t kReadIntervalMs = 5000;

  static constexpr uint16_t kGoodWeDiscoveryPort = 48899;
  static constexpr uint16_t kGoodWeRuntimePort = 8899;
  static constexpr uint8_t kGoodWeModbusAddress = 0xF7;

  // Diagnosemodus: Gibt die rohe GoodWe-Antwort als Hex-Dump über den
  // seriellen Monitor aus. Für die Verifikation an echter Hardware auf
  // true setzen; im Normalbetrieb auf false belassen.
  static constexpr bool kHexDumpEnabled = false;

  // Testwerte für Milestone 2 (simulierte Überschuss-Schaltung)
  // Hinweis: Diese 200 W / 100 W-Werte sind temporäre Testwerte und werden
  // für den Produktiveinsatz angepasst.
  static constexpr float kSurplusSwitchOnThresholdW = 200.0F;
  static constexpr float kSurplusSwitchOffThresholdW = 100.0F;
  static constexpr uint32_t kSurplusSwitchOnDelayMs = 15000;
  static constexpr uint32_t kSurplusSwitchOffDelayMs = 10000;

  // Output: Shelly Plug M Gen3 (Milestone 3)
  // Konfiguration erfolgt in LocalCredentials.h (nicht ins Repository).
  static constexpr bool kShellyOutputEnabled = kLocalShellyOutputEnabled;
  static constexpr const char* kShellyHost = kLocalShellyHost;
  static constexpr uint8_t kShellySwitchId = kLocalShellySwitchId;
  static constexpr uint32_t kShellyHttpTimeoutMs = 3000;
};

}  // namespace solarpilot::config
