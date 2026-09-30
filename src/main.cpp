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
                        AppConfig::kSurplusSwitchOutputRetryDelayMs,
                        AppConfig::kSurplusSwitchFailSafeTimeoutMs},
    selectOutput());
InverterEndpoint inverter;
bool inverterReady = false;
uint32_t lastReadMs = 0;
uint32_t lastWifiReconnectAttemptMs = 0;
uint32_t lastGoodWeRecoveryAttemptMs = 0;
uint32_t wifiLostAtMs = 0;
uint8_t consecutiveGoodWeFailedCycles = 0;
bool wifiWasConnected = false;
bool shellyFailureSimulationEnabled = false;
bool goodWeLossSimulationEnabled = false;
bool wifiLossSimulationEnabled = false;

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

  Logger::info("[RECOVERY] Suche GoodWe-Wechselrichter...");
  goodWeClient.resetConnection();
  InverterEndpoint recoveredInverter;
  if (!goodWeClient.discover(recoveredInverter,
                             AppConfig::kInverterDiscoveryTimeoutMs) ||
      !goodWeClient.connect(recoveredInverter)) {
    Logger::warn("[RECOVERY] GoodWe noch nicht verfügbar; erneuter Versuch folgt.");
    return false;
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

void handleSerialCommand(const char* command, uint32_t nowMs) {
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
      "D = Shelly-Discovery | TW = WLAN-Verlustsimulation | "
      "TG = GoodWe-Verlustsimulation | TX = Shelly-Fehlersimulation");

  if (AppConfig::kShellyOutputEnabled) {
    Logger::info("[CONFIG] Ausgabe: Shelly Plug M Gen3 (LAN)");
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
  recoverGoodWe(millis());
  if (inverterReady) {
    Logger::info(
        "Milestone 3 aktiv: Netzleistung wird gelesen und Ausgang gesteuert.");
    // Rein informative Shelly-Discovery erst nach erfolgreichem GoodWe-Start.
    runShellyDiscoveryOnce();
  } else {
    Logger::warn(
        "[RECOVERY] Start ohne GoodWe; automatische Wiederherstellung aktiv.");
  }
}

void loop() {
  handleSerialInput();
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
        Logger::info("[RECOVERY] WLAN-Wiederverbindung wird versucht...");
        wifiManager.requestReconnect();
      }
    }
    delay(100);
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Logger::info("[RECOVERY] WLAN wiederhergestellt.");
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
    if (goodWeReadSucceeded) {
      consoleOutput.printGridPower(gridPowerW);
      surplusSwitchController.update(gridPowerW, nowMs);
    } else {
      Logger::warn("Netzleistung konnte nicht gelesen werden.");
      surplusSwitchController.noteReadFailure(nowMs);
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

