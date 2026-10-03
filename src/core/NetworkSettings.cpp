#include "config/NetworkSettings.h"
#include <Preferences.h>
#include <cstring>
namespace solarpilot::config {
void NetworkSettings::load(const char* ssid, const char* password) {
  data_.version = 1;
  strlcpy(data_.ssid, ssid, sizeof(data_.ssid));
  strlcpy(data_.password, password, sizeof(data_.password));
  Preferences settings;
  if (!settings.begin("solar-wifi", true)) return;
  Data stored{};
  const bool valid = settings.getBytesLength("network") == sizeof(stored) &&
    settings.getBytes("network", &stored, sizeof(stored)) == sizeof(stored) &&
    stored.version == 1 && stored.ssid[0] && stored.ssid[32] == 0 && stored.password[64] == 0;
  if (valid) { data_ = stored; stored_ = true; }
  memset(&stored, 0, sizeof(stored));
  settings.end();
}
bool NetworkSettings::save(const String& ssid, const String& password) {
  Data candidate{}; candidate.version = 1;
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) return false;
  strlcpy(candidate.ssid, ssid.c_str(), sizeof(candidate.ssid));
  strlcpy(candidate.password, password.c_str(), sizeof(candidate.password));
  Preferences settings;
  if (!settings.begin("solar-wifi", false)) { memset(&candidate, 0, sizeof(candidate)); return false; }
  const bool saved = settings.putBytes("network", &candidate, sizeof(candidate)) == sizeof(candidate);
  settings.end();
  if (saved) { data_ = candidate; stored_ = true; }
  memset(&candidate, 0, sizeof(candidate));
  return saved;
}
}
