#pragma once
#include <Arduino.h>
namespace solarpilot::core {
class WiFiSetup {
 public:
  bool start();
  void tick();
  bool scan();
  bool test(String ssid, String password);
  bool active() const { return active_; }
  bool finished() const { return closeRequested_; }
  bool canSave() const;
  void close();
  void requestClose() { closeRequested_ = true; }
  String statusJson() const;
  String networksJson();
  const String& candidateSsid() const { return ssid_; }
  const String& candidatePassword() const { return password_; }
 private:
  bool active_ = false, scanning_ = false, testing_ = false, tested_ = false;
  bool closeRequested_ = false, wasConnected_ = false;
  uint32_t startedMs_ = 0, testStartedMs_ = 0, sampleMs_ = 0, connectedMs_ = 0;
  unsigned drops_ = 0;
  String apName_, apPassword_, ssid_, password_;
};
}
