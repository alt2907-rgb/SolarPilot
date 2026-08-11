#include "inverter/GoodWeClient.h"

#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Logger.h"

namespace {
constexpr char kDiscoveryRequest[] = "WIFIKIT-214028-READ";
constexpr uint8_t kRuntimeResponsePrefix0 = 0xAA;
constexpr uint8_t kRuntimeResponsePrefix1 = 0x55;
constexpr uint8_t kModbusReadHoldingRegisters = 0x03;
constexpr uint16_t kRuntimeRegisterStart = 0x891C;
constexpr uint16_t kRuntimeRegisterCount = 0x007D;
constexpr uint16_t kActivePowerRegister = 35140;
constexpr size_t kRuntimePayloadOffset = 5;
constexpr size_t kCrcLength = 2;
constexpr size_t kActivePowerPayloadOffset =
    static_cast<size_t>(kActivePowerRegister - kRuntimeRegisterStart) * 2U;
constexpr size_t kRuntimeResponseMinLength =
    kRuntimePayloadOffset + kActivePowerPayloadOffset + sizeof(int16_t) +
    kCrcLength;
constexpr uint32_t kRuntimeResponseTimeoutMs = 1200;
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

  uint8_t request[8] = {0};
  request[0] = config::AppConfig::kGoodWeModbusAddress;
  request[1] = kModbusReadHoldingRegisters;
  request[2] = static_cast<uint8_t>((kRuntimeRegisterStart >> 8U) & 0xFFU);
  request[3] = static_cast<uint8_t>(kRuntimeRegisterStart & 0xFFU);
  request[4] = static_cast<uint8_t>((kRuntimeRegisterCount >> 8U) & 0xFFU);
  request[5] = static_cast<uint8_t>(kRuntimeRegisterCount & 0xFFU);
  const uint16_t requestCrc = checksum(request, 6);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFFU);
  request[7] = static_cast<uint8_t>((requestCrc >> 8U) & 0xFFU);

  udp_.beginPacket(inverterIp_, runtimePort_);
  udp_.write(request, sizeof(request));
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
  uint8_t response[260] = {0};
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

  if (response[0] != kRuntimeResponsePrefix0 ||
      response[1] != kRuntimeResponsePrefix1 ||
      response[2] != config::AppConfig::kGoodWeModbusAddress ||
      response[3] != kModbusReadHoldingRegisters) {
    core::Logger::warn("Unerwarteter Antworttyp vom Wechselrichter.");
    return false;
  }
  if (response[4] != static_cast<uint8_t>(kRuntimeRegisterCount * 2U)) {
    core::Logger::warn("Unerwartete Payload-Länge in GoodWe-Antwort.");
    return false;
  }

  const uint16_t expectedChecksum = checksum(response + 2, responseLen - 4);
  const uint16_t responseChecksum = static_cast<uint16_t>(
      static_cast<uint16_t>(response[responseLen - 2]) |
      (static_cast<uint16_t>(response[responseLen - 1]) << 8U));
  if (expectedChecksum != responseChecksum) {
    core::Logger::warn("Ungültige Checksumme in GoodWe-Antwort.");
    return false;
  }

  const size_t activePowerOffset =
      kRuntimePayloadOffset + kActivePowerPayloadOffset;
  const int16_t activePower = readInt16(response, activePowerOffset);
  gridPowerW = static_cast<float>(activePower);
  return true;
}

uint16_t GoodWeClient::checksum(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001U) != 0U) {
        crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
      } else {
        crc = static_cast<uint16_t>(crc >> 1U);
      }
    }
  }
  return crc;
}

int16_t GoodWeClient::readInt16(const uint8_t* data, size_t offset) {
  return static_cast<int16_t>((static_cast<uint16_t>(data[offset]) << 8U) |
                              static_cast<uint16_t>(data[offset + 1]));
}

}  // namespace solarpilot::inverter
