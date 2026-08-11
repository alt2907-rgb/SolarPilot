#pragma once

namespace solarpilot::output {

class ISwitchOutput {
 public:
  virtual ~ISwitchOutput() = default;
  virtual void setState(bool isOn) = 0;
};

}  // namespace solarpilot::output
