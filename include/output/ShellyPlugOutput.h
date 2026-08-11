#pragma once

#include <stdint.h>

#include "output/ISwitchOutput.h"

namespace solarpilot::output {

// Controls a Shelly Plug M Gen3 (or any Shelly Gen3 device) via local LAN
// using the Shelly RPC HTTP API. Only sends a command on state transitions.
class ShellyPlugOutput final : public ISwitchOutput {
 public:
  // host: IP address or hostname of the Shelly device (no http:// prefix)
  // switchId: Shelly switch channel, default 0
  // timeoutMs: HTTP request timeout
  ShellyPlugOutput(const char* host, uint8_t switchId = 0,
                   uint32_t timeoutMs = 3000);

  void setState(bool isOn) override;

 private:
  const char* host_;
  uint8_t switchId_;
  uint32_t timeoutMs_;

  bool hasState_ = false;
  bool isOn_ = false;
};

}  // namespace solarpilot::output
