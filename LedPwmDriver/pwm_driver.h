#pragma once

#include "pwm_config_buffer_manager.h"

#include <variant>

class IPwmDriver : public StateChangeListener {
 public:
  virtual ~IPwmDriver() = default;
  virtual void operator()(const std::variant<std::monostate, Program::ProgramEntry, Program>& new_state) = 0;
  virtual void waitForFlashAllowed() = 0;
  virtual void notifyFlashComplete() = 0;
};

IPwmDriver* ledPwmDriverInit();