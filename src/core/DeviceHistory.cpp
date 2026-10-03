#include "core/DeviceHistory.h"
#include <LittleFS.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include "core/Logger.h"

namespace solarpilot::core {
namespace {
constexpr unsigned kSegments = 8;
constexpr size_t kSegmentBytes = 32768;
constexpr size_t kBufferBytes = 2048;
constexpr char kHeader[] = "boot,seconds,power_w,rssi,flags,timeouts\n";
// Only virgin storage may be initialized automatically. A failed mount of
// nonempty storage is reported, never formatted (preserve recovery evidence).
bool blankPartition() {
  const auto* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!p) return false;
  uint8_t bytes[256];
  for (size_t offset = 0; offset < p->size; offset += sizeof(bytes)) {
    const size_t n = std::min<size_t>(sizeof(bytes), p->size - offset);
    if (esp_partition_read(p, offset, bytes, n) != ESP_OK) return false;
    for (size_t i = 0; i < n; ++i) if (bytes[i] != 0xFF) return false;
    delay(0);
  }
  return true;
}
}
String DeviceHistory::path(unsigned index) {
  return "/history-" + String(index) + ".csv";
}
void DeviceHistory::begin() {
  boot_ = esp_random();
  buffer_.reserve(kBufferBytes);
  ready_ = LittleFS.begin(false);
  if (!ready_ && blankPartition()) ready_ = LittleFS.begin(true);
  if (!ready_) { failed_ = true; Logger::warn("[HISTORY] Speicher nicht verfuegbar; keine Daten geloescht."); return; }
  // File modification time is unavailable without an RTC. Persist a tiny
  // cursor together with rotation only, not with every sample.
  File cursor = LittleFS.open("/history-active", "r");
  if (cursor) { active_ = cursor.parseInt() % kSegments; cursor.close(); }
  lastFlushMs_ = millis();
  Logger::info("[HISTORY] Eigenstaendige Aufzeichnung bereit.");
}
bool DeviceHistory::flush() {
  if (!ready_) return false;
  if (buffer_.isEmpty()) return true;
  File file = LittleFS.open(path(active_), "a");
  if (!file) { failed_ = true; return false; }
  if (file.size() + buffer_.length() > kSegmentBytes) {
    file.close();
    const unsigned next = (active_ + 1) % kSegments;
    // Cursor first: if power fails, the next boot never appends to the old
    // full segment. Loss of the oldest segment is acceptable ring behavior.
    File cursor = LittleFS.open("/history-active", "w");
    if (!cursor || cursor.print(next) == 0) { failed_ = true; return false; }
    cursor.close();
    active_ = next;
    file = LittleFS.open(path(active_), "w");
    if (!file) { failed_ = true; return false; }
  }
  if (file.size() == 0 && file.print(kHeader) != strlen(kHeader)) {
    failed_ = true; return false;
  }
  const size_t written = file.print(buffer_);
  file.close();
  // Avoid retrying a partially written batch as duplicate complete records.
  if (written != buffer_.length()) { failed_ = true; buffer_ = ""; return false; }
  buffer_ = "";
  lastFlushMs_ = millis();
  return true;
}
void DeviceHistory::tick(const HistorySample& s) {
  if (!ready_ || failed_) return;
  const uint32_t now = millis();
  const bool changed = s.flags != lastFlags_;
  if ((changed && static_cast<uint32_t>(now - lastSampleMs_) >= 1000U) ||
      static_cast<uint32_t>(now - lastSampleMs_) >= 60000U || lastFlags_ == UINT32_MAX) {
    char row[128];
    snprintf(row, sizeof(row), "%08lx,%llu,%.1f,%d,%lu,%lu\n",
      static_cast<unsigned long>(boot_),
      static_cast<unsigned long long>(esp_timer_get_time() / 1000000),
      s.power, s.rssi, static_cast<unsigned long>(s.flags),
      static_cast<unsigned long>(s.timeouts));
    if (buffer_.length() + strlen(row) <= kBufferBytes) buffer_ += row;
    else ++dropped_;
    lastSampleMs_ = now; lastFlags_ = s.flags;
  }
  if (static_cast<uint32_t>(now - lastFlushMs_) >= 300000U ||
      buffer_.length() > kBufferBytes - 128) flush();
}
String DeviceHistory::statusJson() const {
  String files="[";
  for(unsigned i=0;i<8;++i) {
    if(i) files+=',';
    auto file=ready_?LittleFS.open(path(i),"r"):File();
    files+="{\"segment\":"+String(i)+",\"bytes\":"+String(file?file.size():0)+"}";
  }
  files+=']';
  return "{\"ready\":" + String(ready_ ? "true" : "false") +
    ",\"error\":" + String(failed_ ? "true" : "false") +
    ",\"active\":" + String(active_) + ",\"segments\":8,\"buffered_bytes\":" +
    String(buffer_.length()) + ",\"dropped\":" + String(dropped_) + ",\"files\":"+files+"}";
}
}
