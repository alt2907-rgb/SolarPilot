#include <Arduino.h>

#include "config/AppConfig.h"
#include "control/SurplusSwitchController.h"
#include "core/Logger.h"
#include "core/WiFiManager.h"
#include "inverter/GoodWeClient.h"
#include "output/ConsoleOutput.h"
#include "output/VirtualSocketOutput.h"

using solarpilot::config::AppConfig;
using solarpilot::control::SurplusSwitchConfig;
using solarpilot::control::SurplusSwitchController;
using solarpilot::core::Logger;
using solarpilot::core::WiFiManager;
using solarpilot::inverter::GoodWeClient;
using solarpilot::inverter::InverterEndpoint;
using solarpilot::output::ConsoleOutput;
using solarpilot::output::VirtualSocketOutput;

namespace {
WiFiManager wifiManager;
GoodWeClient goodWeClient(AppConfig::kGoodWeDiscoveryPort,
                          AppConfig::kGoodWeRuntimePort);
ConsoleOutput consoleOutput;
VirtualSocketOutput virtualSocketOutput;
SurplusSwitchController surplusSwitchController(
    SurplusSwitchConfig{AppConfig::kSurplusSwitchOnThresholdW,
                        AppConfig::kSurplusSwitchOffThresholdW,
                        AppConfig::kSurplusSwitchOnDelayMs,
                        AppConfig::kSurplusSwitchOffDelayMs},
    virtualSocketOutput);
InverterEndpoint inverter;
bool inverterReady = false;
uint32_t lastReadMs = 0;
}  // namespace

void setup() {
  Logger::begin();
  delay(200);
  Logger::info("SolarPilot startet...");

  if (!wifiManager.connect(AppConfig::kWifiSsid, AppConfig::kWifiPassword,
                           AppConfig::kWifiConnectTimeoutMs)) {
    Logger::error("Setup abgebrochen: WLAN nicht verfügbar.");
    return;
  }

  if (!goodWeClient.discover(inverter, AppConfig::kInverterDiscoveryTimeoutMs)) {
    Logger::error("Setup abgebrochen: GoodWe nicht gefunden.");
    return;
  }

  if (!goodWeClient.connect(inverter)) {
    Logger::error("Setup abgebrochen: Verbindung zum GoodWe fehlgeschlagen.");
    return;
  }

  inverterReady = true;
  Logger::info("Milestone 2 aktiv: Netzleistung wird gelesen und simuliert geschaltet.");
}

void loop() {
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
  }
}
