#include <Arduino.h>
#include <Update.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_timer.h>
#include <atomic>
#include <LittleFS.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config/AppConfig.h"
#include "config/AdminConfig.h"
#include "config/NetworkSettings.h"
#include "web/AdminPage.h"
#include "control/SurplusSwitchController.h"
#include "core/Logger.h"
#include "core/DeviceHistory.h"
#include "core/SystemHealth.h"
#include "core/WiFiManager.h"
#include "core/WiFiSetup.h"
#include "discovery/ShellyDeviceInfo.h"
#include "discovery/ShellyDiscovery.h"
#include "inverter/GoodWeClient.h"
#include "output/ConsoleOutput.h"
#include "output/ISwitchOutput.h"
#include "output/ShellyPlugOutput.h"
#include "output/VirtualSocketOutput.h"

using solarpilot::config::AppConfig;
using solarpilot::control::SurplusSwitchConfig;
using solarpilot::control::SurplusSwitchController;
using solarpilot::core::Logger;
using solarpilot::core::SystemHealth;
using solarpilot::core::WiFiManager;
using solarpilot::discovery::ShellyDeviceInfo;
using solarpilot::discovery::ShellyDiscovery;
using solarpilot::inverter::GoodWeClient;
using solarpilot::inverter::InverterEndpoint;
using solarpilot::output::ConsoleOutput;
using solarpilot::output::ISwitchOutput;
using solarpilot::output::ShellyPlugOutput;
using solarpilot::output::VirtualSocketOutput;

namespace {
WiFiManager wifiManager;
solarpilot::config::NetworkSettings networkSettings;
solarpilot::core::WiFiSetup wifiSetup;
solarpilot::core::DeviceHistory deviceHistory;
GoodWeClient goodWeClient(AppConfig::kGoodWeDiscoveryPort,
                          AppConfig::kGoodWeRuntimePort);
solarpilot::inverter::IInverterClient& measurementSource = goodWeClient;
ConsoleOutput consoleOutput;
VirtualSocketOutput virtualSocketOutput;
// shellyPlugOutput is constructed unconditionally but only used when
// kShellyOutputEnabled is true. selectOutput() is the sole gatekeeper.
// kShellyOutputEnabled is constexpr, so the unused branch is optimized away.
ShellyPlugOutput shellyPlugOutput(AppConfig::kShellyHost,
                                   AppConfig::kShellySwitchId,
                                   AppConfig::kShellyHttpTimeoutMs);

ISwitchOutput& selectOutput() {
  if (AppConfig::kShellyOutputEnabled) {
    return shellyPlugOutput;
  }
  return virtualSocketOutput;
}

SurplusSwitchController surplusSwitchController(
    SurplusSwitchConfig{AppConfig::kSurplusSwitchOnThresholdW,
                        AppConfig::kSurplusSwitchOffThresholdW,
                        AppConfig::kSurplusSwitchOnDelayMs,
                        AppConfig::kSurplusSwitchOffDelayMs,
                        AppConfig::kSurplusSwitchOutputRetryDelayMs,
                        AppConfig::kSurplusSwitchFailSafeTimeoutMs},
    selectOutput());
InverterEndpoint inverter;
bool inverterReady = false;
uint32_t lastReadMs = 0;
uint32_t lastWifiReconnectAttemptMs = 0;
uint32_t lastGoodWeRecoveryAttemptMs = 0;
uint32_t wifiLostAtMs = 0;
uint8_t wifiReconnectAttempts = 0;
constexpr uint8_t kWifiReconnectAttemptsBeforeRestart = 3;
uint8_t consecutiveGoodWeFailedCycles = 0;
bool wifiWasConnected = false;
bool forceGoodWeDiscoveryOnRecovery = false;
bool shellyFailureSimulationEnabled = false;
bool goodWeLossSimulationEnabled = false;
bool wifiLossSimulationEnabled = false;
SystemHealth systemHealth;
WebServer statusWebServer(80);
float latestGridPowerW = 0.0F;
bool hasLatestGridPower = false;
bool statusWebServerStarted = false;
std::atomic<bool> otaUpdateInProgress{false};
bool otaUpdateSucceeded = false;
uint32_t lastHealthLogMs = 0;
constexpr uint32_t kHealthLogIntervalMs = 60000U;
constexpr uint32_t kNetworkPathProbeTimeoutMs = 400U;
uint32_t networkProbeRuns = 0;
uint32_t gatewayProbeSuccesses = 0;
uint32_t shellyProbeSuccesses = 0;
uint32_t lastGatewayProbeMs = 0;
uint32_t lastShellyProbeMs = 0;
bool lastGatewayProbeOk = false;
bool lastShellyProbeOk = false;

enum class TestMode { kInactive, kManual, kAutomatic, kFailSafe };
enum class AutomaticTestPhase {
  kInactive,
  kBaseline,
  kOnQualification,
  kOnHold,
  kOffQualification
};

TestMode testMode = TestMode::kInactive;
AutomaticTestPhase automaticTestPhase = AutomaticTestPhase::kInactive;
float simulatedGridPowerW = 0.0F;
uint32_t automaticTestPhaseStartedMs = 0;
char serialLine[24] = {};
size_t serialLineLength = 0;
bool serialLineOverflow = false;

ShellyDiscovery shellyDiscovery;
Preferences shellyBindingPreferences;
String boundShellyDeviceId;

void runShellyDiscoveryOnce();
bool bindConfiguredShellyFromDiscovery();

bool probeTcpEndpoint(const IPAddress& ip, uint16_t port, uint32_t& elapsedMs) {
  WiFiClient client;
  client.setTimeout(kNetworkPathProbeTimeoutMs);
  const uint32_t startedMs = millis();
  const bool connected = client.connect(ip, port, kNetworkPathProbeTimeoutMs);
  elapsedMs = millis() - startedMs;
  client.stop();
  return connected;
}

void runNetworkPathDiagnostic() {
  if (WiFi.status() != WL_CONNECTED) {
    Logger::warn("[NET-DIAG] Uebersprungen: WLAN nicht verbunden.");
    return;
  }

  ++networkProbeRuns;
  const IPAddress gateway = WiFi.gatewayIP();
  lastGatewayProbeOk = probeTcpEndpoint(gateway, 80, lastGatewayProbeMs);
  if (lastGatewayProbeOk) ++gatewayProbeSuccesses;

  IPAddress shellyIp;
  lastShellyProbeOk = shellyIp.fromString(shellyPlugOutput.host()) &&
                      probeTcpEndpoint(shellyIp, 80, lastShellyProbeMs);
  if (lastShellyProbeOk) ++shellyProbeSuccesses;

  const bool wifiStillConnected = WiFi.status() == WL_CONNECTED;
  Logger::infof(
      "[NET-DIAG] GoodWe-Fehler | WLAN=%s | RSSI=%d dBm | Gateway:80=%s (%lu ms) | "
      "Shelly=%s (%lu ms) | Probes=%lu",
      wifiStillConnected ? "OK" : "AUS",
      wifiStillConnected ? WiFi.RSSI() : 0,
      lastGatewayProbeOk ? "OK" : "FEHLER",
      static_cast<unsigned long>(lastGatewayProbeMs),
      lastShellyProbeOk ? "OK" : "FEHLER",
      static_cast<unsigned long>(lastShellyProbeMs),
      static_cast<unsigned long>(networkProbeRuns));
}

String htmlEscape(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  return escaped;
}

uint32_t webTestStartedMs = 0;
bool webTestActive = false;
bool webGoodWeLossPending = false;
uint32_t webTestDurationMs = 120000U;
bool otaUploadAccepted = false;
bool otaEarlyResponseSent = false;
uint32_t otaReceivedBytes = 0;
uint32_t otaReportedBytes = 0;
std::atomic<uint32_t> otaLastActivityMs{0};
esp_timer_handle_t otaWatchdog = nullptr;
bool ensureOtaWatchdog() {
  if (otaWatchdog != nullptr) return true;
  esp_timer_create_args_t args{};
  args.callback = [](void*) {
    // WebServer's multipart reader can wait inside handleClient indefinitely.
    // This timer runs independently. No Logger/Update access from this task.
    // OTA is armed only after confirmed OFF; incomplete images are not selected.
    if (otaUpdateInProgress.load() &&
        static_cast<uint32_t>(millis() - otaLastActivityMs.load()) > 30000U) {
      ESP.restart();
    }
  };
  args.name = "ota-timeout";
  if (esp_timer_create(&args, &otaWatchdog) != ESP_OK) return false;
  if (esp_timer_start_periodic(otaWatchdog, 1000000) == ESP_OK) return true;
  esp_timer_delete(otaWatchdog);
  otaWatchdog = nullptr;
  return false;
}
String adminToken;
void handleSerialCommand(const char* command, uint32_t nowMs);
void endTestMode();
void logWifiLinkDiagnostic();

bool requireAdmin() {
  if (solarpilot::config::kAdminPassword[0] == '\0') {
    statusWebServer.send(503, "text/plain; charset=utf-8", "Adminzugang noch nicht eingerichtet. Lokale Admin-Zugangsdaten konfigurieren.");
    return false;
  }
  if (!statusWebServer.authenticate(solarpilot::config::kAdminUser, solarpilot::config::kAdminPassword)) {
    statusWebServer.requestAuthentication(DIGEST_AUTH, "SolarPilot Administration");
    return false;
  }
  return true;
}
bool requireActionToken() {
  if (!requireAdmin()) return false;
  if (adminToken.isEmpty() || statusWebServer.arg("token") != adminToken) {
    statusWebServer.send(403, "text/plain; charset=utf-8", "Aktion abgelehnt. Adminseite neu laden und erneut versuchen.");
    return false;
  }
  return true;
}
void handleSetupStatus() {
  if (!requireAdmin()) return;
  statusWebServer.sendHeader("Cache-Control", "no-store");
  String state=wifiSetup.statusJson(); state.remove(state.length()-1);
  state+=",\"stored\":"+String(networkSettings.stored()?"true":"false")+"}";
  statusWebServer.send(200,"application/json",state);
}
void handleSetupNetworks() {
  if (!requireAdmin()) return;
  statusWebServer.sendHeader("Cache-Control", "no-store");
  statusWebServer.send(200,"application/json",wifiSetup.networksJson());
}
void handleStatusPage() {
  statusWebServer.sendHeader("Cache-Control", "no-store");
  statusWebServer.send_P(200, "text/html; charset=utf-8", solarpilot::web::kPage);
}
void handleAdminPage() {
  if (requireAdmin()) handleStatusPage();
}
void handleStatusJson() {
  const uint32_t now = millis();
  systemHealth.setSourceFailedCycles(consecutiveGoodWeFailedCycles);
  const auto health = systemHealth.snapshot(wifiManager.isConnected(), inverterReady,
      AppConfig::kShellyOutputEnabled, surplusSwitchController.hasPendingOutputRetry(), now,
      AppConfig::kSurplusSwitchFailSafeTimeoutMs);
  const char* state = health.overall == solarpilot::core::HealthState::kOk ? "In Ordnung" :
      health.overall == solarpilot::core::HealthState::kDegraded ? "Eingeschraenkt" : "Nicht verfuegbar";
  String json = "{\"health\":\"" + String(state) + "\",\"power\":" + String(latestGridPowerW, 1);
  json += ",\"valid\":" + String(health.hasValidMeasurement && health.lastValidMeasurementAgeMs < AppConfig::kSurplusSwitchFailSafeTimeoutMs && !goodWeLossSimulationEnabled && !wifiSetup.active() && wifiManager.isConnected() ? "true" : "false");
  json += ",\"age\":" + (health.hasValidMeasurement ? String(health.lastValidMeasurementAgeMs) : String("null"));
  json += ",\"on\":" + String(surplusSwitchController.isOn() ? "true" : "false");
  json += ",\"real\":" + String(AppConfig::kShellyOutputEnabled ? "true" : "false");
  json += ",\"pending\":" + String(surplusSwitchController.isFailSafeShutdownPending() ? "true" : "false");
  json += ",\"retry\":" + String(health.outputRetryPending ? "true" : "false");
  json += ",\"rssi\":" + (wifiManager.isConnected() ? String(WiFi.RSSI()) : String("null"));
  json += ",\"goodwe\":" + String(inverterReady ? "true" : "false");
  json += ",\"source\":\"" + String(measurementSource.sourceId()) + "\"";
  json += ",\"source_connected\":" + String(inverterReady ? "true" : "false");
  json += ",\"test\":" + String(testMode != TestMode::kInactive || goodWeLossSimulationEnabled || wifiLossSimulationEnabled || shellyFailureSimulationEnabled || wifiSetup.active() ? "true" : "false");
  json += ",\"retries\":" + String(measurementSource.totalRetryAttempts());
  json += ",\"timeouts\":" + String(measurementSource.runtimeTimeouts());
  json += ",\"probes\":" + String(networkProbeRuns) + "}";
  statusWebServer.sendHeader("Cache-Control", "no-store");
  statusWebServer.send(200, "application/json", json);
}
void handleLogs() {
  if (!requireAdmin()) return;
  statusWebServer.sendHeader("Cache-Control", "no-store");
  statusWebServer.send(200, "application/json", "{\"token\":\"" + adminToken + "\",\"entries\":" + Logger::recentJson() + "}");
}
void handleHistoryStatus() {
  if (!requireAdmin()) return;
  statusWebServer.sendHeader("Cache-Control", "no-store");
  statusWebServer.send(200, "application/json", deviceHistory.statusJson());
}
void handleHistoryExport() {
  if (!requireAdmin()) return;
  const String segment = statusWebServer.arg("segment");
  if (segment.length() != 1 || segment[0] < '0' || segment[0] > '7') {
    statusWebServer.send(400, "text/plain", "Segment muss zwischen 0 und 7 liegen."); return;
  }
  if (!deviceHistory.ready()) {
    statusWebServer.send(503, "text/plain", "Aufzeichnungsspeicher nicht verfuegbar."); return;
  }
  File file = LittleFS.open(solarpilot::core::DeviceHistory::path(segment[0] - '0'), "r");
  if (!file) { statusWebServer.send(404, "text/plain", "Dieses Segment ist noch leer."); return; }
  statusWebServer.sendHeader("Cache-Control", "no-store");
  statusWebServer.sendHeader("Content-Disposition", "attachment; filename=solarpilot-" + segment + ".csv");
  statusWebServer.streamFile(file, "text/csv; charset=utf-8");
  file.close();
}
bool stopAllTests(bool clearOutputFailure = true) {
  endTestMode();
  goodWeLossSimulationEnabled = false;
  wifiLossSimulationEnabled = false;
  if (clearOutputFailure) {
    shellyFailureSimulationEnabled = false;
    shellyPlugOutput.setTestFailureEnabled(false);
  }
  webTestActive = false;
  webGoodWeLossPending = false;
  inverterReady = false;
  lastGoodWeRecoveryAttemptMs = 0;
  return surplusSwitchController.requestConfirmedOff();
}
void handleAdminAction() {
  if (!requireActionToken()) return;
  const String command = statusWebServer.arg("command");
  if (command == "wifi-setup") {
    if (otaUpdateInProgress.load() || !stopAllTests()) {
      statusWebServer.send(409,"text/plain","Einrichtung abgelehnt: AUS nicht bestaetigt oder Update aktiv."); return;
    }
    measurementSource.resetConnection(); wifiWasConnected=false;
    const bool started=wifiSetup.start();
    statusWebServer.send(started?200:503,"text/plain",started?
      "Einrichtung fuer zehn Minuten aktiv. Zugang steht im Einrichtungsbereich.":"Einrichtungs-WLAN konnte nicht starten."); return;
  }
  if (command == "wifi-scan" || command == "wifi-test" || command == "wifi-save" || command == "wifi-cancel") {
    bool ok=false;
    if (command=="wifi-scan") ok=wifiSetup.scan();
    if (command=="wifi-test") ok=wifiSetup.test(statusWebServer.arg("ssid"),statusWebServer.arg("password"));
    if (command=="wifi-save" && wifiSetup.canSave()) {
      ok=networkSettings.save(wifiSetup.candidateSsid(),wifiSetup.candidatePassword());
      if(ok) wifiSetup.requestClose();
    }
    if (command=="wifi-cancel" && wifiSetup.active()) { wifiSetup.requestClose(); ok=true; }
    statusWebServer.send(ok?200:409,"text/plain",ok?
      "Aktion angenommen. Ergebnis im Einrichtungsbereich pruefen.":"Aktion abgelehnt. Verbindungstest oder Eingaben pruefen."); return;
  }
  if (wifiSetup.active() && command != "history-flush") {
    statusWebServer.send(409,"text/plain","Zuerst die WLAN-Einrichtung beenden."); return;
  }
  if (command == "history-flush") {
    const bool saved = deviceHistory.flush();
    statusWebServer.send(saved ? 200 : 503, "text/plain; charset=utf-8",
      saved ? "Aufzeichnung gespeichert. Segmente koennen heruntergeladen werden." :
              "Aufzeichnung konnte nicht gespeichert werden. Speicherstatus pruefen.");
    return;
  }
  if (command == "diagnose") {
    logWifiLinkDiagnostic();
    runNetworkPathDiagnostic();
    statusWebServer.send(200, "text/plain; charset=utf-8", "Verbindungen geprüft. Ergebnisse stehen im Live-Protokoll.");
    return;
  }
  if (command == "stop" || command == "restart") {
    const bool confirmed = stopAllTests();
    if (!confirmed) {
      statusWebServer.send(409, "text/plain; charset=utf-8", "Tests beendet. AUS noch nicht bestätigt; weitere Versuche laufen. Neustart wurde nicht ausgeführt.");
      return;
    }
    statusWebServer.send(200, "text/plain; charset=utf-8", command == "restart" ? "AUS bestätigt. SolarPilot startet neu." : "Alle Tests beendet. Steckdose AUS bestätigt.");
    if (command == "restart") { deviceHistory.flush(); delay(300); ESP.restart(); }
    return;
  }
  if (command != "cycle" && command != "goodwe-loss" && command != "switch-failure" && command != "wifi-loss") {
    statusWebServer.send(400, "text/plain; charset=utf-8", "Unbekannte Aktion."); return;
  }
  if (!stopAllTests()) {
    statusWebServer.send(409, "text/plain; charset=utf-8", "Test abgelehnt: AUS konnte nicht bestätigt werden."); return;
  }
  if (command == "switch-failure" && !AppConfig::kShellyOutputEnabled) {
    statusWebServer.send(409, "text/plain; charset=utf-8", "Kein realer Shelly-Ausgang konfiguriert."); return;
  }
  if (command == "cycle") handleSerialCommand("TA", millis());
  if (command == "goodwe-loss") {
    handleSerialCommand("T100", millis());
    webGoodWeLossPending = true;
  }
  if (command == "switch-failure") { handleSerialCommand("TX", millis()); handleSerialCommand("TA", millis()); }
  webTestDurationMs = command == "wifi-loss" ? 40000U : 120000U;
  if (command == "wifi-loss") handleSerialCommand("TW", millis());
  webTestStartedMs = millis(); webTestActive = true;
  Logger::info("[WEB] Test gestartet; zeitlich begrenzter Test.");
  statusWebServer.send(200, "text/plain; charset=utf-8", "Test gestartet. Automatisches Ende nach spätestens zwei Minuten. Ergebnisse im Live-Protokoll.");
}
void handleOtaUpload() {
  const auto rejectTransport = []() {
    otaEarlyResponseSent = true;
    statusWebServer.send(409, "text/plain; charset=utf-8", "Update abgelehnt. Bitte das Live-Protokoll pruefen.");
    statusWebServer.client().stop();
  };
  HTTPUpload& upload = statusWebServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaUploadAccepted = false; otaUpdateSucceeded = false;
    otaEarlyResponseSent = false;
    otaReceivedBytes = otaReportedBytes = 0;
    // Stop rejected transports too: the multipart parser otherwise continues
    // consuming the complete body, even though no upload watchdog is armed.
    if (!requireActionToken()) { otaEarlyResponseSent = true; statusWebServer.client().stop(); return; }
    if (wifiSetup.active()) { Logger::warn("[OTA] Abgelehnt: WLAN-Einrichtung aktiv."); rejectTransport(); return; }
    if (!upload.filename.endsWith(".bin")) { Logger::warn("[OTA] Abgelehnt: Datei muss auf .bin enden."); rejectTransport(); return; }
    const bool offConfirmed = stopAllTests(false);
    deviceHistory.flush();
    shellyFailureSimulationEnabled = false;
    shellyPlugOutput.setTestFailureEnabled(false);
    if (!offConfirmed) { Logger::error("[OTA] Abgelehnt: physisches AUS nicht bestaetigt."); rejectTransport(); return; }
    if (!ensureOtaWatchdog()) { Logger::error("[OTA] Abgelehnt: Zeitueberwachung nicht verfuegbar."); rejectTransport(); return; }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { Logger::error("[OTA] Update konnte nicht gestartet werden."); rejectTransport(); return; }
    otaLastActivityMs = millis();
    otaUploadAccepted = true; otaUpdateInProgress = true;
    Logger::info("[OTA] AUS bestaetigt. Softwareuebertragung gestartet.");
  } else if (otaUploadAccepted && upload.status == UPLOAD_FILE_WRITE) {
    otaLastActivityMs = millis();
    if (otaReceivedBytes == 0) Logger::infof("[OTA] Erster Datenblock: %u Bytes; freier Heap: %u", upload.currentSize, ESP.getFreeHeap());
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.abort(); otaUploadAccepted = false; otaUpdateInProgress = false;
      Logger::error("[OTA] Schreibfehler. Update abgebrochen.");
    } else {
      otaReceivedBytes += upload.currentSize;
      if (otaReportedBytes == 0 || otaReceivedBytes - otaReportedBytes >= 65536U) {
        otaReportedBytes = otaReceivedBytes;
        // Update.write may buffer a partial flash sector before writing it.
        Logger::infof("[OTA] Vom Updater verarbeitet: %lu Bytes", static_cast<unsigned long>(otaReceivedBytes));
      }
    }
  } else if (otaUploadAccepted && upload.status == UPLOAD_FILE_END) {
    otaUpdateSucceeded = Update.end(true);
    otaUpdateInProgress = false;
    Logger::info(otaUpdateSucceeded ? "[OTA] Software erfolgreich geprueft." : "[OTA] Ungueltige Software; bisherige Version bleibt aktiv.");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (otaUploadAccepted) Update.abort();
    otaUploadAccepted = false; otaUpdateInProgress = false; otaUpdateSucceeded = false;
    Logger::infof("[OTA] Uebertragung abgebrochen; verarbeitet: %lu Bytes", static_cast<unsigned long>(otaReceivedBytes));
  }
}
void handleOtaFinished() {
  // Buffered multipart bytes can still complete parsing after transport.stop().
  // Do not send a second response on a rejected/already closed connection.
  if (otaEarlyResponseSent) return;
  if (!requireActionToken()) return;
  const bool success = otaUpdateSucceeded && !Update.hasError();
  statusWebServer.send(success ? 200 : 409, "text/plain; charset=utf-8", success ? "Software installiert. SolarPilot startet neu. Bitte die Seite gleich neu laden." : "Update abgelehnt oder fehlgeschlagen. Bitte das Live-Protokoll prüfen; die bisherige Software bleibt aktiv.");
  otaUpdateInProgress = false;
  if (success) { delay(500); ESP.restart(); }
}
void startStatusWebServer() {
  if (statusWebServerStarted || !wifiManager.isConnected()) return;
  adminToken = String(esp_random(), HEX) + String(esp_random(), HEX) + String(esp_random(), HEX) + String(esp_random(), HEX);
  statusWebServer.on("/", HTTP_GET, handleStatusPage);
  statusWebServer.on("/status", HTTP_GET, handleStatusPage);
  statusWebServer.on("/admin", HTTP_GET, handleAdminPage);
  statusWebServer.on("/api/status", HTTP_GET, handleStatusJson);
  statusWebServer.on("/api/logs", HTTP_GET, handleLogs);
  statusWebServer.on("/api/setup", HTTP_GET, handleSetupStatus);
  statusWebServer.on("/api/setup/networks", HTTP_GET, handleSetupNetworks);
  statusWebServer.on("/api/history", HTTP_GET, handleHistoryStatus);
  statusWebServer.on("/history.csv", HTTP_GET, handleHistoryExport);
  statusWebServer.on("/api/action", HTTP_POST, handleAdminAction);
  statusWebServer.on("/update", HTTP_GET, handleAdminPage);
  statusWebServer.on("/update", HTTP_POST, handleOtaFinished, handleOtaUpload);
  statusWebServer.begin(); statusWebServerStarted = true;
  Logger::infof("[WEB] Uebersicht: http://%s/", WiFi.localIP().toString().c_str());
}

void logSystemHealth(uint32_t nowMs, bool wifiConnected) {
  if (lastHealthLogMs != 0 &&
      static_cast<uint32_t>(nowMs - lastHealthLogMs) < kHealthLogIntervalMs) {
    return;
  }
  lastHealthLogMs = nowMs;
  systemHealth.setSourceFailedCycles(consecutiveGoodWeFailedCycles);
  const auto health = systemHealth.snapshot(
      wifiConnected, inverterReady, AppConfig::kShellyOutputEnabled,
      surplusSwitchController.hasPendingOutputRetry(), nowMs,
      AppConfig::kSurplusSwitchFailSafeTimeoutMs);
  Logger::infof(
      "[HEALTH] Gesamt=%s | WLAN=%s | GoodWe=%s | letzter Messwert=%s%lu ms | "
      "GoodWe-Fehlerzyklen=%lu | Shelly-Retry=%s",
      SystemHealth::stateToString(health.overall),
      health.wifiConnected ? "OK" : "AUS",
      health.sourceConnected ? "OK" : "AUS",
      health.hasValidMeasurement ? "" : "noch keiner / ",
      static_cast<unsigned long>(health.lastValidMeasurementAgeMs),
      static_cast<unsigned long>(health.sourceFailedCycles),
      health.outputRetryPending ? "JA" : "NEIN");
}

void loadShellyBinding() {
  if (!AppConfig::kShellyOutputEnabled) return;
  if (!shellyBindingPreferences.begin("shelly-bind", false)) {
    Logger::warn("[SHELLY-BINDING] Permanenter Speicher nicht verfügbar.");
    return;
  }
  boundShellyDeviceId = shellyBindingPreferences.getString("device-id", "");
  const String lastHost = shellyBindingPreferences.getString("last-host", "");
  if (!lastHost.isEmpty()) {
    shellyPlugOutput.setHost(lastHost.c_str());
    Logger::infof("[SHELLY-BINDING] Letzte bekannte IP geladen: %s", lastHost.c_str());
  }
  if (!boundShellyDeviceId.isEmpty()) {
    Logger::infof("[SHELLY-BINDING] Gespeicherte Geräte-ID geladen: %s",
                  boundShellyDeviceId.c_str());
  }
}

bool recoverGoodWe(uint32_t nowMs) {
  if (!wifiManager.isConnected()) {
    return false;
  }
  if (static_cast<uint32_t>(nowMs - lastGoodWeRecoveryAttemptMs) <
      AppConfig::kGoodWeRecoveryIntervalMs &&
      lastGoodWeRecoveryAttemptMs != 0) {
    return false;
  }
  lastGoodWeRecoveryAttemptMs = nowMs;

  if (goodWeLossSimulationEnabled) {
    Logger::warn("[TESTMODE] TG: GoodWe-Recovery simuliert weiterhin erfolglos.");
    return false;
  }

  Logger::info("[RECOVERY] Stelle GoodWe-Verbindung wieder her...");
  measurementSource.resetConnection();

  // Reuse the last known endpoint once. If communication still fails enough
  // to trigger another recovery, force broadcast discovery instead of
  // declaring the same unverified endpoint recovered forever.
  InverterEndpoint recoveredInverter = inverter;
  if (recoveredInverter.ip != IPAddress(0, 0, 0, 0) &&
      !forceGoodWeDiscoveryOnRecovery &&
      measurementSource.connect(recoveredInverter)) {
    forceGoodWeDiscoveryOnRecovery = true;
    Logger::infof("[RECOVERY] Letzten GoodWe-Endpunkt einmalig wiederverwendet: %s",
                  recoveredInverter.ip.toString().c_str());
  } else {
    Logger::info("[RECOVERY] Suche GoodWe-Wechselrichter per Broadcast...");
    if (!measurementSource.discover(recoveredInverter,
                               AppConfig::kInverterDiscoveryTimeoutMs) ||
        !measurementSource.connect(recoveredInverter)) {
      forceGoodWeDiscoveryOnRecovery = true;
      Logger::warn(
          "[RECOVERY] GoodWe noch nicht verfügbar; erneuter Versuch folgt.");
      return false;
    }
    forceGoodWeDiscoveryOnRecovery = false;
  }

  inverter = recoveredInverter;
  inverterReady = true;
  consecutiveGoodWeFailedCycles = 0;
  lastReadMs = nowMs - AppConfig::kReadIntervalMs;
  Logger::info("[RECOVERY] GoodWe-Verbindung wiederhergestellt.");
  return true;
}

void endTestMode() {
  testMode = TestMode::kInactive;
  automaticTestPhase = AutomaticTestPhase::kInactive;
  lastReadMs = millis() - AppConfig::kReadIntervalMs;
  Logger::info("[TESTMODE] Beendet – echte GoodWe-Daten aktiv.");
}

void startManualTest(float gridPowerW) {
  testMode = TestMode::kManual;
  automaticTestPhase = AutomaticTestPhase::kInactive;
  simulatedGridPowerW = gridPowerW;
  Logger::infof("[TESTMODE] Aktiv – simulierte Netzleistung: %.1f W",
                simulatedGridPowerW);
}

void startAutomaticTest(uint32_t nowMs) {
  testMode = TestMode::kAutomatic;
  automaticTestPhase = AutomaticTestPhase::kBaseline;
  automaticTestPhaseStartedMs = nowMs;
  Logger::info("[TESTMODE] TA gestartet: automatischer EIN/AUS-Zyklus.");
}

void startFailSafeTest() {
  if (!surplusSwitchController.isOn()) {
    Logger::warn(
        "[TESTMODE] TF nicht gestartet: Ausgang ist AUS. Zuerst mit T100 "
        "und normaler Einschaltverzögerung einschalten.");
    return;
  }

  testMode = TestMode::kFailSafe;
  automaticTestPhase = AutomaticTestPhase::kInactive;
  Logger::info(
      "[TESTMODE] TF gestartet: GoodWe-Leseausfall simuliert, kein "
      "Leistungswert wird eingespeist.");
}

void logWifiLinkDiagnostic() {
  if (WiFi.status() != WL_CONNECTED) {
    Logger::warn("[WIFI-DIAG] WLAN ist aktuell nicht verbunden.");
    // Manual, disconnected-only scan: keep safety checks around this blocking
    // diagnostic and never expose credentials or neighbouring network names.
    surplusSwitchController.noteReadFailure(millis());
    const int count = WiFi.scanNetworks(false, true);
    int matches = 0;
    int strongest = -127;
    int channel = 0;
    for (int i = 0; i < count; ++i) {
      if (WiFi.SSID(i) == networkSettings.ssid()) {
        ++matches;
        if (WiFi.RSSI(i) > strongest) {
          strongest = WiFi.RSSI(i);
          channel = WiFi.channel(i);
        }
      }
    }
    WiFi.scanDelete();
    surplusSwitchController.noteReadFailure(millis());
    Logger::infof("[WIFI-DIAG] WLAN-Suche: Ergebnis=%d, Ziel-Treffer=%d, bester Pegel=%d dBm, Kanal=%d",
                  count, matches, strongest, channel);
    return;
  }

  const String bssid = WiFi.BSSIDstr();
  Logger::infof(
      "[WIFI-DIAG] SSID=%s | RSSI=%d dBm | BSSID=%s | Kanal=%d | IP=%s | Gateway=%s",
      WiFi.SSID().c_str(), WiFi.RSSI(), bssid.c_str(), WiFi.channel(),
      WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
}

void handleSerialCommand(const char* command, uint32_t nowMs) {
  if (strcmp(command,"WC")==0) { wifiSetup.requestClose(); return; }
  if (wifiSetup.active()) {
    Logger::warn("[SETUP] Serielle Tests gesperrt. WC beendet die WLAN-Einrichtung."); return;
  }
  if (strcmp(command, "W") == 0 || strcmp(command, "w") == 0) {
    logWifiLinkDiagnostic();
    return;
  }
  if (strcmp(command, "TB") == 0) {
    if (!AppConfig::kShellyOutputEnabled) {
      Logger::warn("[SHELLY-BINDING] TB: Shelly-Ausgang ist nicht aktiviert.");
      return;
    }

    const String persistedDeviceId =
        shellyBindingPreferences.getString("device-id", "");
    if (persistedDeviceId.isEmpty()) {
      Logger::warn("[SHELLY-BINDING] TB: Keine Geräte-ID im NVS gespeichert.");
    } else {
      Logger::infof("[SHELLY-BINDING] TB: Im NVS gespeicherte Geräte-ID: %s",
                    persistedDeviceId.c_str());
    }
    Logger::infof("[SHELLY-BINDING] TB: Aktive Geräte-ID im RAM: %s",
                  boundShellyDeviceId.isEmpty() ? "<leer>"
                                                : boundShellyDeviceId.c_str());
    return;
  }

  if (strcmp(command, "D") == 0 || strcmp(command, "d") == 0) {
    Logger::info("[SHELLY-DISCOVERY] Manuell ausgelöst über Serial ('D').");
    runShellyDiscoveryOnce();
    return;
  }

  if (strcmp(command, "TW") == 0) {
    wifiLossSimulationEnabled = !wifiLossSimulationEnabled;
    Logger::infof("[TESTMODE] WLAN-Verlustsimulation %s.",
                  wifiLossSimulationEnabled ? "aktiviert" : "deaktiviert");
    if (wifiLossSimulationEnabled) {
      wifiManager.disconnectForTest();
    } else {
      lastWifiReconnectAttemptMs = 0;
    }
    return;
  }

  if (strcmp(command, "TG") == 0) {
    goodWeLossSimulationEnabled = !goodWeLossSimulationEnabled;
    Logger::infof("[TESTMODE] GoodWe-Verlustsimulation %s.",
                  goodWeLossSimulationEnabled ? "aktiviert" : "deaktiviert");
    if (goodWeLossSimulationEnabled) {
      inverterReady = true;
      consecutiveGoodWeFailedCycles = 0;
    } else {
      inverterReady = false;
      lastGoodWeRecoveryAttemptMs = 0;
    }
    return;
  }

  if (strcmp(command, "TX") == 0) {
    if (!AppConfig::kShellyOutputEnabled) {
      Logger::warn(
          "[TESTMODE] TX benötigt den konfigurierten Shelly-Ausgang.");
      return;
    }
    shellyFailureSimulationEnabled = !shellyFailureSimulationEnabled;
    shellyPlugOutput.setTestFailureEnabled(shellyFailureSimulationEnabled);
    Logger::infof("[TESTMODE] Shelly-Fehlersimulation %s.",
                  shellyFailureSimulationEnabled ? "aktiviert" : "deaktiviert");
    return;
  }

  if (strcmp(command, "T-") == 0) {
    endTestMode();
    return;
  }
  if (strcmp(command, "TA") == 0) {
    startAutomaticTest(nowMs);
    return;
  }
  if (strcmp(command, "TF") == 0) {
    startFailSafeTest();
    return;
  }

  if (command[0] == 'T' && command[1] != '\0') {
    char* end = nullptr;
    const float gridPowerW = strtof(command + 1, &end);
    if (*end == '\0' && isfinite(gridPowerW)) {
      startManualTest(gridPowerW);
      return;
    }
  }

  Logger::warn("Unbekannter Serial-Befehl.");
}

void handleSerialInput() {
  while (Serial.available() > 0) {
    const int incoming = Serial.read();
    if (incoming == '\r' || incoming == '\n') {
      if (serialLineOverflow) {
        Logger::warn("Serial-Befehl zu lang und verworfen.");
      } else if (serialLineLength > 0) {
        serialLine[serialLineLength] = '\0';
        handleSerialCommand(serialLine, millis());
      }
      serialLineLength = 0;
      serialLineOverflow = false;
    } else if (!serialLineOverflow) {
      if (serialLineLength + 1 < sizeof(serialLine)) {
        serialLine[serialLineLength++] = static_cast<char>(incoming);
      } else {
        serialLineOverflow = true;
      }
    }
  }
}

void advanceAutomaticTest(uint32_t nowMs) {
  if (testMode != TestMode::kAutomatic) {
    return;
  }

  uint32_t phaseDurationMs = 0;
  switch (automaticTestPhase) {
    case AutomaticTestPhase::kBaseline:
      phaseDurationMs = 2U * AppConfig::kReadIntervalMs;
      break;
    case AutomaticTestPhase::kOnQualification:
      phaseDurationMs = AppConfig::kSurplusSwitchOnDelayMs +
                        2U * AppConfig::kReadIntervalMs;
      break;
    case AutomaticTestPhase::kOnHold:
      phaseDurationMs = 2U * AppConfig::kReadIntervalMs;
      break;
    case AutomaticTestPhase::kOffQualification:
      phaseDurationMs = AppConfig::kSurplusSwitchOffDelayMs +
                        2U * AppConfig::kReadIntervalMs;
      break;
    case AutomaticTestPhase::kInactive:
      return;
  }

  if (static_cast<uint32_t>(nowMs - automaticTestPhaseStartedMs) <
      phaseDurationMs) {
    return;
  }

  automaticTestPhaseStartedMs = nowMs;
  switch (automaticTestPhase) {
    case AutomaticTestPhase::kBaseline:
      automaticTestPhase = AutomaticTestPhase::kOnQualification;
      Logger::info("[TESTMODE] TA: Überschuss oberhalb EIN-Schwelle.");
      break;
    case AutomaticTestPhase::kOnQualification:
      automaticTestPhase = AutomaticTestPhase::kOnHold;
      Logger::info("[TESTMODE] TA: EIN-Qualifikation beendet, kurzer Halt.");
      break;
    case AutomaticTestPhase::kOnHold:
      automaticTestPhase = AutomaticTestPhase::kOffQualification;
      Logger::info("[TESTMODE] TA: Wert unterhalb AUS-Schwelle.");
      break;
    case AutomaticTestPhase::kOffQualification:
      endTestMode();
      break;
    case AutomaticTestPhase::kInactive:
      break;
  }
}

float automaticTestGridPowerW() {
  switch (automaticTestPhase) {
    case AutomaticTestPhase::kBaseline:
    case AutomaticTestPhase::kOffQualification:
      return AppConfig::kSurplusSwitchOffThresholdW - 100.0F;
    case AutomaticTestPhase::kOnQualification:
    case AutomaticTestPhase::kOnHold:
      return AppConfig::kSurplusSwitchOnThresholdW + 100.0F;
    case AutomaticTestPhase::kInactive:
      return 0.0F;
  }
  return 0.0F;
}

bool bindConfiguredShellyFromDiscovery() {
  if (!AppConfig::kShellyOutputEnabled) return false;

  ShellyDeviceInfo devices[AppConfig::kShellyDiscoveryMaxDevices];
  const size_t count = shellyDiscovery.discover(
      devices, AppConfig::kShellyDiscoveryMaxDevices,
      AppConfig::kShellyDiscoveryMdnsTimeoutMs,
      AppConfig::kShellyDiscoveryHttpTimeoutMs);

  // Backward-compatible one-time migration: if no persistent binding exists,
  // identify the device currently owning the legacy configured host and save
  // its stable Shelly ID in ESP32 NVS. Future boots no longer depend on that IP.
  if (boundShellyDeviceId.isEmpty()) {
    for (size_t i = 0; i < count; ++i) {
      if (devices[i].infoRetrieved &&
          devices[i].ip.toString() == String(AppConfig::kShellyHost)) {
        boundShellyDeviceId = devices[i].id;
        shellyBindingPreferences.putString("device-id", boundShellyDeviceId);
        shellyBindingPreferences.putString("last-host", devices[i].ip.toString());
        Logger::infof("[SHELLY-BINDING] Gerät dauerhaft gebunden: id=%s",
                      boundShellyDeviceId.c_str());
        break;
      }
    }
  }

  if (boundShellyDeviceId.isEmpty()) {
    Logger::warn("[SHELLY-BINDING] Stabile Geräte-ID noch nicht ermittelt; feste Host-Konfiguration bleibt aktiv.");
    return false;
  }

  for (size_t i = 0; i < count; ++i) {
    const bool idMatches = devices[i].infoRetrieved && devices[i].id == boundShellyDeviceId;
    const bool hostnameMatches = devices[i].hostname == boundShellyDeviceId;
    if (idMatches || hostnameMatches) {
      const String currentHost = devices[i].ip.toString();
      shellyBindingPreferences.putString("last-host", currentHost);
      if (currentHost != String(shellyPlugOutput.host())) {
        Logger::infof("[SHELLY-BINDING] Neue IP für %s: %s",
                      boundShellyDeviceId.c_str(), currentHost.c_str());
        shellyPlugOutput.setHost(currentHost.c_str());
      } else {
        Logger::infof("[SHELLY-BINDING] Gerät bestätigt: %s unter %s",
                      boundShellyDeviceId.c_str(), currentHost.c_str());
      }
      return true;
    }
  }

  Logger::warn("[SHELLY-BINDING] Gebundenes Gerät aktuell nicht per mDNS gefunden; letzter Host bleibt aktiv.");
  return false;
}

// Milestone 4A: informative, one-shot mDNS discovery. Never switches
// shellyPlugOutput automatically and never blocks GoodWe/Surplus setup;
// on any failure it just logs and returns.
void runShellyDiscoveryOnce() {
  ShellyDeviceInfo devices[AppConfig::kShellyDiscoveryMaxDevices];
  const size_t count = shellyDiscovery.discover(
      devices, AppConfig::kShellyDiscoveryMaxDevices,
      AppConfig::kShellyDiscoveryMdnsTimeoutMs,
      AppConfig::kShellyDiscoveryHttpTimeoutMs);

  if (count == 0) {
    Logger::info("[SHELLY-DISCOVERY] Kein Shelly-Gerät im Netzwerk gefunden.");
    return;
  }

  Logger::infof("[SHELLY-DISCOVERY] %u Gerät(e) gefunden:",
               static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    const ShellyDeviceInfo& device = devices[i];
    Logger::infof("  [%u] %s (%s)", static_cast<unsigned>(i + 1),
                 device.hostname.c_str(), device.ip.toString().c_str());
    if (device.infoRetrieved) {
      Logger::infof("       id=%s mac=%s model=%s gen=%s", device.id.c_str(),
                   device.mac.c_str(), device.model.c_str(),
                   device.generation.c_str());
    } else {
      Logger::info("       Geräteinfo nicht abrufbar (nur mDNS-Daten).");
    }
  }
}
}  // namespace

void setup() {
  Logger::begin();
  networkSettings.load(AppConfig::kWifiSsid,AppConfig::kWifiPassword);
  deviceHistory.begin();
  measurementSource.setWaitHook([]() {
    if (testMode == TestMode::kInactive) {
      surplusSwitchController.noteReadFailure(millis());
    }
  });
  delay(200);
  Logger::info("SolarPilot startet...");
  Logger::info(
      "W = WLAN-Linkdiagnose | D = Shelly-Discovery | TW = WLAN-Verlustsimulation | "
      "TG = GoodWe-Verlustsimulation | TX = Shelly-Fehlersimulation | "
      "TB = Shelly-Bindung pruefen");

  if (AppConfig::kShellyOutputEnabled) {
    Logger::info("[CONFIG] Ausgabe: Shelly Plug M Gen3 (LAN)");
    loadShellyBinding();
  } else {
    Logger::info("[CONFIG] Ausgabe: VirtualSocketOutput (Testmodus)");
  }

  wifiWasConnected =
      wifiManager.connect(networkSettings.ssid(), networkSettings.password(),
                          AppConfig::kWifiConnectTimeoutMs);
  if (!wifiWasConnected) {
    Logger::warn(
        "[RECOVERY] Start ohne WLAN; SolarPilot versucht die Verbindung "
        "selbstständig wiederherzustellen.");
    wifiLostAtMs = millis();
    lastWifiReconnectAttemptMs = millis();
    return;
  }

  // Ein fehlender GoodWe beendet den Start nicht mehr dauerhaft. Der gleiche
  // Recovery-Pfad wird beim Boot und nach Laufzeitverlusten verwendet.
  lastGoodWeRecoveryAttemptMs = 0;
  startStatusWebServer();
  recoverGoodWe(millis());
  if (inverterReady) {
    Logger::info(
        "Milestone 3 aktiv: Netzleistung wird gelesen und Ausgang gesteuert.");
    // Keep the normal runtime path free of mDNS. The persisted last-known
    // Shelly endpoint loaded above is sufficient for switching. Discovery
    // remains available explicitly through the serial D command.
  } else {
    Logger::warn(
        "[RECOVERY] Start ohne GoodWe; automatische Wiederherstellung aktiv.");
  }
}

void loop() {
  if (!otaUpdateInProgress.load()) {
    if (testMode == TestMode::kInactive) surplusSwitchController.noteReadFailure(millis());
    const bool wifi = wifiManager.isConnected() && !wifiLossSimulationEnabled;
    const auto health = systemHealth.snapshot(wifi, inverterReady,
      AppConfig::kShellyOutputEnabled, surplusSwitchController.hasPendingOutputRetry(),
      millis(), AppConfig::kSurplusSwitchFailSafeTimeoutMs);
    const bool valid = health.hasValidMeasurement &&
      health.lastValidMeasurementAgeMs < AppConfig::kSurplusSwitchFailSafeTimeoutMs &&
      wifi && !goodWeLossSimulationEnabled && !wifiSetup.active();
    const uint32_t flags = (valid ? 1U : 0U) | (wifi ? 2U : 0U) |
      (inverterReady ? 4U : 0U) | (surplusSwitchController.isOn() ? 8U : 0U) |
      (surplusSwitchController.isFailSafeShutdownPending() ? 16U : 0U) |
      (surplusSwitchController.hasPendingOutputRetry() ? 32U : 0U) |
      ((testMode != TestMode::kInactive || wifiLossSimulationEnabled ||
        goodWeLossSimulationEnabled || shellyFailureSimulationEnabled || wifiSetup.active()) ? 64U : 0U);
    deviceHistory.tick({valid ? latestGridPowerW : 0.0F,
      wifi ? WiFi.RSSI() : 0, flags, measurementSource.runtimeTimeouts()});
  }
  handleSerialInput();
  if (statusWebServerStarted) statusWebServer.handleClient();
  wifiSetup.tick();
  if (wifiSetup.active()) {
    if (wifiSetup.finished()) {
      delay(300); wifiSetup.close();
      wifiManager.restartStation(networkSettings.ssid(),networkSettings.password());
      wifiWasConnected=false; inverterReady=false; wifiLostAtMs=millis();
      lastWifiReconnectAttemptMs=millis();
    }
    delay(5); return;
  }
  if (otaUpdateInProgress && static_cast<uint32_t>(millis() - otaLastActivityMs) > 30000U) {
    Update.abort(); otaUpdateInProgress = false; otaUploadAccepted = false;
    Logger::warn("[OTA] Zeitlimit erreicht; Regelung wird fortgesetzt.");
  }
  if (webTestActive && static_cast<uint32_t>(millis() - webTestStartedMs) >= webTestDurationMs) {
    stopAllTests(); Logger::info("[WEB] Testzeit abgelaufen. Tests beendet.");
  }
  if (webGoodWeLossPending && surplusSwitchController.isOn()) {
    endTestMode(); handleSerialCommand("TG", millis()); webGoodWeLossPending = false;
  }
  if (otaUpdateInProgress) {
    delay(5);
    return;
  }
  advanceAutomaticTest(millis());

  const uint32_t nowMs = millis();
  const bool wifiConnected =
      !wifiLossSimulationEnabled && wifiManager.isConnected();

  if (!wifiConnected) {
    // Auch ohne Netzwerk muss die bestehende 30-s-Sicherheitslogik weiter
    // laufen. Ein fehlgeschlagener Shelly-AUS-Befehl bleibt durch PR #15
    // ausstehend und wird nach Rückkehr des Netzes erneut versucht.
    surplusSwitchController.noteReadFailure(nowMs);

    if (wifiWasConnected) {
      wifiWasConnected = false;
      wifiLostAtMs = nowMs;
      inverterReady = false;
      measurementSource.resetConnection();
      Logger::warn("[RECOVERY] WLAN-Verbindung verloren.");
    }

    if (static_cast<uint32_t>(nowMs - lastWifiReconnectAttemptMs) >=
        AppConfig::kWifiReconnectIntervalMs) {
      lastWifiReconnectAttemptMs = nowMs;
      if (wifiLossSimulationEnabled) {
        Logger::warn(
            "[TESTMODE] TW: WLAN-Recovery bleibt während der Simulation "
            "absichtlich unterbrochen.");
        wifiManager.disconnectForTest();
      } else {
        ++wifiReconnectAttempts;
        if (wifiReconnectAttempts >= kWifiReconnectAttemptsBeforeRestart) {
          Logger::warn("[RECOVERY] Mehrere WLAN-Reconnects erfolglos; harter WLAN-Neustart.");
          wifiManager.restartStation(networkSettings.ssid(), networkSettings.password());
          wifiReconnectAttempts = 0;
        } else {
          Logger::info("[RECOVERY] WLAN-Wiederverbindung wird versucht...");
          wifiManager.requestReconnect();
        }
      }
    }
    delay(100);
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    wifiReconnectAttempts = 0;
    Logger::info("[RECOVERY] WLAN wiederhergestellt.");
    startStatusWebServer();
    lastGoodWeRecoveryAttemptMs = 0;

    // War das WLAN lange genug weg, muss ein eingeschalteter Ausgang zuerst
    // wirklich AUS bestätigt werden, bevor normale Überschussdaten wieder
    // verarbeitet werden.
    if (wifiLostAtMs != 0 &&
        static_cast<uint32_t>(nowMs - wifiLostAtMs) >=
            AppConfig::kSurplusSwitchFailSafeTimeoutMs &&
        surplusSwitchController.isOn()) {
      surplusSwitchController.noteReadFailure(nowMs);
      if (surplusSwitchController.isOn()) {
        delay(100);
        return;
      }
    }
  }

  // Runtime recovery deliberately avoids mDNS/device-info traffic. The last
  // known Shelly endpoint remains active; output retries are handled by the
  // controller without blocking GoodWe with discovery work.

  if (!inverterReady) {
    surplusSwitchController.noteReadFailure(nowMs);
    // Sobald der Fail-safe ein bestätigtes AUS verlangt, darf eine neue
    // GoodWe-Messung diesen Pending-Zustand nicht durch update() aufheben.
    // Erst nach erfolgreichem physischem AUS wird die Recovery fortgesetzt.
    if (surplusSwitchController.isFailSafeShutdownPending()) {
      delay(100);
      return;
    }
    recoverGoodWe(nowMs);
    delay(100);
    return;
  }
  if ((nowMs - lastReadMs) < AppConfig::kReadIntervalMs) {
    delay(50);
    return;
  }
  lastReadMs = nowMs;

  if (testMode == TestMode::kFailSafe) {
    surplusSwitchController.noteReadFailure(nowMs);
    if (!surplusSwitchController.isOn()) {
      endTestMode();
    }
    return;
  }

  float gridPowerW = 0.0F;
  const bool goodWeReadSucceeded =
      !goodWeLossSimulationEnabled && measurementSource.readGridPowerW(gridPowerW) &&
      isfinite(gridPowerW);
  if (testMode == TestMode::kInactive) {
    // Runtime retries can block for several seconds. Check safety with the
    // current time before accepting a new reading or running diagnostic probes.
    // A recovered reading must not cancel an unconfirmed fail-safe shutdown.
    surplusSwitchController.noteReadFailure(millis());
    if (surplusSwitchController.isFailSafeShutdownPending()) {
      return;
    }
    if (goodWeReadSucceeded) {
      systemHealth.noteMeasurement(millis());
      latestGridPowerW = gridPowerW;
      hasLatestGridPower = true;
      consoleOutput.printGridPower(gridPowerW);
      surplusSwitchController.update(gridPowerW, millis());
    } else {
      Logger::warn("Netzleistung konnte nicht gelesen werden.");
      runNetworkPathDiagnostic();
      surplusSwitchController.noteReadFailure(millis());
      if (consecutiveGoodWeFailedCycles < 0xFFU) {
        ++consecutiveGoodWeFailedCycles;
      }
      if (consecutiveGoodWeFailedCycles >=
          AppConfig::kGoodWeFailedCyclesBeforeRecovery) {
        Logger::warn(
            "[RECOVERY] GoodWe-Verbindung nach mehreren vollständig "
            "fehlgeschlagenen Lesezyklen als verloren markiert.");
        inverterReady = false;
        measurementSource.resetConnection();
        lastGoodWeRecoveryAttemptMs = 0;
      }
    }
    if (goodWeReadSucceeded) {
      consecutiveGoodWeFailedCycles = 0;
      forceGoodWeDiscoveryOnRecovery = false;
    }
    logSystemHealth(millis(), wifiConnected);
    return;
  }

  if (!goodWeReadSucceeded) {
    Logger::warn("Netzleistung konnte nicht gelesen werden.");
  }

  gridPowerW = testMode == TestMode::kManual ? simulatedGridPowerW
                                              : automaticTestGridPowerW();
  Logger::infof("[TESTMODE] Aktiv – simulierte Netzleistung: %.1f W",
                gridPowerW);
  surplusSwitchController.update(gridPowerW, nowMs);
}

