#include <Arduino.h>

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

ShellyDiscovery shellyDiscovery;

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

// Manueller Entwicklungs-Trigger (Milestone 4A): 'd'/'D' über Serial löst
// erneut runShellyDiscoveryOnce() aus. Kein periodisches Polling, keine
// Auswirkung auf GoodWe-/Surplus-Ablauf in loop().
void handleSerialDiscoveryTrigger() {
  while (Serial.available() > 0) {
    const int incoming = Serial.read();
    if (incoming == 'd' || incoming == 'D') {
      Logger::info("[SHELLY-DISCOVERY] Manuell ausgelöst über Serial ('D').");
      runShellyDiscoveryOnce();
    }
  }
}

void loop() {
  handleSerialDiscoveryTrigger();

  if (!inverterReady || !wifiManager.isConnected()) {
    delay(1000);
    return;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - lastReadMs) < AppConfig::kReadIntervalMs) {
    delay(50);
    return;
  }
  lastReadMs = nowMs;

  float gridPowerW = 0.0F;
  if (goodWeClient.readGridPowerW(gridPowerW)) {
    consoleOutput.printGridPower(gridPowerW);
    surplusSwitchController.update(gridPowerW, nowMs);
  } else {
    Logger::warn("Netzleistung konnte nicht gelesen werden.");
    surplusSwitchController.noteReadFailure(nowMs);
  }
}

