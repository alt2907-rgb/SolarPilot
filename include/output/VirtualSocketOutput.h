#pragma once

#include "output/ISwitchOutput.h"

namespace solarpilot::output {

class VirtualSocketOutput final : public ISwitchOutput {
 public:
  void setState(bool isOn) override;

 private:
  bool hasState_ = false;
  bool isOn_ = false;
};

}  // namespace solarpilot::output
