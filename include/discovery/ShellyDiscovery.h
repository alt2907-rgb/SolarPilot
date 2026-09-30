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
//
// mDNS lifecycle: MDNSResponder::begin() calls the underlying esp-idf
// mdns_init(), which is not safe to call repeatedly (a second call while
// already initialized fails and can leave the mDNS/UDP state unstable,
// which was observed to make subsequent GoodWe UDP broadcast discovery
// fail too). ShellyDiscovery therefore starts mDNS at most once per
// object lifetime (i.e. once per WiFi connection in practice) and simply
// reuses it on every later discover() call, including repeated manual
// triggers.
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
  // Starts mDNS exactly once; returns true if mDNS is (already) running.
  bool ensureMdnsStarted();

  static bool fetchDeviceInfo(ShellyDeviceInfo& device, uint32_t timeoutMs);
  static bool extractJsonStringField(const String& json, const char* key,
                                     String& outValue);
  static bool extractJsonRawField(const String& json, const char* key,
                                  String& outValue);

  bool mdnsStarted_ = false;
};

}  // namespace solarpilot::discovery
