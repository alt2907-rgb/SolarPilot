#include "discovery/ShellyDiscovery.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "core/Logger.h"

namespace {
constexpr char kShellyMdnsService[] = "shelly";
constexpr char kShellyMdnsProto[] = "tcp";
constexpr char kShellyRpcDeviceInfoPath[] = "/rpc/Shelly.GetDeviceInfo";
constexpr uint8_t kDeviceInfoMaxAttempts = 3;
constexpr uint32_t kDeviceInfoRetryDelayMs = 250;
}  // namespace

namespace solarpilot::discovery {

size_t ShellyDiscovery::discover(ShellyDeviceInfo* results, size_t maxResults,
                                  uint32_t mdnsTimeoutMs,
                                  uint32_t httpTimeoutMs) {
  if (results == nullptr || maxResults == 0) {
    return 0;
  }

  if (WiFi.status() != WL_CONNECTED) {
    core::Logger::warn(
        "[SHELLY-DISCOVERY] Übersprungen: kein WLAN verbunden.");
    return 0;
  }

  if (!ensureMdnsStarted()) {
    core::Logger::warn(
        "[SHELLY-DISCOVERY] Übersprungen: mDNS nicht verfügbar (WLAN besteht "
        "weiterhin).");
    return 0;
  }

  // Note: this ESP32 Arduino core's MDNSResponder::queryService() has no
  // timeout parameter; it uses its own internal fixed wait. mdnsTimeoutMs is
  // kept in the public API for forward-compatibility with cores that add one.
  (void)mdnsTimeoutMs;
  const int found = MDNS.queryService(kShellyMdnsService, kShellyMdnsProto);
  if (found <= 0) {
    core::Logger::info(
        "[SHELLY-DISCOVERY] Keine Shelly-Geräte per mDNS gefunden.");
    return 0;
  }

  size_t count = 0;
  for (int i = 0; i < found && count < maxResults; ++i) {
    ShellyDeviceInfo device;
    device.ip = MDNS.IP(i);
    device.port = MDNS.port(i);
    device.hostname = MDNS.hostname(i);

    // Best-effort: failing to fetch device info still keeps the mDNS
    // result (ip/hostname), it just leaves infoRetrieved = false.
    fetchDeviceInfo(device, httpTimeoutMs);

    results[count] = device;
    ++count;
  }

  core::Logger::infof("[SHELLY-DISCOVERY] %u Shelly-Gerät(e) per mDNS gefunden.",
                      static_cast<unsigned>(count));
  return count;
}

bool ShellyDiscovery::ensureMdnsStarted() {
  if (mdnsStarted_) {
    return true;
  }

  // mdns_init() (called by MDNS.begin()) must only run once for the
  // lifetime of the WiFi connection; calling it again while already
  // initialized fails and was observed to destabilize UDP handling enough
  // to also break the unrelated GoodWe broadcast discovery right after.
  if (!MDNS.begin("solarpilot")) {
    core::Logger::warn(
        "[SHELLY-DISCOVERY] mDNS-Start fehlgeschlagen (WLAN ist verbunden, "
        "es liegt an mDNS/Discovery, nicht am WLAN).");
    return false;
  }

  mdnsStarted_ = true;
  core::Logger::info("[SHELLY-DISCOVERY] mDNS einmalig gestartet.");
  return true;
}

bool ShellyDiscovery::fetchDeviceInfo(ShellyDeviceInfo& device,
                                      uint32_t timeoutMs) {
  char url[96];
  snprintf(url, sizeof(url), "http://%s%s", device.ip.toString().c_str(),
           kShellyRpcDeviceInfoPath);

  String body;
  int lastHttpCode = -1;
  for (uint8_t attempt = 1; attempt <= kDeviceInfoMaxAttempts; ++attempt) {
    HTTPClient http;
    if (http.begin(url)) {
      http.setTimeout(static_cast<int>(timeoutMs));
      lastHttpCode = http.GET();
      if (lastHttpCode == HTTP_CODE_OK) {
        body = http.getString();
        http.end();
        break;
      }
    }
    http.end();
    if (attempt < kDeviceInfoMaxAttempts) {
      delay(kDeviceInfoRetryDelayMs);
    }
  }

  if (lastHttpCode != HTTP_CODE_OK) {
    core::Logger::infof(
        "[SHELLY-DISCOVERY] Geräteinfo nach %u Versuchen nicht abrufbar (%s, HTTP %d).",
        static_cast<unsigned>(kDeviceInfoMaxAttempts),
        device.ip.toString().c_str(), lastHttpCode);
    return false;
  }

  extractJsonStringField(body, "id", device.id);
  extractJsonStringField(body, "mac", device.mac);
  extractJsonStringField(body, "model", device.model);
  extractJsonRawField(body, "gen", device.generation);
  device.generation.trim();
  device.infoRetrieved = true;
  return true;
}

bool ShellyDiscovery::extractJsonStringField(const String& json,
                                             const char* key,
                                             String& outValue) {
  const String pattern = String("\"") + key + "\":\"";
  const int startIdx = json.indexOf(pattern);
  if (startIdx < 0) {
    return false;
  }
  const int valueStart = startIdx + pattern.length();
  const int valueEnd = json.indexOf('"', valueStart);
  if (valueEnd < 0) {
    return false;
  }
  outValue = json.substring(valueStart, valueEnd);
  return true;
}

bool ShellyDiscovery::extractJsonRawField(const String& json, const char* key,
                                          String& outValue) {
  const String pattern = String("\"") + key + "\":";
  const int startIdx = json.indexOf(pattern);
  if (startIdx < 0) {
    return false;
  }
  int valueEnd = startIdx + pattern.length();
  while (valueEnd < static_cast<int>(json.length()) &&
        json[valueEnd] != ',' && json[valueEnd] != '}') {
    ++valueEnd;
  }
  outValue = json.substring(startIdx + pattern.length(), valueEnd);
  outValue.trim();
  return true;
}

}  // namespace solarpilot::discovery
