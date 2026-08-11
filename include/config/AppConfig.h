#pragma once

#include <stdint.h>

namespace solarpilot::config {

struct AppConfig {
  static constexpr const char* kWifiSsid = "YOUR_WIFI_SSID";
  static constexpr const char* kWifiPassword = "YOUR_WIFI_PASSWORD";

  static constexpr uint32_t kWifiConnectTimeoutMs = 20000;
  static constexpr uint32_t kInverterDiscoveryTimeoutMs = 5000;
  static constexpr uint32_t kReadIntervalMs = 5000;

  static constexpr uint16_t kGoodWeDiscoveryPort = 48899;
  static constexpr uint16_t kGoodWeRuntimePort = 8899;
};

}  // namespace solarpilot::config
