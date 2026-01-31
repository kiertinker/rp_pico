#include "pwm_config_buffer_manager.h"

#include "pico/flash.h"
#include "hardware/flash.h"

#include <array>

extern char __flash_binary_end;

namespace {

unsigned char* configFlashAddress() {
    // Calculate the start address of the PWM config in flash
    const uint32_t flash_start = (uint32_t)&__flash_binary_end;
    const uint32_t pwm_config_address = (flash_start + FLASH_SECTOR_SIZE) & ~(FLASH_SECTOR_SIZE - 1);
    return reinterpret_cast<unsigned char*>(pwm_config_address);
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

void call_flash_range_program(void* data) {
  flash_range_program(reinterpret_cast<uint32_t>(configFlashAddress()), reinterpret_cast<const uint8_t*>(data), PAGE_SIZE_ALLOC_CONFIG);
}

void call_flash_range_erase([[maybe_unused]] void* __unused) {
  flash_range_erase(reinterpret_cast<uint32_t>(configFlashAddress()), FLASH_SECTOR_SIZE);
}

}  // anonymous namespace

PwmConfigData::PwmConfigData() {
  // Read the PWM config data from flash into the data_ array
  std::copy(configFlashAddress(), configFlashAddress() + PWM_CONFIG_SIZE, data_);
  if (make_hash(data_ + HASH_SIZE, PWM_CONFIG_SIZE - HASH_SIZE) != hash_bits_) {
    // Hash mismatch - initialize to defaults (zero data), then clear flash sector
    std::fill(data_, data_ + PWM_CONFIG_SIZE, 0);
    hash_bits_ = make_hash(data_ + HASH_SIZE, PWM_CONFIG_SIZE - HASH_SIZE);
    // Update flash with default data (and default hash).
    // This is a best effort - if it fails, we just move on.
    // Erase the flash sector
    if (!flash_safe_execute(call_flash_range_erase, nullptr, 1000)) {
      // Program the default data into flash
      flash_safe_execute(call_flash_range_program, reinterpret_cast<void*>(data_), 1000);
    }
  }
}
