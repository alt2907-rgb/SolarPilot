#pragma once

namespace solarpilot::output {

class ISwitchOutput {
 public:
  virtual ~ISwitchOutput() = default;
  virtual bool setState(bool isOn) = 0;
};

}  // namespace solarpilot::output
