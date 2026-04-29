#include "gatt_service.h"

#include "ble/att_db.h"
#include "ble/att_server.h"
#include "bluetooth_gatt.h"
#include "btstack.h"
#include "btstack_defines.h"
#include "btstack_tlv.h"
#include "btstack_util.h"
#include "btstack_debug.h"
#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "gatt_led_pwm_control_server.h"

#include <stdio.h>
#include <string.h>
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

// // HCI packet handler
// void packet_handler(uint8_t packet_type, [[maybe_unused]] uint16_t channel, uint8_t *packet, [[maybe_unused]] uint16_t size) {
//   bd_addr_t local_addr;
//   printf("Packet handler called with packet type: %d, channel: %d\n", packet_type, channel);
//   if (packet_type != HCI_EVENT_PACKET) return;
//   // Retrieve event type from HCI packet
//   uint8_t event_type = hci_event_packet_get_type(packet);
//   printf("Packet handler called with event type: %d\n", event_type);
//   uint16_t conn_interval;
//   hci_con_handle_t con_handle;
//   static const char * const phy_names[] = { "Reserved", "1 M", "2 M", "Codec" };
//   // Switch on event type . . .
//   switch(event_type) {
//     // Setup GAP advertisement
//     case BTSTACK_EVENT_STATE: {
//       if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
//       gap_local_bd_addr(local_addr);
//       printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));
//       // setup advertisements
//       uint16_t adv_int_min = 800;
//       uint16_t adv_int_max = 800;
//       uint8_t adv_type = 0;
//       bd_addr_t null_addr;
//       memset(null_addr, 0, 6);
//       gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
//       assert(adv_data.size() <= 31); // ble limitation
//       gap_advertisements_set_data(adv_data.size(), (uint8_t*) adv_data.data());
//       gap_advertisements_enable(1);
//       break;
//     }
//   case HCI_EVENT_DISCONNECTION_COMPLETE:
//     con_handle = hci_event_disconnection_complete_get_connection_handle(packet);
//     printf("- LE Connection 0x%04x: disconnect, reason %02x\n", con_handle, hci_event_disconnection_complete_get_reason(packet));
//     break;
//   case HCI_EVENT_META_GAP:
//     switch (hci_event_gap_meta_get_subevent_code(packet)) {
//     case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
//       // print connection parameters (without using float operations)
//       con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
//       conn_interval = gap_subevent_le_connection_complete_get_conn_interval(packet);
//       printf("- LE Connection 0x%04x: connected - connection interval %u.%02u ms, latency %u\n", con_handle, conn_interval * 125 / 100, 25 * (conn_interval & 3), gap_subevent_le_connection_complete_get_conn_latency(packet));
//       // request min con interval 15 ms for iOS 11+
//       printf("- LE Connection 0x%04x: request 15 ms connection interval\n", con_handle);
//       gap_request_connection_parameter_update(con_handle, 12, 12, 4, 0x0048);
//       break;
//     default:
//       break;
//     }
//     break;
//   case HCI_EVENT_LE_META:
//     switch (hci_event_le_meta_get_subevent_code(packet)) {
//     case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
//       // print connection parameters (without using float operations)
//       con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
//       conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
//       printf("- LE Connection 0x%04x: connection update - connection interval %u.%02u ms, latency %u\n", con_handle, conn_interval * 125 / 100, 25 * (conn_interval & 3), hci_subevent_le_connection_update_complete_get_conn_latency(packet));
//       break;
//     case HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE:
//       con_handle = hci_subevent_le_data_length_change_get_connection_handle(packet);
//       printf("- LE Connection 0x%04x: data length change - max %u bytes per packet\n", con_handle, hci_subevent_le_data_length_change_get_max_tx_octets(packet));
//       break;
//     case HCI_SUBEVENT_LE_PHY_UPDATE_COMPLETE:
//       con_handle = hci_subevent_le_phy_update_complete_get_connection_handle(packet);
//       printf("- LE Connection 0x%04x: PHY update - using LE %s PHY now\n", con_handle, phy_names[hci_subevent_le_phy_update_complete_get_tx_phy(packet)]);
//       break;
//     default:
//       break;
//     }
//     break;

//   default:
//     break;
//   }
// }
// btstack_packet_callback_registration_t hci_event_callback_registration;

/* ---------------------------------------------------------------
 * Globals
 * ---------------------------------------------------------------*/
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static btstack_packet_callback_registration_t hci_event_callback_registration;

/* ---------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------*/
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

/* ---------------------------------------------------------------
 * HCI packet handler
 * ---------------------------------------------------------------*/
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
  UNUSED(channel);
  UNUSED(size);
  if (packet_type != HCI_EVENT_PACKET) return;
  uint8_t event_code = hci_event_packet_get_type(packet);
  switch (event_code) {
  /* ----------------------------------------------------------
   * Stack initialised / controller powered up.
   * Start BLE advertisements once the stack is working.
   * ---------------------------------------------------------- */
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
      bd_addr_t local_addr;
      gap_local_bd_addr(local_addr);
      printf("[BLE] Stack working – addr: %s\n", bd_addr_to_str(local_addr));
      gap_advertisements_enable(1);
    }
    break;
  /* ----------------------------------------------------------
   * BLE connection established (LE Meta – Connection Complete).
   * ---------------------------------------------------------- */
  case HCI_EVENT_LE_META:
    {
      if (hci_event_le_meta_get_subevent_code(packet) != HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
        printf("[BLE] LE Meta event received, but not connection complete – subevent code: %d\n", hci_event_le_meta_get_subevent_code(packet));
        break;
      }

      uint8_t status = hci_subevent_le_connection_complete_get_status(packet);
      if (status != ERROR_CODE_SUCCESS) {
        printf("[BLE] Connection failed, status 0x%02x\n", status);
        break;
      }
      connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
      bd_addr_t remote_addr;
      hci_subevent_le_connection_complete_get_peer_address(packet, remote_addr);
      printf("[BLE] Connected: handle=0x%04x  addr=%s\n", connection_handle, bd_addr_to_str(remote_addr));
      /* Stop advertising while a central is connected. */
      gap_advertisements_enable(0);
    }
    break;
  /* ----------------------------------------------------------
   * BLE connection torn down.
   * ---------------------------------------------------------- */
  case HCI_EVENT_DISCONNECTION_COMPLETE: {
    hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
    uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
    printf("[BLE] Disconnected: handle=0x%04x  reason=0x%02x\n", handle, reason);
    if (handle == connection_handle)
      connection_handle = HCI_CON_HANDLE_INVALID;
    /* Resume advertising so a new central can connect. */
    gap_advertisements_enable(1);
    break;
  }

  case HCI_EVENT_META_GAP:
    switch (hci_event_gap_meta_get_subevent_code(packet)) {
    case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
      // print connection parameters (without using float operations)
      {
        hci_con_handle_t con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
        uint16_t conn_interval = gap_subevent_le_connection_complete_get_conn_interval(packet);
        printf("[BLE] Connection update: handle=0x%04x  interval=%u.%02u ms  latency=%u\n", con_handle, conn_interval * 125 / 100, 25 * (conn_interval & 3), gap_subevent_le_connection_complete_get_conn_latency(packet));
      }
      break;
    default:
      break;
    }
    break;
  /* ----------------------------------------------------------
   * BLE SM pairing complete (informational).
   * With just-works / no-bonding this fires but requires no action.
   * ---------------------------------------------------------- */
  case SM_EVENT_JUST_WORKS_REQUEST:
    printf("[BLE] SM just-works request – accepting\n");
    sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
    break;
  case SM_EVENT_PAIRING_COMPLETE:
    printf("[BLE] Pairing complete, status=0x%02x\n", sm_event_pairing_complete_get_status(packet));
    break;
  default:
    break;
  }
}

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
  std::variant<std::monostate, unsigned int, uint8_t*> customServiceCharacteristicWrite(
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
  
  CustomServiceErrorCode init(CharacteristicUpdateListener* update_listener);

 private:
  CustomService() = default;
  std::array<CustomServiceCharacteristic, 6> characteristics_;
  CharacteristicUpdateListener* update_listener_ = nullptr;
};

int CustomService::customServiceWriteCallback(
    hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
  CustomService* instance = CustomService::getInstance(CustomService::GetInstanceCmd::GET_EXISTING_INSTANCE);
  for (auto& characteristic : instance->characteristics_) {
    auto result = characteristic.customServiceCharacteristicWrite(con_handle, attribute_handle, transaction_mode, offset, buffer, buffer_size);
    switch (result.index()) {
      case 0: // std::monostate, meaning the handle didn't match this characteristic, so we should keep looking.
        continue;
      case 1: // unsigned int, meaning the handle matched and there was an error, so we should stop processing and return that error.
        return std::get<unsigned int>(result);
      case 2: // uint8_t*, meaning the handle matched and the write was successful, so we should process the new value and then stop processing and return success.
        // If the write was successful and we have an update listener, notify it of the change.
        if (instance->update_listener_ != nullptr) {
          uint8_t* new_value = std::get<uint8_t*>(result);
          // We can ignore the value and size parameters here since the listener can read the updated value directly from the characteristic's value pointer if needed.
          (*instance->update_listener_)(CustomServiceCharacteristicIndex(&characteristic - &instance->characteristics_[0]), new_value, buffer_size);
//          printf("Notified update listener of change to characteristic index %d\n", &characteristic - &instance->characteristics_[0]);
        }
    }
//    printf("Write callback processed for handle 0x%04x, attribute handle 0x%04x\n", con_handle, attribute_handle);
    return 0; // Indicate success for the write operation.
  }
//  printf("Write callback called with handle: 0x%04x, attribute handle: 0x%04x, but no characteristic matched the handle.\n", con_handle, attribute_handle);
  return ATT_ERROR_INVALID_HANDLE;  // Couldn't find the handle, so returning error.
}

uint16_t CustomService::customServiceReadCallback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
  CustomService* instance = CustomService::getInstance(CustomService::GetInstanceCmd::GET_EXISTING_INSTANCE);
  printf("Read callback called with handle: 0x%04x, offset: %d, buffer size: %d\n", attribute_handle, offset, buffer_size);
  for (auto& characteristic : instance->characteristics_)
    if (auto result = characteristic.customServiceCharacteristicRead(con_handle, attribute_handle, offset, buffer, buffer_size); result.has_value())
      return result.value();
  return 0;  // Couldn't find the handle, so returning 0 is all we can do.
}

// Write callback
std::variant<std::monostate, unsigned int, uint8_t*> CustomServiceCharacteristic::customServiceCharacteristicWrite(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
  static std::vector<uint8_t> staging_buffer(256); // Temporary staging buffer for writes
  // Enable/disable notifications
  if (attribute_handle == characteristic_handles_.client_configuration_handle){
//    printf("Attribute handle matches client configuration handle for %s.\nClient configuration write: handle=0x%04x, value=0x%04x\n", characteristic_values_.user_description.data(), attribute_handle, little_endian_read_16(buffer, 0));
	  characteristic_values_.client_configuration = little_endian_read_16(buffer, 0);
    return 0U;
  }

  // Write characteristic value
  if (attribute_handle == characteristic_handles_.value_handle) {
//    printf("Attribute handle matches value handle for %s.\nValue write: handle=0x%04x, offset=%d, buffer size=%d\n", characteristic_values_.user_description.data(), attribute_handle, offset, buffer_size);
    switch (transaction_mode) {
      case ATT_TRANSACTION_MODE_NONE:
//        printf("Transaction mode: None\n");
        if (offset + buffer_size != characteristic_values_.value.size()) {
          printf("Write out of bounds: offset %d + buffer size %d exceeds characteristic value size %d\n", offset, buffer_size, characteristic_values_.value.size());
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH; // Indicate error
        }
        // This is a bit hackey, but the mode selection characteristic is the only one that specifically needs value validation.
        // So if we're writing to that characteristic, do that additional validation.
        if ((attribute_handle == ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_VALUE_HANDLE) &&
            // Example: Validate that mode selection is within expected range (0-4)
            (buffer[0] > 4))
          return 0U;  // Invalid mode, but fail silently.
//        printf("Write within bounds, updating characteristic value.\n");
        memcpy(characteristic_values_.value.data() + offset, buffer, buffer_size);
        break;

      case ATT_TRANSACTION_MODE_ACTIVE:
//        printf("Transaction mode: Active\n");
        // Store data in staging buffer
        if (offset + buffer_size > staging_buffer.size()) {
//          printf("Staging buffer overflow: offset %d + buffer size %d exceeds staging buffer size %d\n", offset, buffer_size, staging_buffer.size());
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_OFFSET; // Indicate error
        }
        return 0U;

      case ATT_TRANSACTION_MODE_VALIDATE:
//        printf("Transaction mode: Validate\n");
        if (offset + buffer_size != characteristic_values_.value.size()) {
//          printf("Validation failed: offset %d + buffer size %d does not match characteristic value size %d\n", offset, buffer_size, characteristic_values_.value.size());
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH; // Indicate error
        }
        memcpy(staging_buffer.data() + offset, buffer, buffer_size);
        return 0U;

      case ATT_TRANSACTION_MODE_EXECUTE:
//        printf("Transaction mode: Execute\n");
        memcpy(characteristic_values_.value.data(), staging_buffer.data(), characteristic_values_.value.size());
        break;

      case ATT_TRANSACTION_MODE_CANCEL:
//        printf("Transaction mode: Cancel\n");
        return 0U;
    
      default:
//        printf("Unknown transaction mode: %d\n", transaction_mode);
        return 0U; // Invalid transaction mode, indicate error.
    }
    return characteristic_values_.value.data(); // Indicate success and provide pointer to new value. 
  }
  return std::monostate{};  // Couldn't find the handle, so returning empty variant to indicate no action taken.
}

std::optional<uint16_t> CustomServiceCharacteristic::customServiceCharacteristicRead(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, unsigned char* buffer, uint16_t buffer_size) {
  if (attribute_handle == characteristic_handles_.value_handle) {
    printf("Read request for %s value handle (0x%04x), offset %d, buffer size %d\n", characteristic_values_.user_description.data(), attribute_handle, offset, buffer_size);
    return att_read_callback_handle_blob(characteristic_values_.value.data(), characteristic_values_.value.size(), offset, buffer, buffer_size);
  }
  if (attribute_handle == characteristic_handles_.client_configuration_handle) {
    printf("Read request for %s client configuration handle (0x%04x), offset %d, buffer size %d\n", characteristic_values_.user_description.data(), attribute_handle, offset, buffer_size);
    return att_read_callback_handle_little_endian_16(characteristic_values_.client_configuration, offset, buffer, buffer_size);
  }
  if (attribute_handle == characteristic_handles_.user_description_handle) {
    printf("Read request for %s user description handle (0x%04x), offset %d, buffer size %d\n", characteristic_values_.user_description.data(), attribute_handle, offset, buffer_size);
    return att_read_callback_handle_blob((unsigned char*)characteristic_values_.user_description.data(), characteristic_values_.user_description.length(), offset, buffer, buffer_size);
  }
  printf("Read request for unknown attribute handle (0x%04x)\n", attribute_handle);
  return std::nullopt;
}

CustomService* CustomService::getInstance(GetInstanceCmd cmd) {
  static CustomService* instance = nullptr;

  if (cmd == GetInstanceCmd::CREATE_INSTANCE && instance == nullptr) {
    printf("Creating CustomService instance...\n");
    instance = new CustomService();
  } else if (cmd == GetInstanceCmd::CLEAR_INSTANCE && instance != nullptr) {
    delete instance;
    instance = nullptr;
  }
  return instance;
}
void deleteTag(void *context, uint32_t tag) {
  // No-op delete tag function for dummy TLV implementation
}
int setTag(void *context, uint32_t tag, const uint8_t *value, uint32_t value_len) {
  // No-op set tag function for dummy TLV implementation
  return 0; // Indicate success
}
int getTag(void * context, uint32_t tag, uint8_t * buffer, uint32_t buffer_size) {
  // No-op get tag function for dummy TLV implementation
  return 0; // Indicate success but no data
}
int btstack_tlv_none_set_tag(const btstack_tlv_t *tlv, void *context, uint32_t tag, const uint8_t *value, uint32_t value_len) {
  // No-op set tag function for dummy TLV implementation
  return 0; // Indicate success
}

CustomServiceErrorCode CustomService::init(CharacteristicUpdateListener* update_listener) {
  static btstack_packet_callback_registration_t att_event_callback_registration;

  printf("Initializing custom GATT service...\n");
  // // Supplanting the TLV implementation that BTstack uses for storing link keys and device info with a dummy implementation that does nothing,
  // // since we don't need that functionality and its flash usage complicates the pwm implementation on the second core.
  // printf("Setting dummy TLV instance for BTstack...\n");
  // const btstack_tlv_t * tlv_impl = btstack_tlv_dummy_get_instance();
  // btstack_tlv_set_instance(tlv_impl, NULL); // Second param is context, not needed here
  // printf("Dummy TLV instance set for BTstack.\n");

  printf("Disabling non-volatile memory storage for BTstack...\n");
  // Register a NULL/no-op TLV backend so LE device DB also stays in RAM
  // Do NOT call btstack_tlv_flash_bank_init() or pico_flash_bank_instance()
  static btstack_tlv_t tlv_impl = {.get_tag = &getTag, .store_tag = &setTag, .delete_tag = &deleteTag};
  btstack_tlv_set_instance(&tlv_impl, NULL);
  printf("Initializing Bluetooth hardware...\n");
  // Initialise the BT/Wi-Fi chip
  if (int fail_code = cyw43_arch_init(); fail_code != 0) {
    printf("Bluetooth init failed with code %d\n", fail_code);
    return CustomServiceErrorCode::CYW43_ARCH_INIT_FAILED;
  }
  printf("Bluetooth hardware initialized successfully.\n");

  const std::array<std::pair<unsigned char*, size_t>, 6>& characteristic_value_ptrs = update_listener->getCharacteristicValuePtrs();
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

  printf("Custom GATT service characteristics initialized successfully.\n");

  // // Service start and end handles (modeled off heartrate example)
  // service_handler.start_handle = 0 ;
  // service_handler.end_handle = 0xFFFF ;
  // service_handler.read_callback = &customServiceReadCallback ;
  // service_handler.write_callback = &customServiceWriteCallback ;
  // // Register the service handler
  // att_server_register_service_handler(&service_handler);
  // inform about BTstack state
  // hci_event_callback_registration.callback = &packet_handler;
  // hci_add_event_handler(&hci_event_callback_registration);

  /*
   * BLE-only security manager: just-works, no bonding, no MITM.
   * sm_set_io_capabilities / sm_set_authentication_requirements
   * govern BLE pairing exclusively (classic BT is disabled).
   */

  l2cap_init();
  printf("L2CAP initialized successfully.\n");


  sm_init();
  sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
  sm_set_authentication_requirements(0);   /* No bonding, no MITM */
  // /* Disable classic BT so only BLE is active. */
  // hci_disable_classic();

  
  att_server_init(profile_data, &customServiceReadCallback, &customServiceWriteCallback);
  printf("ATT server initialized successfully.\n");


  hci_event_callback_registration.callback = &hci_packet_handler;
  hci_add_event_handler(&hci_event_callback_registration);

  /* Load advertisement data and set parameters. */
  gap_advertisements_set_data(adv_data.size(), (uint8_t*) adv_data.data());
  bd_addr_t null_addr;
  memset(null_addr, 0, 6);
  gap_advertisements_set_params(
    0x00A0,   /* min interval: 100 ms  (units: 0.625 ms) */
    0x00A0,   /* max interval: 100 ms                    */
    0 /*ADV_IND*/,  /* connectable undirected                  */
    0,        /* own address type: public                */
    null_addr,  /* no whitelist                            */
    0x07,     /* all channels (37, 38, 39)               */
    0         /* no filter policy                        */
  );
  
  // register for ATT event
  att_server_register_packet_handler(&hci_packet_handler);
  
  // turn on bluetooth!
  hci_power_control(HCI_POWER_ON);
  update_listener_ = update_listener;
  printf("Custom GATT service initialized successfully.\n");
  return CustomServiceErrorCode::ERROR_OK;
}

} // anonymous namespace

std::variant<CustomServiceInterface*, CustomServiceErrorCode> customServiceServerInit(CharacteristicUpdateListener* update_listener) {
  CustomService* service_instance = CustomService::getInstance(CustomService::GetInstanceCmd::CREATE_INSTANCE);
  if (CustomServiceErrorCode error_code = service_instance->init(update_listener);
      error_code != CustomServiceErrorCode::ERROR_OK) {
    CustomService::getInstance(CustomService::GetInstanceCmd::CLEAR_INSTANCE);
    return error_code; // Failed to initialize service, so clean up instance and return nullptr to indicate failure.
  }
  return service_instance;
}