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
  void resetConnection();

  uint32_t successfulReads() const { return successfulReads_; }
  uint32_t failedReadCycles() const { return failedReadCycles_; }
  uint32_t totalRetryAttempts() const { return totalRetryAttempts_; }
  uint32_t runtimeTimeouts() const { return runtimeTimeouts_; }
  uint32_t invalidRuntimePackets() const { return invalidRuntimePackets_; }
  uint32_t unexpectedSenderPackets() const { return unexpectedSenderPackets_; }
  uint32_t lastResponseTimeMs() const { return lastResponseTimeMs_; }
  uint32_t maxResponseTimeMs() const { return maxResponseTimeMs_; }
  uint32_t averageResponseTimeMs() const {
    return successfulReads_ == 0 ? 0 : totalResponseTimeMs_ / successfulReads_;
  }

 private:
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

  RuntimeAttemptResult requestRuntimeData(uint8_t* responseBuffer,
                                          size_t bufferSize,
                                          size_t& responseLen,
                                          uint32_t& responseTimeMs);
  void logStatsIfDue();

  uint16_t discoveryPort_;
  uint16_t runtimePort_;
  IPAddress inverterIp_;
  bool connected_;
  WiFiUDP udp_;

  uint32_t successfulReads_;
  uint32_t failedReadCycles_;
  uint32_t totalRetryAttempts_;
  uint32_t unexpectedSenderPackets_;
  uint32_t invalidRuntimePackets_;
  uint32_t runtimeTimeouts_;
  uint32_t lastResponseTimeMs_;
  uint32_t maxResponseTimeMs_;
  uint32_t totalResponseTimeMs_;
};

}  // namespace solarpilot::inverter
