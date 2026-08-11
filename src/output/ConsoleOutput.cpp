#include "output/ConsoleOutput.h"

#include "core/Logger.h"

namespace solarpilot::output {

void ConsoleOutput::printGridPower(float gridPowerW) const {
  core::Logger::infof("Aktuelle Netzleistung: %.1f W", gridPowerW);
}

}  // namespace solarpilot::output
