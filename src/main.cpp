#include <Arduino.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config/AppConfig.h"
#include "control/SurplusSwitchController.h"
#include "core/Logger.h"
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
                        AppConfig::kSurplusSwitchFailSafeTimeoutMs},
    selectOutput());
InverterEndpoint inverter;
bool inverterReady = false;
uint32_t lastReadMs = 0;

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

void runShellyDiscoveryOnce();

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

void handleSerialCommand(const char* command, uint32_t nowMs) {
  if (strcmp(command, "D") == 0 || strcmp(command, "d") == 0) {
    Logger::info("[SHELLY-DISCOVERY] Manuell ausgelöst über Serial ('D').");
    runShellyDiscoveryOnce();
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
  Logger::info("D = Shelly-Discovery erneut ausführen");

  if (AppConfig::kShellyOutputEnabled) {
    Logger::info("[CONFIG] Ausgabe: Shelly Plug M Gen3 (LAN)");
  } else {
    Logger::info("[CONFIG] Ausgabe: VirtualSocketOutput (Testmodus)");
  }

  if (!wifiManager.connect(AppConfig::kWifiSsid, AppConfig::kWifiPassword,
                           AppConfig::kWifiConnectTimeoutMs)) {
    Logger::error("Setup abgebrochen: WLAN nicht verfügbar.");
    return;
  }

  // GoodWe zuerst zuverlässig initialisieren: die informative Shelly-mDNS-
  // Discovery läuft absichtlich erst danach (siehe runShellyDiscoveryOnce()
  // weiter unten), damit ein mDNS-/Discovery-Problem niemals den GoodWe-Start
  // verzögert oder beeinträchtigt.
  if (!goodWeClient.discover(inverter, AppConfig::kInverterDiscoveryTimeoutMs)) {
    Logger::error("Setup abgebrochen: GoodWe nicht gefunden.");
    return;
  }

  if (!goodWeClient.connect(inverter)) {
    Logger::error("Setup abgebrochen: Verbindung zum GoodWe fehlgeschlagen.");
    return;
  }

  inverterReady = true;
  Logger::info("Milestone 3 aktiv: Netzleistung wird gelesen und Ausgang gesteuert.");

  // Rein informativ und entkoppelt vom GoodWe-Kernbetrieb: ein Fehler hier
  // (mDNS, RPC) darf inverterReady/den Regelbetrieb nicht mehr beeinflussen.
  runShellyDiscoveryOnce();
}

void loop() {
  handleSerialInput();
  advanceAutomaticTest(millis());

  if (!inverterReady ||
      (!wifiManager.isConnected() && testMode != TestMode::kFailSafe)) {
    delay(1000);
    return;
  }

  const uint32_t nowMs = millis();
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
  const bool goodWeReadSucceeded = goodWeClient.readGridPowerW(gridPowerW);
  if (testMode == TestMode::kInactive) {
    if (goodWeReadSucceeded) {
      consoleOutput.printGridPower(gridPowerW);
      surplusSwitchController.update(gridPowerW, nowMs);
    } else {
      Logger::warn("Netzleistung konnte nicht gelesen werden.");
      surplusSwitchController.noteReadFailure(nowMs);
    }
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

