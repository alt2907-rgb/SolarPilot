#pragma once

#include <Arduino.h>

namespace solarpilot::inverter {

struct InverterEndpoint {
  IPAddress ip;
  uint16_t port;
  String serial;
};

class IInverterClient {
 public:
  virtual ~IInverterClient() = default;

  virtual bool discover(InverterEndpoint& endpoint, uint32_t timeoutMs) = 0;
  virtual bool connect(const InverterEndpoint& endpoint) = 0;
  virtual bool readGridPowerW(float& gridPowerW) = 0;
};

}  // namespace solarpilot::inverter
