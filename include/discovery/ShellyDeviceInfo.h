#pragma once

#include <Arduino.h>

namespace solarpilot::discovery {

// Plain data describing a Shelly device found via mDNS discovery, optionally
// enriched with details from the Shelly RPC "Shelly.GetDeviceInfo" call.
// Deliberately dependency-free (no cloud/MQTT fields) so it can later be
// reused as-is by a "add device" web UI flow without further conversion.
struct ShellyDeviceInfo {
  IPAddress ip;
  uint16_t port = 80;
  String hostname;    // mDNS hostname, e.g. "shellyplug-s-XXXXXX.local"
  String id;          // Shelly RPC device id
  String mac;
  String model;
  String generation;  // Shelly RPC "gen" field, e.g. "2"; empty if unknown
  bool infoRetrieved = false;  // true if Shelly.GetDeviceInfo succeeded
};

}  // namespace solarpilot::discovery
