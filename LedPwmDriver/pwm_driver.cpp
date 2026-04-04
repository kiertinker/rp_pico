#include "pwm_driver.h"
#include "pwm_config_buffer_manager.h"

#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#include <stdio.h>

#include <functional>
#include <memory>
#include <variant>
    
namespace {

std::function<void()> globalLambda;

constexpr uint PWM_FREQUENCY = 1000; // 1kHz
struct PwmSetting {
  std::variant<std::monostate, Program::ProgramEntry, Program> setting_;
  std::array<unsigned char, PROGRAM_SIZE> buffer_;
  PwmSetting() : setting_(std::monostate{}) {}
  void update(const std::variant<std::monostate, Program::ProgramEntry, Program>& setting) {
    if (std::holds_alternative<std::monostate>(setting)) {
      setting_ = std::monostate{};
    } else if (std::holds_alternative<Program::ProgramEntry>(setting)) {
      // Copy static color data to our internal buffer and construct a ProgramEntry for use in updating PWM levels.
      std::get<Program::ProgramEntry>(setting).copyTo(buffer_.data());
      setting_.emplace<Program::ProgramEntry>(buffer_.data(), 0);
    } else if (std::holds_alternative<Program>(setting)) {
      // Copy program data to our internal buffer and construct a Program for use in running a PWM level program.
      std::get<Program>(setting).copyTo(buffer_.data());
      setting_.emplace<Program>(buffer_.data(), 0);
    }
  }
};

class LedPwmDriver : public IPwmDriver {
 private:
  void init_pwm(uint pin, uint& slice);
  Mutex pwm_mutex_; // Mutex to protect concurrent access to PWM hardware from multiple cores or interrupts.
  // Flag to track which buffer is currently active for PWM output, used for double buffering of program data
  // to ensure consistent updates to the PWM hardware without glitches.   
  bool buffer1_active_ = true;
  // Flag to indicate that a new program has been loaded and is pending to be applied to the PWM hardware on
  // the next cycle, used in conjunction with double buffering to ensure smooth transitions between programs without glitches.
  // This flag is set to true whenever a new program is loaded via the state change listener and is reset to false
  // after the new program has been applied to the PWM hardware in the main loop or an appropriate update function.  
  bool new_program_pending_ = false;
  // Flag to indicate that we need to go into a sram only mode to allow flash writes to complete without interference
  // from reading instructions from flash.
  volatile bool waiting_for_flash_allowed_ = false;
  // Flag to indicate that a flash write has completed and the pwm drive can proceed as normal.
  // This flag is used in conjunction with waiting_for_flash_allowed_ to manage transitions between normal operation and sram-only mode for flash writes.
  volatile bool flash_write_complete_ = false;
  // Double buffers for PWM settings to allow glitch-free updates to the PWM hardware when changing programs or static settings.
  PwmSetting buffer1_, buffer2_;

  void monostateDriveLights();
  void staticColorDriveLights(const Program::ProgramEntry& static_settings);
  void programDriveLights(const Program& program);
  bool blockForInterrupt();

 public:
  LedPwmDriver();
  void operator()(const std::variant<std::monostate, Program::ProgramEntry, Program>& new_state) override;
  void waitForFlashAllowed() override;
  void notifyFlashComplete() override;
  void driveLights();
  static void globalLambdaWrapper();
};

struct RGBWChannel {
  uint pin_r, pin_g, pin_b, pin_w;
  uint slice_r, slice_g, slice_b, slice_w;
};
    
RGBWChannel channels[2];

void __not_in_flash_func(waitForFlashComplete)(volatile bool& waiting_for_flash_allowed, volatile bool& flash_write_complete) {
  // Now we are in code running from SRAM, so we can safely let the flash write to commense.
  waiting_for_flash_allowed = false;
  // We now wait until the flash write is complete before allowing the PWM driver to resume normal operation,
  // which may involve reading instructions from flash.
  while (!flash_write_complete);
}
   
}  // namespace

void LedPwmDriver::init_pwm(uint pin, uint& slice) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  slice = pwm_gpio_to_slice_num(pin);
  
  uint clock_div = clock_get_hz(clk_sys) / (PWM_FREQUENCY * 4096);
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, clock_div);
  pwm_config_set_wrap(&config, 4095);
  pwm_init(slice, &config, true);
}

LedPwmDriver::LedPwmDriver() {}
    
void LedPwmDriver::operator()(const std::variant<std::monostate, Program::ProgramEntry, Program>& new_state) {
  LockGuard lock(pwm_mutex_); // Ensure exclusive access to PWM hardware when updating settings from the state change listener, which may be called from a different thread or interrupt context.   
  if (buffer1_active_) buffer1_.update(new_state);
  else buffer2_.update(new_state);
  new_program_pending_ = true; // Set the flag to indicate that we have a new program pending to be applied to the PWM hardware.
}

void LedPwmDriver::waitForFlashAllowed() {
  waiting_for_flash_allowed_ = true;
  while (waiting_for_flash_allowed_);
}

void LedPwmDriver::notifyFlashComplete()    {
  flash_write_complete_ = true;
}

bool LedPwmDriver::blockForInterrupt() {
  if (waiting_for_flash_allowed_) {
    // If we're waiting for flash to be allowed, we skip blocking for interrupts and just wait until we're notified that the flash write is complete.
    flash_write_complete_ = false; // Reset the flash write complete flag before we start waiting for the flash write to complete.
    waitForFlashComplete(waiting_for_flash_allowed_, flash_write_complete_);
  }
  if (new_program_pending_) {
    LockGuard lock(pwm_mutex_); // Ensure exclusive access to PWM settings objects.
    new_program_pending_ = false; // Reset the flag now that we're handling the new program pending state.
    buffer1_active_ = !buffer1_active_; // Switch active buffer to the one that was just updated with new settings.
    // If we have a new program pending, we want to break out of the wait loop immediately to update the PWM output based on the new settings.
    return true;
  }
  return false; // Indicate that there was no new program pending and we can proceed with normal operation.
}


void LedPwmDriver::driveLights() {
  printf("Starting LED PWM Driver main loop...\n");
  while (true) {
    const auto& active_setting = buffer1_active_ ? buffer1_.setting_ : buffer2_.setting_;
    if (std::holds_alternative<Program::ProgramEntry>(active_setting))
      staticColorDriveLights(std::get<Program::ProgramEntry>(active_setting));
    else if (std::holds_alternative<Program>(active_setting))
      programDriveLights(std::get<Program>(active_setting));
    else
      monostateDriveLights();
  }
}

void LedPwmDriver::monostateDriveLights() {
  // Set all PWM levels to 0 to turn off all lights.
  pwm_set_gpio_level(channels[0].pin_r, 0);
  pwm_set_gpio_level(channels[0].pin_g, 0);
  pwm_set_gpio_level(channels[0].pin_b, 0);
  pwm_set_gpio_level(channels[0].pin_w, 0);
  pwm_set_gpio_level(channels[1].pin_r, 0);
  pwm_set_gpio_level(channels[1].pin_g, 0);
  pwm_set_gpio_level(channels[1].pin_b, 0);
  pwm_set_gpio_level(channels[1].pin_w, 0);
  // In monostate (off) mode, we just want to keep the lights off and wait for an interrupt to update the state,
  // so we can just loop here with minimal CPU usage.
  while (!blockForInterrupt()) tight_loop_contents();
}

void LedPwmDriver::staticColorDriveLights(const Program::ProgramEntry& static_settings) {
  // Set PWM levels based on the static color settings in the provided ProgramEntry.
  pwm_set_gpio_level(channels[0].pin_r, static_settings.left_channel.red.get_magnitude());
  pwm_set_gpio_level(channels[0].pin_g, static_settings.left_channel.green.get_magnitude());
  pwm_set_gpio_level(channels[0].pin_b, static_settings.left_channel.blue.get_magnitude());
  pwm_set_gpio_level(channels[0].pin_w, static_settings.left_channel.white.get_magnitude());
  pwm_set_gpio_level(channels[1].pin_r, static_settings.right_channel.red.get_magnitude());
  pwm_set_gpio_level(channels[1].pin_g, static_settings.right_channel.green.get_magnitude());
  pwm_set_gpio_level(channels[1].pin_b, static_settings.right_channel.blue.get_magnitude());
  pwm_set_gpio_level(channels[1].pin_w, static_settings.right_channel.white.get_magnitude());
  // In static color mode, we just want to keep the lights at the set color and wait for an interrupt to update the state,
  // so we can just loop here with minimal CPU usage.
  while (!blockForInterrupt()) tight_loop_contents();
}

void LedPwmDriver::programDriveLights(const Program& program) {
  // This function would implement the logic to run a program of PWM settings based on the provided Program object.
  // For simplicity, this is just a placeholder and would need to be implemented to handle timing and transitions between program entries.
}


void LedPwmDriver::globalLambdaWrapper() {
  globalLambda();
}

IPwmDriver* ledPwmDriverInit() {
  printf("Initializing LED PWM Driver...\n");
  LedPwmDriver* instance = new LedPwmDriver();
  // Initialize the PWM hardware and start the PWM output based on the initial settings.
  // We do this here in the init function to ensure that we set up the PWM hardware before we start receiving updates via the state change listener.
  // This way we can ensure that we're ready to handle updates to the PWM settings as soon as we return from this function.
  globalLambda = [instance]() {
    instance->driveLights();
  };
  multicore_launch_core1(LedPwmDriver::globalLambdaWrapper);
  return instance;
}