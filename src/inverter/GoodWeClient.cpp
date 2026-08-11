#include "inverter/GoodWeClient.h"

#include <WiFi.h>

#include "core/Logger.h"

namespace {
constexpr char kDiscoveryRequest[] = "WIFIKIT-214028-READ";
constexpr uint8_t kRuntimeRequest[] = {0xAA, 0x55, 0xC0, 0x7F, 0x01,
                                       0x06, 0x00, 0x02, 0x45};

constexpr size_t kPayloadOffset = 7;
constexpr size_t kPGridOffset = 38;
constexpr size_t kGridDirectionOffset = 80;
constexpr size_t kRuntimeResponseMinLength =
    kPayloadOffset + kGridDirectionOffset + 1;
constexpr uint32_t kRuntimeResponseTimeoutMs = 1200;
constexpr uint8_t kGridDirectionExport = 1;
constexpr uint8_t kGridDirectionImport = 2;
}  // namespace

namespace solarpilot::inverter {

GoodWeClient::GoodWeClient(uint16_t discoveryPort, uint16_t runtimePort)
    : discoveryPort_(discoveryPort),
      runtimePort_(runtimePort),
      inverterIp_(0, 0, 0, 0),
      connected_(false) {}

bool GoodWeClient::discover(InverterEndpoint& endpoint, uint32_t timeoutMs) {
  if (!udp_.begin(0)) {
    core::Logger::error("UDP konnte nicht gestartet werden.");
    return false;
  }

  udp_.beginPacket(IPAddress(255, 255, 255, 255), discoveryPort_);
  udp_.write(reinterpret_cast<const uint8_t*>(kDiscoveryRequest),
             sizeof(kDiscoveryRequest) - 1);
  udp_.endPacket();

  const uint32_t startMs = millis();
  while ((millis() - startMs) < timeoutMs) {
    const int packetSize = udp_.parsePacket();
    if (packetSize <= 0) {
      delay(20);
      continue;
    }

    char payload[128] = {0};
    const int bytesRead = udp_.read(payload, sizeof(payload) - 1);
    if (bytesRead <= 0) {
      continue;
    }

    endpoint.ip = udp_.remoteIP();
    endpoint.port = runtimePort_;
    endpoint.serial = String(payload);
    core::Logger::infof("GoodWe gefunden: %s", endpoint.ip.toString().c_str());
    return true;
  }

  core::Logger::warn("Kein GoodWe-Wechselrichter per Broadcast gefunden.");
  return false;
}

bool GoodWeClient::connect(const InverterEndpoint& endpoint) {
  inverterIp_ = endpoint.ip;
  connected_ = inverterIp_ != IPAddress(0, 0, 0, 0);
  if (connected_) {
    core::Logger::infof("Verbindung vorbereitet zu %s:%u",
                        inverterIp_.toString().c_str(), runtimePort_);
  }
  return connected_;
}

bool GoodWeClient::requestRuntimeData(uint8_t* responseBuffer, size_t bufferSize,
                                      size_t& responseLen) {
  responseLen = 0;
  if (!connected_) {
    return false;
  }

  udp_.beginPacket(inverterIp_, runtimePort_);
  udp_.write(kRuntimeRequest, sizeof(kRuntimeRequest));
  udp_.endPacket();

  const uint32_t startMs = millis();
  while ((millis() - startMs) < kRuntimeResponseTimeoutMs) {
    const int packetSize = udp_.parsePacket();
    if (packetSize <= 0) {
      delay(20);
      continue;
    }

    if (static_cast<size_t>(packetSize) > bufferSize) {
      core::Logger::error("Antwortpaket ist größer als der Puffer.");
      return false;
    }

    const int bytesRead = udp_.read(responseBuffer, bufferSize);
    if (bytesRead <= 0) {
      return false;
    }

    responseLen = static_cast<size_t>(bytesRead);
    return true;
  }

  return false;
}

bool GoodWeClient::readGridPowerW(float& gridPowerW) {
  uint8_t response[256] = {0};
  size_t responseLen = 0;
  if (!requestRuntimeData(response, sizeof(response), responseLen)) {
    core::Logger::warn("Keine Laufzeitdaten vom Wechselrichter erhalten.");
    return false;
  }

  if (responseLen < kRuntimeResponseMinLength) {
    core::Logger::warn("Antwort ist zu kurz für Netzleistungsdaten.");
    return false;
  }
  if (responseLen < 2) {
    return false;
  }

  if (response[0] != 0xAA || response[1] != 0x55 || response[4] != 0x01 ||
      response[5] != 0x86) {
    core::Logger::warn("Unerwarteter Antworttyp vom Wechselrichter.");
    return false;
  }

  const uint16_t expectedChecksum = checksum(response, responseLen - 2);
  const uint16_t responseChecksum = static_cast<uint16_t>(
      (static_cast<uint16_t>(response[responseLen - 2]) << 8U) |
      static_cast<uint16_t>(response[responseLen - 1]));
  if (expectedChecksum != responseChecksum) {
    core::Logger::warn("Ungültige Checksumme in GoodWe-Antwort.");
    return false;
  }

  const size_t payloadPGridOffset = kPayloadOffset + kPGridOffset;
  const size_t payloadDirectionOffset = kPayloadOffset + kGridDirectionOffset;
  const uint16_t pgridRaw = readUInt16(response, payloadPGridOffset);
  const uint8_t direction = response[payloadDirectionOffset];

  // Positive Werte = Export ins Netz, negative Werte = Import aus dem Netz.
  float signedPower = static_cast<float>(pgridRaw);
  if (direction == kGridDirectionImport) {
    signedPower = -signedPower;
  } else if (direction != kGridDirectionExport) {
    core::Logger::warn("Unbekannter Netzrichtungswert in GoodWe-Antwort.");
    return false;
  }

  gridPowerW = signedPower;
  return true;
}

uint16_t GoodWeClient::checksum(const uint8_t* data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i < len; ++i) {
    sum += data[i];
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

uint16_t GoodWeClient::readUInt16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) |
                               static_cast<uint16_t>(data[offset + 1]));
}

}  // namespace solarpilot::inverter
