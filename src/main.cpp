#include <Arduino.h>
#include <Update.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config/AppConfig.h"
#include "control/SurplusSwitchController.h"
#include "core/Logger.h"
#include "core/SystemHealth.h"
#include "core/WiFiManager.h"
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
GoodWeClient goodWeClient(AppConfig::kGoodWeDiscoveryPort,
                          AppConfig::kGoodWeRuntimePort);
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
bool otaUpdateInProgress = false;
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

void handleStatusPage() {
  const uint32_t nowMs = millis();
  const bool wifiConnected = wifiManager.isConnected();
  systemHealth.setGoodWeFailedCycles(consecutiveGoodWeFailedCycles);
  const auto health = systemHealth.snapshot(
      wifiConnected, inverterReady, AppConfig::kShellyOutputEnabled,
      surplusSwitchController.hasPendingOutputRetry(), nowMs,
      AppConfig::kSurplusSwitchFailSafeTimeoutMs);

  String html;
  html.reserve(3500);
  html += F("<!doctype html><html lang='de'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta http-equiv='refresh' content='5'><title>SolarPilot Status</title>"
            "<style>body{font-family:system-ui,sans-serif;max-width:760px;margin:24px auto;padding:0 16px;background:#f5f5f5;color:#222}"
            "h1{margin-bottom:4px}.card{background:white;border-radius:10px;padding:16px;margin:12px 0;box-shadow:0 1px 4px #bbb}"
            "table{width:100%;border-collapse:collapse}td{padding:7px 4px;border-bottom:1px solid #eee}td:first-child{font-weight:600}"
            ".ok{font-weight:700}</style></head><body><h1>SolarPilot</h1><div>Read-only Status &middot; Aktualisierung alle 5 s</div>");

  html += F("<div class='card'><h2>System</h2><table><tr><td>Gesamtzustand</td><td class='ok'>");
  html += SystemHealth::stateToString(health.overall);
  html += F("</td></tr><tr><td>GoodWe</td><td>");
  html += health.goodWeConnected ? "verbunden" : "nicht verbunden";
  html += F("</td></tr><tr><td>GoodWe-Fehlerzyklen</td><td>");
  html += String(health.goodWeFailedCycles);
  html += F("</td></tr><tr><td>GoodWe erfolgreiche Reads</td><td>");
  html += String(goodWeClient.successfulReads());
  html += F("</td></tr><tr><td>GoodWe Retry-Versuche</td><td>");
  html += String(goodWeClient.totalRetryAttempts());
  html += F("</td></tr><tr><td>GoodWe Timeouts</td><td>");
  html += String(goodWeClient.runtimeTimeouts());
  html += F("</td></tr><tr><td>Antwortzeit zuletzt / &Oslash; / max</td><td>");
  html += String(goodWeClient.lastResponseTimeMs());
  html += " / ";
  html += String(goodWeClient.averageResponseTimeMs());
  html += " / ";
  html += String(goodWeClient.maxResponseTimeMs());
  html += F(" ms</td></tr><tr><td>Ungueltige / fremde UDP-Pakete</td><td>");
  html += String(goodWeClient.invalidRuntimePackets());
  html += " / ";
  html += String(goodWeClient.unexpectedSenderPackets());
  html += F("</td></tr><tr><td>Letzter Messwert</td><td>");
  if (health.hasValidGoodWeReading) {
    html += String(health.lastValidGoodWeAgeMs / 1000U);
    html += " s alt";
  } else {
    html += "noch keiner";
  }
  html += F("</td></tr><tr><td>Netzleistung</td><td>");
  if (hasLatestGridPower) {
    html += String(latestGridPowerW, 1);
    html += " W";
  } else {
    html += "noch kein Wert";
  }
  html += F("</td></tr><tr><td>Ausgang</td><td>");
  html += surplusSwitchController.isOn() ? "EIN" : "AUS";
  html += F("</td></tr><tr><td>Shelly-Retry</td><td>");
  html += health.outputRetryPending ? "JA" : "NEIN";
  html += F("</td></tr></table></div>");

  html += F("<div class='card'><h2>WLAN</h2><table><tr><td>Status</td><td>");
  html += wifiConnected ? "verbunden" : "nicht verbunden";
  html += F("</td></tr>");
  if (wifiConnected) {
    html += F("<tr><td>SSID</td><td>");
    html += htmlEscape(WiFi.SSID());
    html += F("</td></tr><tr><td>RSSI</td><td>");
    html += String(WiFi.RSSI());
    html += F(" dBm</td></tr><tr><td>BSSID / AP</td><td>");
    html += htmlEscape(WiFi.BSSIDstr());
    html += F("</td></tr><tr><td>Kanal</td><td>");
    html += String(WiFi.channel());
    html += F("</td></tr><tr><td>ESP-IP</td><td>");
    html += WiFi.localIP().toString();
    html += F("</td></tr><tr><td>Gateway</td><td>");
    html += WiFi.gatewayIP().toString();
    html += F("</td></tr>");
  }
  html += F("</table></div><div class='card'><h2>Netzwerkdiagnose</h2><table>"
            "<tr><td>Diagnoseläufe bei GoodWe-Fehler</td><td>");
  html += String(networkProbeRuns);
  html += F("</td></tr><tr><td>Gateway TCP-Port 80 letzter Test</td><td>");
  if (networkProbeRuns == 0) {
    html += "noch keiner";
  } else {
    html += lastGatewayProbeOk ? "OK" : "FEHLER";
    html += " / ";
    html += String(lastGatewayProbeMs);
    html += " ms";
  }
  html += F("</td></tr><tr><td>Gateway TCP-Port 80 erfolgreich</td><td>");
  html += String(gatewayProbeSuccesses);
  html += " / ";
  html += String(networkProbeRuns);
  html += F("</td></tr><tr><td>Shelly letzter Test</td><td>");
  if (networkProbeRuns == 0) {
    html += "noch keiner";
  } else {
    html += lastShellyProbeOk ? "OK" : "FEHLER";
    html += " / ";
    html += String(lastShellyProbeMs);
    html += " ms";
  }
  html += F("</td></tr><tr><td>Shelly erfolgreich</td><td>");
  html += String(shellyProbeSuccesses);
  html += " / ";
  html += String(networkProbeRuns);
  html += F("</td></tr></table></div><div class='card'><h2>Firmware</h2>"
            "<p><a href='/update'>OTA-Update ueber WLAN</a></p></div></body></html>");
  statusWebServer.send(200, "text/html; charset=utf-8", html);
}

void handleOtaPage() {
  String html;
  html.reserve(1800);
  html += F("<!doctype html><html lang='de'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>SolarPilot Update</title>"
            "<style>body{font-family:system-ui,sans-serif;max-width:650px;margin:24px auto;padding:0 16px;background:#f5f5f5;color:#222}"
            ".card{background:white;border-radius:10px;padding:16px;margin:12px 0;box-shadow:0 1px 4px #bbb}"
            "button{padding:10px 16px;font-size:16px}</style></head><body>"
            "<h1>SolarPilot Firmware-Update</h1><div class='card'>"
            "<p>Lokales OTA-Update. Nur eine von PlatformIO erzeugte <b>firmware.bin</b> verwenden.</p>"
            "<p>Während des Updates die Stromversorgung nicht trennen.</p>"
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='firmware' accept='.bin,application/octet-stream' required><br><br>"
            "<button type='submit'>Firmware installieren</button></form></div>"
            "<p><a href='/'>Zurueck zum Status</a></p></body></html>");
  statusWebServer.send(200, "text/html; charset=utf-8", html);
}

void handleOtaUpload() {
  HTTPUpload& upload = statusWebServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaUpdateInProgress = true;
    otaUpdateSucceeded = false;
    Logger::infof("[OTA] Update gestartet: %s", upload.filename.c_str());
    if (!upload.filename.endsWith(".bin")) {
      Logger::warn("[OTA] Abgelehnt: Firmware-Datei muss auf .bin enden.");
      Update.abort();
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Logger::error("[OTA] Update konnte nicht initialisiert werden.");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() &&
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Logger::error("[OTA] Schreiben der Firmware fehlgeschlagen.");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.isRunning() && Update.end(true)) {
      otaUpdateSucceeded = true;
      Logger::infof("[OTA] Firmware erfolgreich geschrieben (%u Byte).",
                    static_cast<unsigned>(upload.totalSize));
    } else {
      Logger::error("[OTA] Firmware-Validierung/Abschluss fehlgeschlagen.");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Logger::warn("[OTA] Upload abgebrochen.");
  }
}

void handleOtaFinished() {
  const bool success = otaUpdateSucceeded && !Update.hasError();
  statusWebServer.send(
      success ? 200 : 500, "text/html; charset=utf-8",
      success
          ? "<!doctype html><html lang='de'><meta charset='utf-8'><body><h2>Update erfolgreich.</h2><p>SolarPilot startet neu...</p></body></html>"
          : "<!doctype html><html lang='de'><meta charset='utf-8'><body><h2>Update fehlgeschlagen.</h2><p>Die bisherige Firmware bleibt aktiv.</p><p><a href='/update'>Zurueck</a></p></body></html>");
  otaUpdateInProgress = false;
  if (success) {
    delay(500);
    ESP.restart();
  }
}

void startStatusWebServer() {
  if (statusWebServerStarted || !wifiManager.isConnected()) return;
  statusWebServer.on("/", HTTP_GET, handleStatusPage);
  statusWebServer.on("/status", HTTP_GET, handleStatusPage);
  statusWebServer.on("/update", HTTP_GET, handleOtaPage);
  statusWebServer.on("/update", HTTP_POST, handleOtaFinished, handleOtaUpload);
  statusWebServer.begin();
  statusWebServerStarted = true;
  Logger::infof("[WEB] Read-only Status: http://%s/",
                WiFi.localIP().toString().c_str());
}

void logSystemHealth(uint32_t nowMs, bool wifiConnected) {
  if (lastHealthLogMs != 0 &&
      static_cast<uint32_t>(nowMs - lastHealthLogMs) < kHealthLogIntervalMs) {
    return;
  }
  lastHealthLogMs = nowMs;
  systemHealth.setGoodWeFailedCycles(consecutiveGoodWeFailedCycles);
  const auto health = systemHealth.snapshot(
      wifiConnected, inverterReady, AppConfig::kShellyOutputEnabled,
      surplusSwitchController.hasPendingOutputRetry(), nowMs,
      AppConfig::kSurplusSwitchFailSafeTimeoutMs);
  Logger::infof(
      "[HEALTH] Gesamt=%s | WLAN=%s | GoodWe=%s | letzter Messwert=%s%lu ms | "
      "GoodWe-Fehlerzyklen=%lu | Shelly-Retry=%s",
      SystemHealth::stateToString(health.overall),
      health.wifiConnected ? "OK" : "AUS",
      health.goodWeConnected ? "OK" : "AUS",
      health.hasValidGoodWeReading ? "" : "noch keiner / ",
      static_cast<unsigned long>(health.lastValidGoodWeAgeMs),
      static_cast<unsigned long>(health.goodWeFailedCycles),
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
  goodWeClient.resetConnection();

  // Reuse the last known endpoint once. If communication still fails enough
  // to trigger another recovery, force broadcast discovery instead of
  // declaring the same unverified endpoint recovered forever.
  InverterEndpoint recoveredInverter = inverter;
  if (recoveredInverter.ip != IPAddress(0, 0, 0, 0) &&
      !forceGoodWeDiscoveryOnRecovery &&
      goodWeClient.connect(recoveredInverter)) {
    forceGoodWeDiscoveryOnRecovery = true;
    Logger::infof("[RECOVERY] Letzten GoodWe-Endpunkt einmalig wiederverwendet: %s",
                  recoveredInverter.ip.toString().c_str());
  } else {
    Logger::info("[RECOVERY] Suche GoodWe-Wechselrichter per Broadcast...");
    if (!goodWeClient.discover(recoveredInverter,
                               AppConfig::kInverterDiscoveryTimeoutMs) ||
        !goodWeClient.connect(recoveredInverter)) {
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
    return;
  }

  const String bssid = WiFi.BSSIDstr();
  Logger::infof(
      "[WIFI-DIAG] SSID=%s | RSSI=%d dBm | BSSID=%s | Kanal=%d | IP=%s | Gateway=%s",
      WiFi.SSID().c_str(), WiFi.RSSI(), bssid.c_str(), WiFi.channel(),
      WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
}

void handleSerialCommand(const char* command, uint32_t nowMs) {
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
      wifiManager.connect(AppConfig::kWifiSsid, AppConfig::kWifiPassword,
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
  handleSerialInput();
  if (statusWebServerStarted) statusWebServer.handleClient();
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
      goodWeClient.resetConnection();
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
          wifiManager.restartStation(AppConfig::kWifiSsid, AppConfig::kWifiPassword);
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
      !goodWeLossSimulationEnabled && goodWeClient.readGridPowerW(gridPowerW);
  if (testMode == TestMode::kInactive) {
    // Runtime retries can block for several seconds. Check safety with the
    // current time before accepting a new reading or running diagnostic probes.
    // A recovered reading must not cancel an unconfirmed fail-safe shutdown.
    surplusSwitchController.noteReadFailure(millis());
    if (surplusSwitchController.isFailSafeShutdownPending()) {
      return;
    }
    if (goodWeReadSucceeded) {
      systemHealth.noteGoodWeReading(millis());
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
        goodWeClient.resetConnection();
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

