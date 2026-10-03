#pragma once
#include <Arduino.h>

namespace solarpilot::core {
struct HistorySample {
  float power;
  int rssi;
  uint32_t flags;
  uint32_t timeouts;
};
class DeviceHistory {
 public:
  void begin();
  void tick(const HistorySample& sample);
  bool flush();
  String statusJson() const;
  bool ready() const { return ready_; }
  static String path(unsigned index);
 private:
  bool ready_ = false;
  bool failed_ = false;
  uint32_t boot_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastFlushMs_ = 0;
  uint32_t lastFlags_ = UINT32_MAX;
  uint32_t dropped_ = 0;
  unsigned active_ = 0;
  String buffer_;
};
}
