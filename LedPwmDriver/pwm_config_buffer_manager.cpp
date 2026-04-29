#include "pwm_config_buffer_manager.h"

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include <array>
#include <cstdio>

extern char __flash_binary_end;

namespace {

class DisableInterruptsGuard {
  unsigned int status;
 public:
  DisableInterruptsGuard() : status(save_and_disable_interrupts()) {}
  ~DisableInterruptsGuard() { restore_interrupts_from_disabled(status); }
};

// Nearest graduation of Flash page size (256 bytes) not less than PWM_CONFIG_SIZE for program operations.
constexpr size_t PAGE_SIZE_ALLOC_CONFIG = 1024;
// Wait time after a config change before allowing another change to prevent excessive flash writes if multiple updates come in within a short time frame.
constexpr uint64_t FLASH_WRITE_DELAY_US = 1000000 * 10;  // 10 seconds

constexpr unsigned char* configFlashWriteAddress() {
  printf("Calculating flash write address for PWM config data...\n");
  uint32_t flash_target_addr = ((reinterpret_cast<uint32_t>(&__flash_binary_end) + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)) - XIP_BASE;
  return reinterpret_cast<unsigned char*>(flash_target_addr);
}

constexpr unsigned char* configFlashReadAddress() {
  return configFlashWriteAddress() + XIP_BASE; // Address in the XIP region corresponding to the config flash address
}

// Simple FNV-1a hash function for data integrity check.
unsigned long long make_hash(const unsigned char* bytes, size_t length) {
    unsigned long long hash = 14695981039346656037ULL; // FNV offset basis
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<unsigned long long>(bytes[i]);
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

class PwmConfigData : public PwmConfigDataInterface {
 private:
   PwmConfigData();
  // Config size is PWM_CONFIG_SIZE bytes but we need this to be an even flash page size increment.
  // Also using unsigned int array to ensure proper alignment for 64bit hash value (int instructions require 32bit aligned memory).
  unsigned int aligned_[PAGE_SIZE_ALLOC_CONFIG / sizeof(unsigned int)];
  unsigned long long& hash_bits_ = *reinterpret_cast<unsigned long long*>(aligned_);
  unsigned char* data_ = reinterpret_cast<unsigned char*>(aligned_);
  // Model static settings as a ProgramEntry for ease of use, even though the duration field is repurposed for mode selection.
  Program::ProgramEntry static_settings_ = {data_ + HASH_SIZE, 0};
  std::array<Program, 4> programs_ = {{  // First '{' is for the std::array, second is for the initializer list
      {data_ + PROGRAM_START_OFFSET, 0}, {data_ + PROGRAM_START_OFFSET, 1},
      {data_ + PROGRAM_START_OFFSET, 2}, {data_ + PROGRAM_START_OFFSET, 3}}};
  StateChangeListener* state_change_listener_ = nullptr;

  // Track the last time a change was made to the config data to prevent unnecessary flash writes if multiple updates come in within a short time frame.
  absolute_time_t last_change_time_ = 0;

  void setAlarm();
  void handleAlarm();
  void updateFlash();
  void operator()(CustomServiceCharacteristicIndex index, const unsigned char* value, size_t value_size) override;


 public:
  enum class GetInstanceCmd { CREATE_INSTANCE, GET_EXISTING_INSTANCE, CLEAR_INSTANCE };

  static PwmConfigData* getInstance(GetInstanceCmd cmd);
  virtual ~PwmConfigData() { getInstance(GetInstanceCmd::CLEAR_INSTANCE); }

  void initPwnConfigData(StateChangeListener* listener);

  std::array<std::pair<unsigned char*, size_t>, 6> getCharacteristicValuePtrs() override;

  // Delete the copy & assignment constructors
  PwmConfigData(const PwmConfigData&) = delete;
  PwmConfigData& operator=(const PwmConfigData&) = delete;
};

PwmConfigData::PwmConfigData() {
  printf("Getting flash write address.  flash binary end: %p\n", (void*)&__flash_binary_end);
  printf("Calculated flash write target address: 0x%08x\n", reinterpret_cast<uint32_t>(configFlashWriteAddress()));
  printf("Getting flash read address.  Target address: %p\n", (void*)(configFlashWriteAddress() + XIP_BASE));
}

void PwmConfigData::setAlarm()  {
  printf("Setting alarm to write PWM config to flash after delay...\n");
  absolute_time_t now = get_absolute_time();
  add_alarm_in_us(FLASH_WRITE_DELAY_US - absolute_time_diff_us(last_change_time_, now), [](alarm_id_t id, void* user_data) -> int64_t {
    printf("Alarm callback triggered, checking if we can write PWM config to flash now...\n");
    reinterpret_cast<PwmConfigData*>(user_data)->handleAlarm();
    return 0;
  }, this, false);
}

void PwmConfigData::handleAlarm() {
  // We don't need to worry about concurrency issues with the BLE event handler since it runs at a higher priority than this alarm callback, but we do need to ensure that we don't have concurrency issues with the PWM driver interrupt handler, which can be triggered at any time and may also want to write to flash if it's waiting for a flash write to complete.  To prevent concurrency issues with the PWM driver interrupt handler, we wait until we're allowed to write to flash before actually performing the flash write, and we disable interrupts while we're writing to flash to prevent the interrupt handler from preempting us in the middle of a flash write.  We also check if we've had another config update come in since we set the alarm, and if so, we just set another alarm to push the flash write out further instead of writing to flash immediately.
  DisableInterruptsGuard dig;
  absolute_time_t now = get_absolute_time();
  printf("Alarm triggered for flash write, checking if config data has been updated since alarm was set...\n");
  if (absolute_time_diff_us(last_change_time_, now) >= FLASH_WRITE_DELAY_US) {
    printf("Updating flash with new PWM config data...\n");
    last_change_time_ = 0;
    updateFlash();
  } else {
    // If the config was updated again during the wait time, set another alarm to check again after the delay.
    printf("Config data was updated again since alarm was set, setting another alarm to delay flash write...\n");
    setAlarm();
  }
}

void PwmConfigData::updateFlash() {
  printf("Updating flash with new PWM config data.  Flash write address: %p,  __flash_binary_end: %p.\n", configFlashWriteAddress(), __flash_binary_end);
  state_change_listener_->waitForFlashAllowed(); // Wait until we're allowed to write to flash to prevent concurrency issues with the PWM driver interrupt handler.
  flash_range_erase(reinterpret_cast<uint32_t>(configFlashWriteAddress()), FLASH_SECTOR_SIZE);
  flash_range_program(reinterpret_cast<uint32_t>(configFlashWriteAddress()), data_, PAGE_SIZE_ALLOC_CONFIG);
  printf("Flash write complete.\n");
  state_change_listener_->notifyFlashComplete(); // Notify the PWM driver that the flash write is complete so it can resume normal operation if it was waiting for this.
}

// This function is called whenever a characteristic value is updated via BLE.
// It updates the corresponding config data, notifies the pwm driver if necessary, and sets an alarm to write to flash after a delay if necessary.
// Note:  This runs in the context of the BLE event handler, which runs at a high priority irq.  This means that we cannot be preemted by the alarm
// callback that writes to flash, since that runs in a lower priority irq, so we don't need to worry about concurrency issues between this function
// and the alarm callback.  However, we could be preempt the alarm callback, so it will need to suspend interrupts if necessary.
void PwmConfigData::operator()(CustomServiceCharacteristicIndex index, const unsigned char* value, size_t value_size) {
  std::variant<std::monostate, Program::ProgramEntry, Program> new_state;
  // We only want to set the alarm if we don't already have one scheduled.
  // If last_change_time_ is non-zero, that means we have an alarm scheduled to write to flash,
  // so we can just update the config data and let the existing alarm reset itself to push the delay out further before writing to flash.
  bool set_alarm = 0 == last_change_time_;
//  printf("Received update for characteristic index %d, value size %zu.  Set alarm: %s\n", static_cast<int>(index), value_size, set_alarm ? "true" : "false");
  switch (index) {
    case CustomServiceCharacteristicIndex::MODE_SELECTION:
//      printf("Updating mode selection with new value: %d\n", value[0]);
      // Check for immediate exit conditions
      if (value_size != 1 || value[0] > static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_PROGRAM_4)) return;  // bogus data so just bail out here.
//      printf("Mode selection changed from %d to %d\n", static_settings_.duration, value[0]);
      if (value[0] == static_settings_.duration) return; // No change in mode, so no need to update or notify.

      static_settings_.duration = value[0];
      if (state_change_listener_ == nullptr) {
//        printf("No state change listener available to notify of mode selection change.\n");
        break; // No listener to notify of state change, so break to update hash.
      }

      switch (static_cast<MODE_SELECTION_VALUES>(value[0])) {
        case MODE_SELECTION_VALUES::MODE_OFF:
          new_state = std::monostate{};
          break;
        case MODE_SELECTION_VALUES::MODE_STATIC_COLOR:
          new_state.emplace<Program::ProgramEntry>(static_settings_.getBaseAddr(), 0);
          break;
        case MODE_SELECTION_VALUES::MODE_PROGRAM_1:
        case MODE_SELECTION_VALUES::MODE_PROGRAM_2:
        case MODE_SELECTION_VALUES::MODE_PROGRAM_3:
        case MODE_SELECTION_VALUES::MODE_PROGRAM_4:
          // Programs start at index PROGRAM_1_SETTINGS, so we can calculate the program index by subtracting PROGRAM_1_SETTINGS from the mode value.
          new_state.emplace<Program>(const_cast<unsigned char*>(programs_[value[0] - static_cast<unsigned char>(CustomServiceCharacteristicIndex::PROGRAM_1_SETTINGS)].getBaseAddr()), 0);
          break;
        default:
          new_state = std::monostate{};
          break;
      }
//      printf("Notifying state change listener of mode selection change...\n");
      (*state_change_listener_)(new_state);
      break;

    case CustomServiceCharacteristicIndex::STATIC_COLOR_SETTINGS:
      // Check for immediate exit conditions
      if (value_size != 8) return;  // Handle invalid value size, for now, we just ignore it and don't update the config or notify the listener.
      if (std::equal(value, value + 8, &static_settings_.left_channel.red.value)) return; // No change in static settings, so no need to update or notify.

      // Update the static settings with the new value.
      std::copy(value, value + 8, &static_settings_.getBaseAddr()[1]); // +1 to skip the duration byte which is used for mode selection.

      if (static_settings_.duration != static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_STATIC_COLOR))
        // Mode is not static color, so no need to notify of state change.
        break;
      new_state.emplace<Program::ProgramEntry>(static_settings_.getBaseAddr(), 0);
      (*state_change_listener_)(new_state);
      break;

    case CustomServiceCharacteristicIndex::PROGRAM_1_SETTINGS:
    case CustomServiceCharacteristicIndex::PROGRAM_2_SETTINGS:
    case CustomServiceCharacteristicIndex::PROGRAM_3_SETTINGS:
    case CustomServiceCharacteristicIndex::PROGRAM_4_SETTINGS: {
      // Check for immediate exit conditions
      if (value_size != PROGRAM_SIZE) return;  // Handle invalid value size, for now, we just ignore it and don't update the config or notify the listener.
      Program& config_program = programs_[static_cast<size_t>(index) - static_cast<size_t>(CustomServiceCharacteristicIndex::PROGRAM_1_SETTINGS)];
      Program gat_program(const_cast<unsigned char*>(value), 0); // Create a temporary Program object to compare the new value with the existing program data.
      if (config_program == gat_program) return; // No change in static settings, so no need to update or notify.

      // Update the program data with the new value.
      gat_program.copyTo(const_cast<unsigned char*>(config_program.getBaseAddr()));

      // If no state change listener is available or the current mode is not one of the program settings, no need to notify of state change.
      if (!state_change_listener_ || static_settings_.duration != static_cast<unsigned char>(MODE_SELECTION_VALUES(static_cast<int>(index))))
        break; 

      new_state.emplace<Program>(const_cast<unsigned char*>(config_program.getBaseAddr()), 0);
      (*state_change_listener_)(new_state);
      break;
    }
    default:
      return; // Invalid characteristic index, so ignore.
  }
  // Update the hash after handling the change and notifying the listener.
  hash_bits_ = make_hash(data_ + HASH_SIZE, PWM_CONFIG_SIZE - HASH_SIZE);
  // If we had determined that we needed to set an alarm to write to flash, we set it here after we've made the change and updated the hash.
  last_change_time_ = get_absolute_time();
  if (set_alarm) setAlarm();
}

std::array<std::pair<unsigned char*, size_t>, 6> PwmConfigData::getCharacteristicValuePtrs() {
  return {{
    { &data_[HASH_SIZE], 1 },  // Mode selection
    { &data_[HASH_SIZE + 1], 8 },  // Static settings
    { &data_[PROGRAM_START_OFFSET + 0 * PROGRAM_SIZE], PROGRAM_SIZE },  // Program 1 settings
    { &data_[PROGRAM_START_OFFSET + 1 * PROGRAM_SIZE], PROGRAM_SIZE },  // Program 2 settings
    { &data_[PROGRAM_START_OFFSET + 2 * PROGRAM_SIZE], PROGRAM_SIZE },  // Program 3 settings
    { &data_[PROGRAM_START_OFFSET + 3 * PROGRAM_SIZE], PROGRAM_SIZE }   // Program 4 settings
  }};
}

PwmConfigData* PwmConfigData::getInstance(GetInstanceCmd cmd) {
  static PwmConfigData* instance = nullptr;
  if (cmd == GetInstanceCmd::CREATE_INSTANCE && instance == nullptr) {
    instance = new PwmConfigData();
  } else if (cmd == GetInstanceCmd::CLEAR_INSTANCE && instance != nullptr) {
    delete instance;
    instance = nullptr;
  }
  return instance;
}

void PwmConfigData::initPwnConfigData(StateChangeListener* listener) {
  if (listener == nullptr) return; // We require a listener to be provided for this function to do anything, so if it's null, we just return early.
  state_change_listener_ = listener;
  printf("Initializing PWM Config Data...\n");
  // Read the PWM config data from flash into the data_ array
  std::copy(configFlashReadAddress(), configFlashReadAddress() + PWM_CONFIG_SIZE, data_);
  if (make_hash(data_ + HASH_SIZE, PWM_CONFIG_SIZE - HASH_SIZE) != hash_bits_) {
    // Hash mismatch - initialize to defaults (zero data) and hash the bits.
    printf("PWM config hash mismatch - initializing to defaults.\n");
    std::fill(data_, data_ + PWM_CONFIG_SIZE, 0);
    hash_bits_ = make_hash(data_ + HASH_SIZE, PWM_CONFIG_SIZE - HASH_SIZE);
    // Erase the flash sector, then update flash with default data (and default hash).
    // This is a best effort - if it fails, we just move on.
    DisableInterruptsGuard dig;
    updateFlash();
  } else {
    printf("PWN config hash matches.  Config from flash is good!\n");
  }
  std::variant<std::monostate, Program::ProgramEntry, Program> new_state;

  printf("Initializing PWM driver with mode selection %d\n", static_cast<int>(static_settings_.duration));
  switch (static_cast<MODE_SELECTION_VALUES>(static_settings_.duration)) {
    case MODE_SELECTION_VALUES::MODE_STATIC_COLOR:
      new_state.emplace<Program::ProgramEntry>(static_settings_.getBaseAddr(), 0);
      break;

    case MODE_SELECTION_VALUES::MODE_PROGRAM_1:
    case MODE_SELECTION_VALUES::MODE_PROGRAM_2:
    case MODE_SELECTION_VALUES::MODE_PROGRAM_3:
    case MODE_SELECTION_VALUES::MODE_PROGRAM_4:
      // Programs start at index PROGRAM_1_SETTINGS, so we can calculate the program index by subtracting PROGRAM_1_SETTINGS from the mode value.
      new_state.emplace<Program>(const_cast<unsigned char*>(programs_[static_settings_.duration - static_cast<unsigned char>(CustomServiceCharacteristicIndex::PROGRAM_1_SETTINGS)].getBaseAddr()), 0);
      break;

    case MODE_SELECTION_VALUES::MODE_OFF:
    default:
      new_state.emplace<std::monostate>();
  }
  (*state_change_listener_)(new_state);
}

}  // anonymous namespace

PwmConfigDataInterface* pwmConfigDataInterfaceInit(StateChangeListener* listener) {
  printf("Initializing PWM Config Data Interface...\n");
  // We can ignore the listener for now since we're not using it, but we could extend PwmConfigData to call it on updates if desired.
  (void)listener;
  PwmConfigData* instance = PwmConfigData::getInstance(PwmConfigData::GetInstanceCmd::CREATE_INSTANCE);
  if (instance == nullptr) return nullptr; // Failed to create instance, likely because one already exists.
  instance->initPwnConfigData(listener);
  printf("PWM Config Data Interface initialized successfully.\n");
  return instance;
}
