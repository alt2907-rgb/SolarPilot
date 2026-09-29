#pragma once

#include <stdint.h>

#include "discovery/ShellyDeviceInfo.h"

namespace solarpilot::discovery {

// Milestone 4A: one-shot local discovery of Shelly devices via mDNS
// (service "_shelly._tcp") followed by a best-effort local RPC call
// (Shelly.GetDeviceInfo) to enrich each result. Uses only local network
// requests: no Shelly Cloud, no MQTT, no internet dependency.
//
// This module is read-only and side-effect free beyond logging: it does not
// change ShellyPlugOutput, does not persist anything, and never switches
// outputs automatically. Results are plain data so they can later be reused
// by a web UI "add device" flow.
class ShellyDiscovery {
 public:
  // Performs mDNS discovery and, for each found device (up to maxResults),
  // attempts to fetch device info via local HTTP RPC. Writes results into
  // the caller-provided array and returns the number of devices written.
  // Never blocks longer than roughly mdnsTimeoutMs + maxResults *
  // httpTimeoutMs, and always returns normally (0 on any failure) so callers
  // such as setup() can continue with GoodWe/Surplus logic unconditionally.
  size_t discover(ShellyDeviceInfo* results, size_t maxResults,
                  uint32_t mdnsTimeoutMs, uint32_t httpTimeoutMs);

 private:
  static bool fetchDeviceInfo(ShellyDeviceInfo& device, uint32_t timeoutMs);
  static bool extractJsonStringField(const String& json, const char* key,
                                     String& outValue);
  static bool extractJsonRawField(const String& json, const char* key,
                                  String& outValue);
};

}  // namespace solarpilot::discovery
