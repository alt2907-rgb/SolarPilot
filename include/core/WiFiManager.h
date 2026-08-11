#pragma once

#include <stdint.h>

namespace solarpilot::core {

class WiFiManager {
 public:
  bool connect(const char* ssid, const char* password, uint32_t timeoutMs) const;
  bool isConnected() const;
};

}  // namespace solarpilot::core
