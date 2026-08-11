#pragma once

namespace solarpilot::config {
inline constexpr char kLocalWifiSsid[] = "YOUR_WIFI_SSID";
inline constexpr char kLocalWifiPassword[] = "YOUR_WIFI_PASSWORD";
}  // namespace solarpilot::config

// -----------------------------------------------------------------------
// Shelly Plug M Gen3 – lokale LAN-Steuerung (Milestone 3, optional)
//
// Ohne diese Sektion baut das Projekt mit VirtualSocketOutput (Testmodus).
// Zur Aktivierung der echten Steckdose diese Sektion einkommentieren,
// SOLARPILOT_SHELLY_CONFIGURED definieren und die IP-Adresse eintragen.
// -----------------------------------------------------------------------
//
// #define SOLARPILOT_SHELLY_CONFIGURED
// namespace solarpilot::config {
// inline constexpr bool kLocalShellyOutputEnabled = true;
// inline constexpr char kLocalShellyHost[] = "192.168.1.xxx";  // IP oder Hostname
// inline constexpr uint8_t kLocalShellySwitchId = 0;
// }  // namespace solarpilot::config
