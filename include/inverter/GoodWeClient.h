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
  static uint16_t checksum(const uint8_t* data, size_t len);
  static uint16_t readUInt16(const uint8_t* data, size_t offset);

  bool requestRuntimeData(uint8_t* responseBuffer, size_t bufferSize,
                          size_t& responseLen);

  uint16_t discoveryPort_;
  uint16_t runtimePort_;
  IPAddress inverterIp_;
  bool connected_;
  WiFiUDP udp_;
};

}  // namespace solarpilot::inverter
