#include "pwm_driver.h"
#include "pwm_config_buffer_manager.h"

#include "hardware/exception.h"
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

void sigbus(void){
  printf("SIGBUS exception caught on core 1...\n");
  while(1);
}

std::function<void()> globalLambda;

// *** Constants ***

constexpr uint PWM_FREQUENCY = 2000; // 2kHz to allow a reasonable range for the duty cycle, but not clobber the switching capability of our XY-MOS modules
constexpr int64_t CYCLE_PERIOD = 10000;  // micro-seconds per cycle (1 100th of a second)
constexpr int CYCLES_PER_DURATION_UNIT = 5;  // Duration granularity is 20ths of a second, so 5 cycles per duration unit
constexpr int PIN_COUNT = 8;  // Count of colors per channel (i.e. R,G,B,W) * channels (i.e. L,R)
// This sequence follows the order of channel/colors returned by Program::ProgramEntry::color_array
// Init to pins that map to respective channels (slices are set at runtime)
constexpr std::array<unsigned int, PIN_COUNT> GPIO_PINS = {
  16, // 0A Left Red     PIN 21
  18, // 1A Left Green   PIN 24
  20, // 2A Left Blue    PIN 26
  22, // 3A Left White   PIN 29
  14, // 7A Right Red    PIN 19
  12, // 6A Right Green  PIN 16
  10, // 5A Right Blue   PIN 14
   8  // 4A Right White  PIN 11
};


// *** Type Definitions ***

// The setting contains either a monostate (all LED channels off), a program entry (for static color/channel settings), or a full Program of entries.
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

struct PinMagnitude {
  void setMagnitude(unsigned int pin, unsigned short magnitude) {
    pin_ = pin;
    magnitude_ = magnitude;
  }
  unsigned int pin_;
  unsigned short magnitude_;
};


// *** Functions ***

// Sets pin magnitudes for levels set dynamically over the course of a Program entry's duration
void setPwmLevels(const std::array<PinMagnitude, PIN_COUNT> pin_magnitudes, size_t c) {
  for (auto i = 0; i < c; ++i) pwm_set_gpio_level(pin_magnitudes[i].pin_, pin_magnitudes[i].magnitude_ * pin_magnitudes[i].magnitude_);
}

// Sets levels for all the pins in an entry (i.e. static setting or a program setting with only the program entry set).
void setProgramEntryPwmMagnitudes(const Program::ProgramEntry& entry, std::array<PinMagnitude, PIN_COUNT>& magnitudes) {
  for (int i = 0; i < PIN_COUNT; ++i) magnitudes[i].setMagnitude(GPIO_PINS[i], entry.color_array[i]->get_magnitude());
}

// Returns the magnitudes of the color/channels in a program entry with the fade bit set.
// This computes the magnitude based the transition from the previous entry's channel/color setting
// to the current entry's with respect to how many cycles have elapsed over the course of the current entry's duration.
int getFadeMagnitudes(
    std::array<PinMagnitude, PIN_COUNT>& next_pin_magnitudes,
    const Program::ProgramEntry* current_entry, const Program::ProgramEntry* previous_entry,
    int current_cycle, int cycles) {
  int current_index = 0;
  for (int i = 0; i < PIN_COUNT; ++i) {
    if (current_entry->color_array[i]->is_fade()) {
      unsigned int ce_mag = static_cast<unsigned int>(current_entry->color_array[i]->get_magnitude());
      unsigned int pe_mag = static_cast<unsigned int>(previous_entry->color_array[i]->get_magnitude());
      next_pin_magnitudes[current_index++].setMagnitude(
          GPIO_PINS[i],
          (pe_mag <= ce_mag) ? (pe_mag + (ce_mag - pe_mag) * current_cycle / cycles) : (pe_mag - (pe_mag - ce_mag) * current_cycle / cycles));
    }
  }
  return current_index;
}

// Basically just tacks on the non-fade color/channel settings for the entry to the given magnitude settings.
int getAbsoluteMagnitudes(std::array<PinMagnitude, PIN_COUNT>& next_pin_magnitudes, const Program::ProgramEntry* current_entry, int current_index) {
  for (int i = 0; i < PIN_COUNT; ++i)
    if (!current_entry->color_array[i]->is_fade())
      next_pin_magnitudes[current_index++].setMagnitude(GPIO_PINS[i], current_entry->color_array[i]->get_magnitude());
  return current_index;
}

class LedPwmDriver : public IPwmDriver {
 private:
  void init_pwm(unsigned int pin);
  Mutex pwm_mutex_; // Mutex to protect concurrent access to PWM hardware from multiple cores or interrupts.
  // Flag to indicate that a new program has been loaded and is pending to be applied to the PWM hardware on
  // the next cycle, used in conjunction with double buffering to ensure smooth transitions between programs without glitches.
  // This flag is set to true whenever a new program is loaded via the state change listener and is reset to false
  // after the new program has been applied to the PWM hardware in the main loop or an appropriate update function.  
  volatile bool new_program_pending_ = false;
  // Flag to indicate that we need to go into a sram only mode to allow flash writes to complete without interference
  // from reading instructions from flash.
  volatile bool waiting_for_flash_to_be_allowed_ = false;
  // Flag to indicate that a flash write has completed and the pwm drive can proceed as normal.
  // This flag is used in conjunction with waiting_for_flash_allowed_ to manage transitions between normal operation and sram-only mode for flash writes.
  volatile bool flash_write_complete_ = false;
  // Double buffers for PWM settings to allow glitch-free updates to the PWM hardware when changing programs or static settings.
  PwmSetting buffer1_, buffer2_;
  // Active buffer for PWM output, used for double buffering of program data
  // to ensure consistent updates to the PWM hardware without glitches.   
  PwmSetting* active_buffer_ = &buffer1_;

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


void __not_in_flash_func(waitForFlashComplete)(volatile bool& waiting_for_flash_to_be_allowed, volatile bool& flash_write_complete) {
  // Now we are in code running from SRAM, so we can safely let the flash write to commense.
  waiting_for_flash_to_be_allowed = false;
  // We now wait until the flash write is complete before allowing the PWM driver
  // to resume normal operation involving reading instructions from flash.
  while (!flash_write_complete);
}
   
}  // namespace

void LedPwmDriver::init_pwm(unsigned int pin) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  unsigned int slice = pwm_gpio_to_slice_num(pin);
  
  // The PWM frequency is is ulltimately constrained by the switching speed of our XY-MOS MOSFETs,
  // which are rated for up to 100kHz switching speeds, but are ultimately determined by the capacitance of the gate and the voltage applied.
  // The pi pico doesn't apply a very high voltage, so we have to assume in our case it is much lower than 100kHz.  So we will set the frequency as
  // low as we we can while still ensuring that we have enough resolution for our PWM levels to allow for smooth dimming and color transitions,
  // especially since we're using a non-linear brightness curve that squares the input value to get the output brightness.
  // With a base frequency of 2kHz and a wrap value of 8191, we can achieve a good balance of frequency and resolution for our application.
  // 8192 is the number of steps we have with a wrap value of 8191 (0-8191 inclusive gives us 8192 steps)
  // and a non-linear brightness curve that squares the input value, effectively giving us 8192 steps of brightness control.
  uint clock_div = clock_get_hz(clk_sys) / (PWM_FREQUENCY * 8192);
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, clock_div);
  pwm_config_set_wrap(&config, 8191);
  pwm_init(slice, &config, true);
}

LedPwmDriver::LedPwmDriver() {
  for (int pin : GPIO_PINS) init_pwm(pin);
}
    
void LedPwmDriver::operator()(const std::variant<std::monostate, Program::ProgramEntry, Program>& new_state) {
  LockGuard lock(pwm_mutex_); // Ensure exclusive access to PWM hardware when updating settings from the state change listener, which may be called from a different thread or interrupt context.   
//  printf("PWM Driver received new state update.  Staging settings update...\n");
  if (active_buffer_ == &buffer1_) buffer2_.update(new_state);
  else buffer1_.update(new_state);
  new_program_pending_ = true; // Set the flag to indicate that we have a new program pending to be applied to the PWM hardware.
}

// This is called from the thread (core 0) that is waiting to be able to update flash
// memory without concern for the PWM driver core's (core 1) reading instructions from flash.
// It sets `waiting_for_flash_to_be_allowed_` to true to indicate that it is waiting
// for PWM driver core to be running in an SRAM only routine before it can continue.
// The PWM driver core sets this to false once it is running instructions from SRAM.
void LedPwmDriver::waitForFlashAllowed() {
  printf("PWM driver notified to wait for flash to be allowed.  Entering wait state...\n");
  waiting_for_flash_to_be_allowed_ = true;
  while (waiting_for_flash_to_be_allowed_);
}

void LedPwmDriver::notifyFlashComplete()    {
  flash_write_complete_ = true;
}

// We call this at the end of each cycle iteration.  It does two things:
//   1. Blocks (runs code in SRAM only) to allow flashing setting changes.
//   2. Determines if a new setting has been set so as to exit running the previous and begin running the new.
bool LedPwmDriver::blockForInterrupt() {
  if (waiting_for_flash_to_be_allowed_) {
    printf("Waiting to flash.  Entering SRAM loop...\n");
    // If we're waiting for flash to be allowed, we skip blocking for interrupts and just wait until we're notified that the flash write is complete.
    flash_write_complete_ = false; // Reset the flash write complete flag before we start waiting for the flash write to complete.
    waitForFlashComplete(waiting_for_flash_to_be_allowed_, flash_write_complete_);
    printf("Flash write complete.  Resuming normal operation...\n");
  }
  if (new_program_pending_) {
    printf("New program pending.  Updating PWM settings...\n");
    LockGuard lock(pwm_mutex_); // Ensure exclusive access to PWM settings objects.
    new_program_pending_ = false; // Reset the flag now that we're handling the new program pending state.
    // Switch active buffer to the one that was just updated with new settings.
    active_buffer_ = (active_buffer_ == &buffer1_) ? &buffer2_ : &buffer1_;
    // If we have a new program pending, we want to break out of the wait loop immediately to update the PWM output based on the new settings.
    return true;
  }
  return false; // Indicate that there was no new program pending and we can proceed with normal operation.
}


void LedPwmDriver::driveLights() {
  printf("Starting LED PWM Driver main loop...\n");
  while (true) {
    if (std::holds_alternative<Program::ProgramEntry>(active_buffer_->setting_))
      staticColorDriveLights(std::get<Program::ProgramEntry>(active_buffer_->setting_));
    else if (std::holds_alternative<Program>(active_buffer_->setting_))
      programDriveLights(std::get<Program>(active_buffer_->setting_));
    else
      monostateDriveLights();
  }
}

void LedPwmDriver::monostateDriveLights() {
  printf("Monostate mode active.  Turning off all lights and waiting for new settings...\n");
  // Set all PWM levels to 0 to turn off all lights.
  for (unsigned int pin : GPIO_PINS) pwm_set_gpio_level(pin, 0);
  // In monostate (off) mode, we just want to keep the lights off and wait for an interrupt to update the state,
  // so we can just loop here with minimal CPU usage.
  while (!blockForInterrupt()) tight_loop_contents();
}

void LedPwmDriver::staticColorDriveLights(const Program::ProgramEntry& static_settings) {
  printf("Static color mode active.  Setting lights to static color settings and waiting for new settings...\n");
  // Set PWM levels based on the static color settings in the provided ProgramEntry.
  std::array<PinMagnitude, PIN_COUNT> pin_magnitudes;
  setProgramEntryPwmMagnitudes(static_settings, pin_magnitudes);
  setPwmLevels(pin_magnitudes, PIN_COUNT);
  // In static color mode, we just want to keep the lights at the set color and wait for an interrupt to update the state,
  // so we can just loop here with minimal CPU usage.
  while (!blockForInterrupt()) tight_loop_contents();
}

void LedPwmDriver::programDriveLights(const Program& program) {
  printf("Program mode active.  Running program...\n");
  // Check to see if the program has a non-zero duration for any of its entries, if not, we can just run the static color settings
  // for the starting entry without worrying about timing or transitions since there are no durations to handle.
  bool has_non_zero_duration = false;
  for (const auto& entry : program.entries_) {
    if (entry.duration != 0) {
       has_non_zero_duration = true;
       break;
    }
  }
  if (!has_non_zero_duration) {
    printf("Program has no entries with non-zero duration.  Running static color settings for starting entry...\n");
    staticColorDriveLights(program.starting_entry_);
    return;
  }
  // Seed the current entry to `starting_entry_` so it will be bumped tp the previous_entry in the first iteration.
  const Program::ProgramEntry* current_entry = &program.starting_entry_;
  const Program::ProgramEntry* previous_entry = nullptr;

  size_t c_pin_magnitudes = PIN_COUNT;
  std::array<PinMagnitude, PIN_COUNT> next_pin_magnitudes;
  setProgramEntryPwmMagnitudes(program.starting_entry_, next_pin_magnitudes);
  // Seed this to the last index since we do a pre-increment modulus at the start of the program entry loop
  // to determine the current index, which will put us at zero.
  size_t current_entry_index = 19;  // Stage this so that the previous program entry would have been the last entry (zero-based).
  absolute_time_t cycle_start_time = get_absolute_time();
  while (true) {  // Program entry loop
    previous_entry = current_entry;
    // Cycle through program entries until we hit one with a non-zero duration.
    do { ++current_entry_index %= 20; } while (!program.entries_[current_entry_index].duration);
    current_entry = &program.entries_[current_entry_index];
    int cycles = static_cast<int>(current_entry->duration) * 5;
    for (int current_cycle = 0;  current_cycle < cycles; ++current_cycle) {  // Cycle loop
      setPwmLevels(next_pin_magnitudes, c_pin_magnitudes);
      c_pin_magnitudes = getFadeMagnitudes(next_pin_magnitudes, current_entry, previous_entry, current_cycle + 1, cycles);
      if (!current_cycle) c_pin_magnitudes = getAbsoluteMagnitudes(next_pin_magnitudes, current_entry, c_pin_magnitudes);
      while (absolute_time_diff_us(cycle_start_time, get_absolute_time()) < CYCLE_PERIOD) {
        if (blockForInterrupt())
          return; // Break out of the program loop immediately to update the PWM settings based on the new program pending state.
        tight_loop_contents();
      }
      cycle_start_time = get_absolute_time();
    }
  }
}

void LedPwmDriver::globalLambdaWrapper() {
//  exception_set_exclusive_handler(HARDFAULT_EXCEPTION,sigbus);
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