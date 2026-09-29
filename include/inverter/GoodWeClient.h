#pragma once

#include <WiFiUdp.h>

#include "inverter/IInverterClient.h"

namespace solarpilot::inverter {

class GoodWeClient final : public IInverterClient {
 public:
  GoodWeClient(uint16_t discoveryPort, uint16_t runtimePort);

  bool discover(InverterEndpoint& endpoint, uint32_t timeoutMs) override;
  bool connect(const InverterEndpoint& endpoint) override;
  bool readGridPowerW(float& gridPowerW) override;

 private:
  // Ergebnis eines einzelnen Laufzeit-Request-Versuchs, für Diagnose-Logging.
  enum class RuntimeAttemptResult {
    kSendFailed,
    kTimeout,
    kInvalidPacket,
    kSuccess,
  };

  static uint16_t checksum(const uint8_t* data, size_t len);
  static int16_t readInt16(const uint8_t* data, size_t offset);
  static void printHexDump(const uint8_t* data, size_t len);
  static const char* attemptResultToString(RuntimeAttemptResult result);

  void discardStalePackets();
  RuntimeAttemptResult requestRuntimeData(uint8_t* responseBuffer,
                                          size_t bufferSize,
                                          size_t& responseLen);
  void logStatsIfDue();

  uint16_t discoveryPort_;
  uint16_t runtimePort_;
  IPAddress inverterIp_;
  bool connected_;
  WiFiUDP udp_;

  // Einfache Laufzeit-Kommunikationsstatistik (nur Zähler, keine Web-UI).
  uint32_t successfulReads_;
  uint32_t failedReadCycles_;
  uint32_t totalRetryAttempts_;
};

}  // namespace solarpilot::inverter
