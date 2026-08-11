#pragma once

namespace solarpilot::config {
inline constexpr char kLocalWifiSsid[] = "YOUR_WIFI_SSID";
inline constexpr char kLocalWifiPassword[] = "YOUR_WIFI_PASSWORD";

// Shelly Plug M Gen3 – lokale LAN-Steuerung (Milestone 3)
// kLocalShellyOutputEnabled auf true setzen, um die echte Steckdose zu
// aktivieren. Standardmäßig false, damit kein reales Gerät beim Flashen
// unbeabsichtigt geschaltet wird.
inline constexpr bool kLocalShellyOutputEnabled = false;
inline constexpr char kLocalShellyHost[] = "192.168.1.xxx";  // IP oder Hostname
inline constexpr uint8_t kLocalShellySwitchId = 0;
}  // namespace solarpilot::config
