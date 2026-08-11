#include "output/VirtualSocketOutput.h"

#include "core/Logger.h"

namespace solarpilot::output {

void VirtualSocketOutput::setState(bool isOn) {
  if (hasState_ && isOn_ == isOn) {
    return;
  }

  hasState_ = true;
  isOn_ = isOn;

  if (isOn_) {
    core::Logger::info("[CONTROL] Virtuelle Steckdose EIN");
  } else {
    core::Logger::info("[CONTROL] Virtuelle Steckdose AUS");
  }
}

}  // namespace solarpilot::output
