#pragma once

#include <stdint.h>

#if __has_include("config/LocalCredentials.h")
#include "config/LocalCredentials.h"
#else
namespace solarpilot::config {
inline constexpr char kLocalWifiSsid[] = "YOUR_WIFI_SSID";
inline constexpr char kLocalWifiPassword[] = "YOUR_WIFI_PASSWORD";
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
};

}  // namespace solarpilot::config
