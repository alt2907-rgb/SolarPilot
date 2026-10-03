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
  // All sources provide net power at the grid connection, never PV production
  // alone: positive export, negative import, finite watts. false means no
  // fresh measurement; implementations must not return cached data as fresh.
  virtual const char* sourceId() const = 0;
  virtual void resetConnection() = 0;
  // Synchronous safety hook during bounded network waits. Never re-enter the
  // source from this callback or invoke it from another task.
  virtual void setWaitHook(void (*hook)()) = 0;
  virtual uint32_t totalRetryAttempts() const = 0;
  virtual uint32_t runtimeTimeouts() const = 0;
};

}  // namespace solarpilot::inverter
