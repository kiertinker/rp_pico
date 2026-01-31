#include "gatt_service.h"

#include "btstack_defines.h"
#include "ble/att_db.h"
#include "ble/att_server.h"
#include "btstack_util.h"
#include "bluetooth_gatt.h"
#include "btstack_debug.h"

#include "gatt_led_pwm_control_server.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
namespace {

/// @brief Array of custom service objects
std::array<CustomService, 6> service_objects;
att_service_handler_t service_handler ;


// Characteristic user descriptions (appear in LightBlue app)
constexpr std::array<std::string_view, 6> characteristic_user_descriptions = {{
    "Mode Selection",
    "Static Color Settings",
    "Program 1 Settings",
    "Program 2 Settings",
    "Program 3 Settings",
    "Program 4 Settings"
}};

// Callback functions for ATT notifications on characteristics
void characteristic_callback(void * context){
  // Associate the void pointer input with our custom service object
  CustomService* instance = reinterpret_cast<CustomService*>(context);
  // Send a notification
  att_server_notify(instance->con_handle_, instance->characteristic_handles_.value_handle,
      instance->characteristic_values_.value, instance->characteristic_values_.value_size);
}

int customServiceWriteCallback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
  for (auto& service : service_objects)
    if (auto result = service.customServiceWrite(con_handle, attribute_handle, transaction_mode, offset, buffer, buffer_size); result.has_value())
      return result.value();
  return ATT_ERROR_INVALID_HANDLE;  // Couldn't find the handle, so returning error.
}

uint16_t customServiceReadCallback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
  for (auto& service : service_objects)
    if (auto result = service.customServiceRead(con_handle, attribute_handle, offset, buffer, buffer_size); result.has_value())
      return result.value();
  return 0;  // Couldn't find the handle, so returning 0 is all we can do.
}

} // anonymous namespace


// Write callback
std::optional<int> CustomService::customServiceWrite(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
  static std::vector<uint8_t> staging_buffer(256); // Temporary staging buffer for writes
  // Enable/disable notifications
  if (attribute_handle == characteristic_handles_.client_configuration_handle){
	characteristic_values_.client_configuration = little_endian_read_16(buffer, 0);
	con_handle_ = con_handle;
  }

  // Write characteristic value
  if (attribute_handle == characteristic_handles_.value_handle) {
    switch (transaction_mode) {
      case ATT_TRANSACTION_MODE_NONE:
        if (offset + buffer_size != characteristic_values_.value_size) {
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH; // Indicate error
        }
        // This is a bit hackey, but the mode selection characteristic is the only one that specifically needs value validation.
        // So if we're writing to that characteristic, do that additional validation.
        if ((attribute_handle == ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_VALUE_HANDLE) &&
            // Example: Validate that mode selection is within expected range (0-4)
            (buffer[0] > 4))
              return 0;  // Invalid mode, but fail silently.
        memcpy(characteristic_values_.value + offset, buffer, buffer_size);
        break;
      case ATT_TRANSACTION_MODE_ACTIVE:
        // Store data in staging buffer
        if (offset + buffer_size > staging_buffer.size()) {
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_OFFSET; // Indicate error
        }
        break;
      case ATT_TRANSACTION_MODE_VALIDATE:
        if (offset + buffer_size != characteristic_values_.value_size) {
          // Out of bounds write, ignore or handle error as needed
          return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH; // Indicate error
        }
        memcpy(staging_buffer.data() + offset, buffer, buffer_size);
        break;
      case ATT_TRANSACTION_MODE_EXECUTE:
        memcpy(characteristic_values_.value, staging_buffer.data(), characteristic_values_.value_size);
        break;

      case ATT_TRANSACTION_MODE_CANCEL:
        /* code */
        break;
    
      default:
        break;
    }
    return 0;
  }
  return std::nullopt;
}

std::optional<uint16_t> CustomService::customServiceRead(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, unsigned char* buffer, uint16_t buffer_size) {
  if (attribute_handle == characteristic_handles_.value_handle)
    return att_read_callback_handle_blob(characteristic_values_.value, characteristic_values_.value_size, offset, buffer, buffer_size);
  if (attribute_handle == characteristic_handles_.client_configuration_handle)
    return att_read_callback_handle_little_endian_16(characteristic_values_.client_configuration, offset, buffer, buffer_size);
  if (attribute_handle == characteristic_handles_.user_description_handle)
    return att_read_callback_handle_blob((unsigned char*)characteristic_values_.user_description.data(), characteristic_values_.user_description.length(), offset, buffer, buffer_size);
  return std::nullopt;
}


/////////////////////////////////////////////////////////////////////////////
////////////////////////////// USER API /////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

// Initialize our custom service handler
void customServiceServerInit(std::array<std::pair<unsigned char*, size_t>, 6>& characteristic_value_ptrs) {
  std::array<CharacteristicHandles, 6> characteristic_handles_array = {{
    {ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_D10DAD41_F565_11F0_AB18_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_DC7065C1_F565_11F0_AE0B_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_DC7065C1_F565_11F0_AE0B_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_DC7065C1_F565_11F0_AE0B_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_E3B9D14B_F565_11F0_B435_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_E3B9D14B_F565_11F0_B435_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_E3B9D14B_F565_11F0_B435_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_EBF01199_F565_11F0_A5C8_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_EBF01199_F565_11F0_A5C8_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_EBF01199_F565_11F0_A5C8_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_F54B9A83_F565_11F0_8BAF_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_F54B9A83_F565_11F0_8BAF_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_F54B9A83_F565_11F0_8BAF_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE},
    {ATT_CHARACTERISTIC_5375A4AA_F568_11F0_8B5D_8CF8C5822DAE_01_VALUE_HANDLE, ATT_CHARACTERISTIC_5375A4AA_F568_11F0_8B5D_8CF8C5822DAE_01_CLIENT_CONFIGURATION_HANDLE, ATT_CHARACTERISTIC_5375A4AA_F568_11F0_8B5D_8CF8C5822DAE_01_USER_DESCRIPTION_HANDLE}
  }};

  for (int i = 0; i < service_objects.size(); ++i) {
    service_objects[i].setHandles(characteristic_handles_array[i]);
    service_objects[i].setValues({characteristic_value_ptrs[i].first, characteristic_value_ptrs[i].second, 0, characteristic_user_descriptions[i]});
  }


  // Service start and end handles (modeled off heartrate example)
  service_handler.start_handle = 0 ;
  service_handler.end_handle = 0xFFFF ;
  service_handler.read_callback = &customServiceReadCallback ;
  service_handler.write_callback = &customServiceWriteCallback ;
  // Register the service handler
  att_server_register_service_handler(&service_handler);
}

// Update Characteristic A value
// void set_characteristic_a_value(int value){

// 	// Pointer to our service object
// 	custom_service_t * instance = &service_object ;

// 	// Update field value
// 	sprintf(instance->characteristic_a_value, "%d", value) ;

// 	// Are notifications enabled? If so, register a callback
// 	if (instance->characteristic_a_client_configuration){
// 		instance->callback_a.callback = &characteristic_a_callback;
// 		instance->callback_a.context  = (void*) instance;
// 		att_server_register_can_send_now_callback(&instance->callback_a, instance->con_handle);;
// 	}
// }

// // Update Characteristic C value
// void set_characteristic_c_value(char * c_ptr) {

// 	// Pointer to our service object
// 	custom_service_t * instance = &service_object ;

// 	// Point the c characteristic value to our character array input
// 	instance->characteristic_c_value = c_ptr ;

// 	// If client has enabled notifications, register a callback
// 	if (instance->characteristic_c_client_configuration) {
// 		instance->callback_c.callback = &characteristic_c_callback ;
// 		instance->callback_c.context = (void*) instance ;
// 		att_server_register_can_send_now_callback(&instance->callback_c, instance->con_handle) ;
// 	}
// }
