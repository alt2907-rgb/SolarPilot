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
      connected_(false),
      successfulReads_(0),
      failedReadCycles_(0),
      totalRetryAttempts_(0),
      unexpectedSenderPackets_(0),
      invalidRuntimePackets_(0),
      runtimeTimeouts_(0) {}

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
    uint8_t* responseBuffer, size_t bufferSize, size_t& responseLen) {
  responseLen = 0;
  if (!connected_) {
    return RuntimeAttemptResult::kSendFailed;
  }

  // Alte, noch in der Empfangswarteschlange liegende Pakete verwerfen, damit
  // eine veraltete Antwort nicht fälschlich der neuen Anfrage zugeordnet wird.
  discardStalePackets();

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
  const bool sent = (written == sizeof(request)) && (udp_.endPacket() != 0);
  if (!sent) {
    return RuntimeAttemptResult::kSendFailed;
  }

  const uint32_t startMs = millis();
  while ((millis() - startMs) < kRuntimeResponseTimeoutMs) {
    const int packetSize = udp_.parsePacket();
    if (packetSize <= 0) {
      delay(20);
      continue;
    }

    const IPAddress remoteIp = udp_.remoteIP();
    const uint16_t remotePort = udp_.remotePort();
    if (remoteIp != inverterIp_ || remotePort != runtimePort_) {
      ++unexpectedSenderPackets_;
      core::Logger::infof(
          "GoodWe-Diagnose: fremdes UDP-Paket verworfen (von %s:%u, %d Byte).",
          remoteIp.toString().c_str(), static_cast<unsigned>(remotePort),
          packetSize);
      udp_.flush();
      continue;
    }

    if (static_cast<size_t>(packetSize) > bufferSize) {
      core::Logger::error("Antwortpaket ist größer als der Puffer.");
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
    return RuntimeAttemptResult::kSuccess;
  }

  ++runtimeTimeouts_;
  return RuntimeAttemptResult::kTimeout;
}

void GoodWeClient::discardStalePackets() {
  int discardedCount = 0;
  int packetSize = udp_.parsePacket();
  while (packetSize > 0) {
    udp_.flush();
    ++discardedCount;
    packetSize = udp_.parsePacket();
  }
  if (discardedCount > 0) {
    core::Logger::infof(
        "Verworfene veraltete UDP-Pakete vor neuer Anfrage: %d",
        discardedCount);
  }
}

const char* GoodWeClient::attemptResultToString(RuntimeAttemptResult result) {
  switch (result) {
    case RuntimeAttemptResult::kSendFailed:
      return "Senden fehlgeschlagen";
    case RuntimeAttemptResult::kTimeout:
      return "Zeitüberschreitung";
    case RuntimeAttemptResult::kInvalidPacket:
      return "ungültiges Paket";
    case RuntimeAttemptResult::kUnexpectedSender:
      return "fremder UDP-Absender";
    case RuntimeAttemptResult::kSuccess:
      return "erfolgreich";
  }
  return "unbekannt";
}

void GoodWeClient::logStatsIfDue() {
  if (successfulReads_ == 0 ||
      (successfulReads_ % config::AppConfig::kGoodWeStatsLogIntervalReads) !=
          0) {
    return;
  }
  core::Logger::infof(
      "GoodWe-Statistik: erfolgreich=%lu, fehlgeschlagene Zyklen=%lu, "
      "Wiederholversuche gesamt=%lu, Timeouts=%lu, ungültige Pakete=%lu, "
      "fremde UDP-Pakete=%lu",
      static_cast<unsigned long>(successfulReads_),
      static_cast<unsigned long>(failedReadCycles_),
      static_cast<unsigned long>(totalRetryAttempts_),
      static_cast<unsigned long>(runtimeTimeouts_),
      static_cast<unsigned long>(invalidRuntimePackets_),
      static_cast<unsigned long>(unexpectedSenderPackets_));
}

bool GoodWeClient::readGridPowerW(float& gridPowerW) {
  uint8_t response[260] = {0};
  size_t responseLen = 0;
  RuntimeAttemptResult lastResult = RuntimeAttemptResult::kTimeout;

  const uint8_t maxAttempts = config::AppConfig::kGoodWeRuntimeMaxAttempts;
  for (uint8_t attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (attempt > 1) {
      ++totalRetryAttempts_;
      core::Logger::infof(
          "Wiederhole GoodWe-Laufzeitabfrage (Versuch %u von %u) nach: %s",
          static_cast<unsigned>(attempt), static_cast<unsigned>(maxAttempts),
          attemptResultToString(lastResult));
      delay(config::AppConfig::kGoodWeRuntimeRetryDelayMs);
    }

    responseLen = 0;
    lastResult = requestRuntimeData(response, sizeof(response), responseLen);
    if (lastResult != RuntimeAttemptResult::kSuccess) {
      continue;
    }

    if (config::AppConfig::kHexDumpEnabled) {
      printHexDump(response, responseLen);
    }

    if (responseLen < kRuntimeResponseMinLength) {
      core::Logger::warn("Antwort ist zu kurz für Netzleistungsdaten.");
      lastResult = RuntimeAttemptResult::kInvalidPacket;
      continue;
    }

    if (response[0] != kRuntimeResponsePrefix0 ||
        response[1] != kRuntimeResponsePrefix1 ||
        response[2] != config::AppConfig::kGoodWeModbusAddress ||
        response[3] != kModbusReadHoldingRegisters) {
      core::Logger::warn("Unerwarteter Antworttyp vom Wechselrichter.");
      lastResult = RuntimeAttemptResult::kInvalidPacket;
      continue;
    }
    if (response[4] != static_cast<uint8_t>(kRuntimeRegisterCount * 2U)) {
      core::Logger::warn("Unerwartete Payload-Länge in GoodWe-Antwort.");
      lastResult = RuntimeAttemptResult::kInvalidPacket;
      continue;
    }

    // responseLen enthält die komplette rohe UDP-Antwort inklusive AA55-Präfix.
    const uint16_t expectedChecksum = checksum(response + 2, responseLen - 4);
    const uint16_t responseChecksum = static_cast<uint16_t>(
        static_cast<uint16_t>(response[responseLen - 2]) |
        (static_cast<uint16_t>(response[responseLen - 1]) << 8U));
    if (expectedChecksum != responseChecksum) {
      core::Logger::warn("Ungültige Checksumme in GoodWe-Antwort.");
      lastResult = RuntimeAttemptResult::kInvalidPacket;
      continue;
    }

    const size_t activePowerOffset =
        kRuntimePayloadOffset + kActivePowerPayloadOffset;
    const int16_t activePower = readInt16(response, activePowerOffset);
    gridPowerW = static_cast<float>(activePower);

    ++successfulReads_;
    if (attempt > 1) {
      core::Logger::infof(
          "GoodWe-Laufzeitdaten nach Wiederholversuch %u erfolgreich gelesen.",
          static_cast<unsigned>(attempt));
    }
    logStatsIfDue();
    return true;
  }

  ++failedReadCycles_;
  core::Logger::infof(
      "Keine Laufzeitdaten vom Wechselrichter erhalten (letzter Fehler: %s, "
      "%u Versuche).",
      attemptResultToString(lastResult), static_cast<unsigned>(maxAttempts));
  return false;
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

void GoodWeClient::printHexDump(const uint8_t* data, size_t len) {
  constexpr size_t kBytesPerRow = 16;
  Serial.printf("[HexDump] GoodWe-Antwort (%u Byte):\n",
                static_cast<unsigned>(len));
  for (size_t i = 0; i < len; i += kBytesPerRow) {
    Serial.printf("  %04X: ", static_cast<unsigned>(i));
    const size_t rowEnd = (i + kBytesPerRow < len) ? i + kBytesPerRow : len;
    for (size_t j = i; j < rowEnd; ++j) {
      Serial.printf("%02X ", data[j]);
    }
    // Padding so ASCII column is always aligned
    for (size_t j = rowEnd; j < i + kBytesPerRow; ++j) {
      Serial.print("   ");
    }
    Serial.print(" |");
    for (size_t j = i; j < rowEnd; ++j) {
      const uint8_t b = data[j];
      Serial.print((b >= 0x20U && b < 0x7FU) ? static_cast<char>(b) : '.');
    }
    Serial.println('|');
  }
}

}  // namespace solarpilot::inverter
