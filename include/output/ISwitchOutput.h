#pragma once

namespace solarpilot::output {

class ISwitchOutput {
 public:
  virtual ~ISwitchOutput() = default;
  virtual bool setState(bool isOn) = 0;
  virtual bool confirmOff() { return setState(false); }
};

}  // namespace solarpilot::output
