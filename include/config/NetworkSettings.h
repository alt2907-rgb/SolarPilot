#pragma once
#include <Arduino.h>
namespace solarpilot::config {
class NetworkSettings {
 public:
  void load(const char* ssid, const char* password);
  bool save(const String& ssid, const String& password);
  const char* ssid() const { return data_.ssid; }
  const char* password() const { return data_.password; }
  bool stored() const { return stored_; }
 private:
  struct Data { uint32_t version; char ssid[33]; char password[65]; } data_{};
  bool stored_ = false;
};
}
