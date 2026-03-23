#include "gatt_service.h"

#include "ble/att_db.h"
#include "ble/att_server.h"
#include "bluetooth_gatt.h"
#include "btstack.h"
#include "btstack_defines.h"
#include "btstack_util.h"
#include "btstack_debug.h"
#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"

#include "gatt_led_pwm_control_server.h"

#include <stdio.h>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

att_service_handler_t service_handler ;

// Flag for general discoverability
constexpr uint8_t APP_AD_FLAGS = 0x06;

// GAP data packet (must not exceed 32 bytes)
constexpr std::array<uint8_t, 23> adv_data = {
    // Flags general discoverable
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, APP_AD_FLAGS,
    // Name
    0x0F, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'P', 'h', 'a', 'n', 't', 'a', 's', 'm', 'a', 'g', 'o', 'r', 'i', 'a',
    // Custom Service UUID
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x10, 0xFF,
};

constexpr size_t NUM_CHARACTERISTICS = 6;

// HCI packet handler
void packet_handler(uint8_t packet_type, [[maybe_unused]] uint16_t channel, uint8_t *packet, [[maybe_unused]] uint16_t size) {
  bd_addr_t local_addr;
  if (packet_type != HCI_EVENT_PACKET) return;
  // Retrieve event type from HCI packet
  uint8_t event_type = hci_event_packet_get_type(packet);
  // Switch on event type . . .
  switch(event_type) {
    // Setup GAP advertisement
    case BTSTACK_EVENT_STATE: {
      if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
      gap_local_bd_addr(local_addr);
      printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));
      // setup advertisements
      uint16_t adv_int_min = 800;
      uint16_t adv_int_max = 800;
      uint8_t adv_type = 0;
      bd_addr_t null_addr;
      memset(null_addr, 0, 6);
      gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
      assert(adv_data.size() <= 31); // ble limitation
      gap_advertisements_set_data(adv_data.size(), (uint8_t*) adv_data.data());
      gap_advertisements_enable(1);
      break;
    }
    // Disconnected from a client
    case HCI_EVENT_DISCONNECTION_COMPLETE:
      break;
    // Ready to send ATT
    case ATT_EVENT_CAN_SEND_NOW:
      break;
    default:
      break;
  }
}

btstack_packet_callback_registration_t hci_event_callback_registration;

class CustomServiceCharacteristic {
 public:
  struct CharacteristicValues {
    CharacteristicValues() = default;
    CharacteristicValues(const unsigned char* value_ptr, size_t value_size, uint16_t client_configuration, std::string_view user_description)
        : value(value_ptr, value_ptr + value_size), client_configuration(client_configuration), user_description(user_description) {}
    CharacteristicValues(CharacteristicValues&&) = default;
    CharacteristicValues& operator=(CharacteristicValues&&) = default;
    CharacteristicValues(const CharacteristicValues&) = delete;
    CharacteristicValues& operator=(const CharacteristicValues&) = delete;
    std::vector<unsigned char> value;
    uint16_t client_configuration;
    std::string_view user_description;
  };

  struct CharacteristicHandles {
    uint16_t value_handle;
    uint16_t client_configuration_handle;
    uint16_t user_description_handle;
  };

 private:
  CharacteristicValues characteristic_values_;
  CharacteristicHandles characteristic_handles_;

 public:
  void setHandles(const CharacteristicHandles& handles) {	characteristic_handles_ = handles; }
  void setValues(CharacteristicValues& values) { characteristic_values_ = std::move(values); }
  std::optional<uint16_t> customServiceCharacteristicRead(
      /*hci_con_handle_t*/ uint16_t con_handle, uint16_t attribute_handle, uint16_t offset, unsigned char* buffer, uint16_t buffer_size);
  std::variant<std::monostate, unsigned int, std::reference_wrapper<std::vector<uint8_t>&>> customServiceCharacteristicWrite(
      /*hci_con_handle_t*/ uint16_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
};
// Struct for managing this service

class CustomService : public CustomServiceInterface {
 public:
  // Connection handle for service
  static hci_con_handle_t con_handle_;

  // Callback function
  btstack_context_callback_registration_t callback;

  enum class GetInstanceCmd { CREATE_INSTANCE, GET_EXISTING_INSTANCE, CLEAR_INSTANCE };
  static CustomService* getInstance(GetInstanceCmd cmd);
  static int customServiceWriteCallback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
  static uint16_t customServiceReadCallback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size);

  ~CustomService() { getInstance(GetInstanceCmd::CLEAR_INSTANCE); }
  
  CustomServiceErrorCode init(
      std::array<std::pair<unsigned char*, size_t>, 6>& characteristic_value_ptrs, CharacteristicUpdateListener* update_listener);

 private:
  CustomService() = default;
  std::array<CustomServiceCharacteristic, 6> characteristics_;
  CharacteristicUpdateListener* update_listener_ = nullptr;
};

int CustomService::customServiceWriteCallback(
    hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
  CustomService* instance = CustomService::getInstance(CustomService::GetInstanceCmd::GET_EXISTING_INSTANCE);
  for (auto& characteristic : instance->characteristics_) {
    std::variant<std::monostate, unsigned int, std::reference_wrapper<std::vector<uint8_t>&>> result =
        characteristic.customServiceCharacteristicWrite(con_handle, attribute_handle, transaction_mode, offset, buffer, buffer_size);
    switch (result.index()) {
      case 0: // std::monostate, meaning the handle didn't match this characteristic, so we should keep looking.
        continue;
      case 1: // unsigned int, meaning the handle matched and there was an error, so we should stop processing and return that error.
        return std::get<unsigned int>(result);
      case 2: // std::vector<uint8_t>&, meaning the handle matched and the write was successful, so we should process the new value and then stop processing and return success.
        // If the write was successful and we have an update listener, notify it of the change.
        if (instance->update_listener_ != nullptr) {
          std::vector<uint8_t>& new_value = std::get<std::reference_wrapper<std::vector<uint8_t>&>>(result);
          // We can ignore the value and size parameters here since the listener can read the updated value directly from the characteristic's value pointer if needed.
          (*instance->update_listener_)(CustomServiceCharacteristicIndex(&characteristic - &instance->characteristics_[0]), new_value.data(), new_value.size());
        }
    }
    return 0; // Indicate success for the write operation.
  }
  return ATT_ERROR_INVALID_HANDLE;  // Couldn't find the handle, so returning error.
}

uint16_t CustomService::customServiceReadCallback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
  CustomService* instance = CustomService::getInstance(CustomService::GetInstanceCmd::GET_EXISTING_INSTANCE);
  for (auto& characteristic : instance->characteristics_)
    if (auto result = characteristic.customServiceCharacteristicRead(con_handle, attribute_handle, offset, buffer, buffer_size); result.has_value())
      return result.value();
  return 0;  // Couldn't find the handle, so returning 0 is all we can do.
}

// Write callback
std::variant<std::monostate, unsigned int, std::reference_wrapper<std::vector<uint8_t>&>> CustomServiceCharacteristic::customServiceCharacteristicWrite(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
  static std::vector<uint8_t> staging_buffer(256); // Temporary staging buffer for writes
  // Enable/disable notifications
  if (attribute_handle == characteristic_handles_.client_configuration_handle){
	  characteristic_values_.client_configuration = little_endian_read_16(buffer, 0);
    return 0U;
  }

  // Write characteristic value
  if (attribute_handle == characteristic_handles_.value_handle) {
    switch (transaction_mode) {
      case ATT_TRANSACTION_MODE_NONE:
        if (offset + buffer_size != characteristic_values_.value.size()) {
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH; // Indicate error
        }
        // This is a bit hackey, but the mode selection characteristic is the only one that specifically needs value validation.
        // So if we're writing to that characteristic, do that additional validation.
        if ((attribute_handle == ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_VALUE_HANDLE) &&
            // Example: Validate that mode selection is within expected range (0-4)
            (buffer[0] > 4))
              return 0U;  // Invalid mode, but fail silently.
        memcpy(characteristic_values_.value.data() + offset, buffer, buffer_size);
        break;
      case ATT_TRANSACTION_MODE_ACTIVE:
        // Store data in staging buffer
        if (offset + buffer_size > staging_buffer.size()) {
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_OFFSET; // Indicate error
        }
        break;
      case ATT_TRANSACTION_MODE_VALIDATE:
        if (offset + buffer_size != characteristic_values_.value.size()) {
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH; // Indicate error
        }
        memcpy(staging_buffer.data() + offset, buffer, buffer_size);
        break;
      case ATT_TRANSACTION_MODE_EXECUTE:
        memcpy(characteristic_values_.value.data(), staging_buffer.data(), characteristic_values_.value.size());
        break;

      case ATT_TRANSACTION_MODE_CANCEL:
        /* code */
        break;
    
      default:
        break;
    }
    return 0U;
  }
  return std::monostate{};  // Couldn't find the handle, so returning empty variant to indicate no action taken.
}

std::optional<uint16_t> CustomServiceCharacteristic::customServiceCharacteristicRead(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, unsigned char* buffer, uint16_t buffer_size) {
  if (attribute_handle == characteristic_handles_.value_handle)
    return att_read_callback_handle_blob(characteristic_values_.value.data(), characteristic_values_.value.size(), offset, buffer, buffer_size);
  if (attribute_handle == characteristic_handles_.client_configuration_handle)
    return att_read_callback_handle_little_endian_16(characteristic_values_.client_configuration, offset, buffer, buffer_size);
  if (attribute_handle == characteristic_handles_.user_description_handle)
    return att_read_callback_handle_blob((unsigned char*)characteristic_values_.user_description.data(), characteristic_values_.user_description.length(), offset, buffer, buffer_size);
  return std::nullopt;
}

CustomService* CustomService::getInstance(GetInstanceCmd cmd) {
  static CustomService* instance = nullptr;

  if (cmd == GetInstanceCmd::CREATE_INSTANCE && instance == nullptr) {
    instance = new CustomService();
  } else if (cmd == GetInstanceCmd::CLEAR_INSTANCE && instance != nullptr) {
    delete instance;
    instance = nullptr;
  }
  return instance;
}

CustomServiceErrorCode CustomService::init(
    std::array<std::pair<unsigned char*, size_t>, 6>& characteristic_value_ptrs, CharacteristicUpdateListener* update_listener) {
  // Initialise the BT/Wi-Fi chip
  if (int fail_code = cyw43_arch_init(); fail_code != 0) {
    printf("Bluetooth init failed with code %d\n", fail_code);
    return CustomServiceErrorCode::CYW43_ARCH_INIT_FAILED;
  }

  constexpr std::array<CustomServiceCharacteristic::CharacteristicHandles, 6> characteristic_handles_array = {{
    {ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_DC7065C1_F565_11F0_AE0B_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_DC7065C1_F565_11F0_AE0B_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_DC7065C1_F565_11F0_AE0B_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_E3B9D14B_F565_11F0_B435_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_E3B9D14B_F565_11F0_B435_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_E3B9D14B_F565_11F0_B435_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_EBF01199_F565_11F0_A5C8_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_EBF01199_F565_11F0_A5C8_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_EBF01199_F565_11F0_A5C8_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_F54B9A83_F565_11F0_8BAF_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_F54B9A83_F565_11F0_8BAF_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_F54B9A83_F565_11F0_8BAF_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_5375A4AA_F568_11F0_8B5D_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_5375A4AA_F568_11F0_8B5D_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_5375A4AA_F568_11F0_8B5D_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE}
  }};

  // Characteristic user descriptions (appear in LightBlue app)
  constexpr std::array<std::string_view, NUM_CHARACTERISTICS> characteristic_user_descriptions = {{
      "Mode Selection",
      "Static Color Settings",
      "Program 1 Settings",
      "Program 2 Settings",
      "Program 3 Settings",
      "Program 4 Settings"
  }};

  for (int i = 0; i < characteristics_.size(); ++i) {
    characteristics_[i].setHandles(characteristic_handles_array[i]);
    // create temporary CharacteristicValues to set the initial value for each characteristic based on the provided pointers and sizes, and then move that into the characteristic.
    CustomServiceCharacteristic::CharacteristicValues temp(characteristic_value_ptrs[i].first, characteristic_value_ptrs[i].second, 0, characteristic_user_descriptions[i]);
    characteristics_[i].setValues(temp);
  }


  // Service start and end handles (modeled off heartrate example)
  service_handler.start_handle = 0 ;
  service_handler.end_handle = 0xFFFF ;
  service_handler.read_callback = &customServiceReadCallback ;
  service_handler.write_callback = &customServiceWriteCallback ;
  // Register the service handler
  att_server_register_service_handler(&service_handler);
  // inform about BTstack state
  hci_event_callback_registration.callback = &packet_handler;
  hci_add_event_handler(&hci_event_callback_registration);
  
  // register for ATT event
  att_server_register_packet_handler(packet_handler);
  
  // turn on bluetooth!
  hci_power_control(HCI_POWER_ON);
  update_listener_ = update_listener;
  return CustomServiceErrorCode::ERROR_OK;
}

} // anonymous namespace

std::variant<CustomServiceInterface*, CustomServiceErrorCode> customServiceServerInit(
    std::array<std::pair<unsigned char*, size_t>, 6>& characteristic_value_ptrs, CharacteristicUpdateListener* update_listener) {
  CustomService* service_instance = CustomService::getInstance(CustomService::GetInstanceCmd::CREATE_INSTANCE);
  if (CustomServiceErrorCode error_code = service_instance->init(characteristic_value_ptrs, update_listener);
      error_code != CustomServiceErrorCode::ERROR_OK) {
    CustomService::getInstance(CustomService::GetInstanceCmd::CLEAR_INSTANCE);
    return error_code; // Failed to initialize service, so clean up instance and return nullptr to indicate failure.
  }
  return service_instance;
}