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
}  // namespace

namespace solarpilot::inverter {

GoodWeClient::GoodWeClient(uint16_t discoveryPort, uint16_t runtimePort)
    : discoveryPort_(discoveryPort),
      runtimePort_(runtimePort),
      inverterIp_(0, 0, 0, 0),
      connected_(false),
      successfulReads_(0),
      failedReadCycles_(0),
      totalRetryAttempts_(0),
      unexpectedSenderPackets_(0),
      invalidRuntimePackets_(0),
      runtimeTimeouts_(0),
      lastResponseTimeMs_(0),
      maxResponseTimeMs_(0),
      totalResponseTimeMs_(0) {}

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
    if (bytesRead <= 0) continue;

    endpoint.ip = udp_.remoteIP();
    endpoint.port = runtimePort_;
    endpoint.serial = String(payload);
    core::Logger::infof("GoodWe gefunden: %s", endpoint.ip.toString().c_str());
    return true;
  }

  core::Logger::warn("Kein GoodWe-Wechselrichter per Broadcast gefunden.");
  return false;
}

void GoodWeClient::resetConnection() {
  connected_ = false;
  inverterIp_ = IPAddress(0, 0, 0, 0);
  udp_.stop();
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

GoodWeClient::RuntimeAttemptResult GoodWeClient::requestRuntimeData(
    uint8_t* responseBuffer, size_t bufferSize, size_t& responseLen,
    uint32_t& responseTimeMs) {
  responseLen = 0;
  responseTimeMs = 0;
  if (!connected_) return RuntimeAttemptResult::kSendFailed;

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

  if (udp_.beginPacket(inverterIp_, runtimePort_) == 0) {
    return RuntimeAttemptResult::kSendFailed;
  }
  const size_t written = udp_.write(request, sizeof(request));
  if (written != sizeof(request) || udp_.endPacket() == 0) {
    return RuntimeAttemptResult::kSendFailed;
  }

  const uint32_t startMs = millis();
  while ((millis() - startMs) < config::AppConfig::kGoodWeRuntimeResponseTimeoutMs) {
    const int packetSize = udp_.parsePacket();
    if (packetSize <= 0) {
      delay(20);
      continue;
    }

    const IPAddress remoteIp = udp_.remoteIP();
    const uint16_t remotePort = udp_.remotePort();
    if (remoteIp != inverterIp_ || remotePort != runtimePort_) {
      ++unexpectedSenderPackets_;
      udp_.flush();
      continue;
    }

    if (static_cast<size_t>(packetSize) > bufferSize) {
      ++invalidRuntimePackets_;
      udp_.flush();
      return RuntimeAttemptResult::kInvalidPacket;
    }

    const int bytesRead = udp_.read(responseBuffer, bufferSize);
    if (bytesRead <= 0) {
      ++invalidRuntimePackets_;
      return RuntimeAttemptResult::kInvalidPacket;
    }

    responseLen = static_cast<size_t>(bytesRead);
    responseTimeMs = millis() - startMs;
    return RuntimeAttemptResult::kSuccess;
  }

  ++runtimeTimeouts_;
  return RuntimeAttemptResult::kTimeout;
}

const char* GoodWeClient::attemptResultToString(RuntimeAttemptResult result) {
  switch (result) {
    case RuntimeAttemptResult::kSendFailed:
      return "Senden fehlgeschlagen";
    case RuntimeAttemptResult::kTimeout:
      return "Zeitueberschreitung";
    case RuntimeAttemptResult::kInvalidPacket:
      return "ungueltiges Paket";
    case RuntimeAttemptResult::kSuccess:
      return "erfolgreich";
  }
  return "unbekannt";
}

void GoodWeClient::logStatsIfDue() {
  if (successfulReads_ == 0 ||
      (successfulReads_ % config::AppConfig::kGoodWeStatsLogIntervalReads) != 0) {
    return;
  }
  core::Logger::infof(
      "GoodWe-Statistik: erfolgreich=%lu, fehlgeschlagene Zyklen=%lu, "
      "Retries=%lu, Timeouts=%lu, ungueltige Pakete=%lu, fremde UDP-Pakete=%lu, "
      "Antwortzeit zuletzt/avg/max=%lu/%lu/%lu ms",
      static_cast<unsigned long>(successfulReads_),
      static_cast<unsigned long>(failedReadCycles_),
      static_cast<unsigned long>(totalRetryAttempts_),
      static_cast<unsigned long>(runtimeTimeouts_),
      static_cast<unsigned long>(invalidRuntimePackets_),
      static_cast<unsigned long>(unexpectedSenderPackets_),
      static_cast<unsigned long>(lastResponseTimeMs_),
      static_cast<unsigned long>(averageResponseTimeMs()),
      static_cast<unsigned long>(maxResponseTimeMs_));
}

bool GoodWeClient::readGridPowerW(float& gridPowerW) {
  uint8_t response[260] = {0};
  size_t responseLen = 0;
  RuntimeAttemptResult lastResult = RuntimeAttemptResult::kTimeout;

  const uint8_t maxAttempts = config::AppConfig::kGoodWeRuntimeMaxAttempts;
  for (uint8_t attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (attempt > 1) {
      ++totalRetryAttempts_;
      delay(config::AppConfig::kGoodWeRuntimeRetryDelayMs);
    }

    uint32_t responseTimeMs = 0;
    lastResult =
        requestRuntimeData(response, sizeof(response), responseLen, responseTimeMs);
    if (lastResult != RuntimeAttemptResult::kSuccess) continue;

    if (config::AppConfig::kHexDumpEnabled) printHexDump(response, responseLen);

    if (responseLen < kRuntimeResponseMinLength ||
        response[0] != kRuntimeResponsePrefix0 ||
        response[1] != kRuntimeResponsePrefix1 ||
        response[2] != config::AppConfig::kGoodWeModbusAddress ||
        response[3] != kModbusReadHoldingRegisters ||
        response[4] != static_cast<uint8_t>(kRuntimeRegisterCount * 2U)) {
      ++invalidRuntimePackets_;
      lastResult = RuntimeAttemptResult::kInvalidPacket;
      continue;
    }

    const uint16_t expectedChecksum = checksum(response + 2, responseLen - 4);
    const uint16_t responseChecksum = static_cast<uint16_t>(
        static_cast<uint16_t>(response[responseLen - 2]) |
        (static_cast<uint16_t>(response[responseLen - 1]) << 8U));
    if (expectedChecksum != responseChecksum) {
      ++invalidRuntimePackets_;
      lastResult = RuntimeAttemptResult::kInvalidPacket;
      continue;
    }

    const size_t activePowerOffset =
        kRuntimePayloadOffset + kActivePowerPayloadOffset;
    gridPowerW = static_cast<float>(readInt16(response, activePowerOffset));

    ++successfulReads_;
    lastResponseTimeMs_ = responseTimeMs;
    totalResponseTimeMs_ += responseTimeMs;
    if (responseTimeMs > maxResponseTimeMs_) maxResponseTimeMs_ = responseTimeMs;
    if (attempt > 1) {
      core::Logger::infof(
          "GoodWe-Laufzeitdaten nach Wiederholversuch %u erfolgreich gelesen.",
          static_cast<unsigned>(attempt));
    }
    logStatsIfDue();
    return true;
  }

  ++failedReadCycles_;
  core::Logger::warnf(
      "Keine Laufzeitdaten vom Wechselrichter (%s, %u Versuche).",
      attemptResultToString(lastResult), static_cast<unsigned>(maxAttempts));
  return false;
}

uint16_t GoodWeClient::checksum(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x0001U) != 0U
                ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

int16_t GoodWeClient::readInt16(const uint8_t* data, size_t offset) {
  return static_cast<int16_t>((static_cast<uint16_t>(data[offset]) << 8U) |
                              static_cast<uint16_t>(data[offset + 1]));
}

void GoodWeClient::printHexDump(const uint8_t* data, size_t len) {
  constexpr size_t kBytesPerRow = 16;
  Serial.printf("[HexDump] GoodWe-Antwort (%u Byte):\n",
                static_cast<unsigned>(len));
  for (size_t i = 0; i < len; i += kBytesPerRow) {
    Serial.printf("  %04X: ", static_cast<unsigned>(i));
    const size_t rowEnd = (i + kBytesPerRow < len) ? i + kBytesPerRow : len;
    for (size_t j = i; j < rowEnd; ++j) Serial.printf("%02X ", data[j]);
    Serial.println();
  }
}

}  // namespace solarpilot::inverter
