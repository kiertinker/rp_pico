#include "ble/att_db.h"
#include "ble/att_server.h"
#include "bluetooth_gatt.h"
#include "btstack.h"
#include "btstack_defines.h"
#include "btstack_debug.h"
#include "btstack_util.h"

// #include "gatt_led_pwm_control_server.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

enum class CustomServiceCharacteristicIndex : int {
  MODE_SELECTION = 0,
  STATIC_COLOR_SETTINGS = 1,
  PROGRAM_1_SETTINGS = 2,
  PROGRAM_2_SETTINGS = 3,
  PROGRAM_3_SETTINGS = 4,
  PROGRAM_4_SETTINGS = 5
};
// Struct for managing this service
struct CharacteristicValues {
  unsigned char* value;
  size_t value_size;
  uint16_t client_configuration;
  std::string_view user_description;
};

struct CharacteristicHandles {
  uint16_t value_handle;
  uint16_t client_configuration_handle;
  uint16_t user_description_handle;
};

class CustomService {
 public:
  // Connection handle for service
  static hci_con_handle_t con_handle_;
  CharacteristicValues characteristic_values_;
  CharacteristicHandles characteristic_handles_;
  // Callback function
  btstack_context_callback_registration_t callback;
  void setHandles(const CharacteristicHandles& handles) {
	characteristic_handles_ = handles;
  }
  void setValues(const CharacteristicValues& values) {
	characteristic_values_ = values;
  }
  std::optional<uint16_t> customServiceRead(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, unsigned char* buffer, uint16_t buffer_size);
  std::optional<int> customServiceWrite(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
};

// Flag for general discoverability
constexpr uint8_t APP_AD_FLAGS = 0x06;

// GAP data packet (must not exceed 32 bytes)
static std::array<uint8_t, 13> adv_data = {
    // Flags general discoverable
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, APP_AD_FLAGS,
    // Name
    0x05, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'P', 'i', 'c', 'o',
    // Custom Service UUID
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x10, 0xFF,
};
// // Create a callback registration object, and an att service handler object
// static att_service_handler_t 	service_handler ;
// static std::array<CustomService, 6> service_objects ;

// // Protothreads semaphore
// semaphore_t BLUETOOTH_READY ;

// // Callback functions for ATT notifications on characteristics
// static void characteristic_callback(void * context){
// 	// Associate the void pointer input with our custom service object
// 	custom_service_t * instance = (custom_service_t *) context ;
// 	// Send a notification
// 	att_server_notify(instance->con_handle, instance->characteristic_handle, instance->characteristic_value, strlen(instance->characteristic_value)) ;
// }


// // Read callback (no client configuration handles on characteristics without Notify)
// static uint16_t custom_service_read_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
// 	UNUSED(con_handle);

// 	// Characteristic A
// 	if (attribute_handle == service_object.characteristic_a_handle){
// 		return att_read_callback_handle_blob(service_object.characteristic_a_value, strlen(service_object.characteristic_a_value), offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_a_client_configuration_handle){
// 		return att_read_callback_handle_little_endian_16(service_object.characteristic_a_client_configuration, offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_a_user_description_handle) {
// 		return att_read_callback_handle_blob(service_object.characteristic_a_user_description, strlen(service_object.characteristic_a_user_description), offset, buffer, buffer_size);
// 	}

// 	// Characteristic B
// 	if (attribute_handle == service_object.characteristic_b_handle){
// 		return att_read_callback_handle_blob(service_object.characteristic_b_value, strlen(service_object.characteristic_b_value), offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_b_client_configuration_handle){
// 		return att_read_callback_handle_little_endian_16(service_object.characteristic_b_client_configuration, offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_b_user_description_handle) {
// 		return att_read_callback_handle_blob(service_object.characteristic_b_user_description, strlen(service_object.characteristic_b_user_description), offset, buffer, buffer_size);
// 	}

// 	// Characteristic C
// 	if (attribute_handle == service_object.characteristic_c_handle){
// 		return att_read_callback_handle_blob(service_object.characteristic_c_value, strlen(service_object.characteristic_c_value), offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_c_client_configuration_handle){
// 		return att_read_callback_handle_little_endian_16(service_object.characteristic_c_client_configuration, offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_c_user_description_handle) {
// 		return att_read_callback_handle_blob(service_object.characteristic_c_user_description, strlen(service_object.characteristic_c_user_description), offset, buffer, buffer_size);
// 	}

// 	// Characteristic D
// 	if (attribute_handle == service_object.characteristic_d_handle){
// 		return att_read_callback_handle_blob(service_object.characteristic_d_value, strlen(service_object.characteristic_d_value), offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_d_client_configuration_handle){
// 		return att_read_callback_handle_little_endian_16(service_object.characteristic_d_client_configuration, offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_d_user_description_handle) {
// 		return att_read_callback_handle_blob(service_object.characteristic_d_user_description, strlen(service_object.characteristic_d_user_description), offset, buffer, buffer_size);
// 	}

// 	// Characteristic E
// 	if (attribute_handle == service_object.characteristic_e_handle){
// 		return att_read_callback_handle_blob(service_object.characteristic_e_value, strlen(service_object.characteristic_e_value), offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_e_user_description_handle) {
// 		return att_read_callback_handle_blob(service_object.characteristic_e_user_description, strlen(service_object.characteristic_e_user_description), offset, buffer, buffer_size);
// 	}

// 	// Characteristic E
// 	if (attribute_handle == service_object.characteristic_f_handle){
// 		return att_read_callback_handle_blob(service_object.characteristic_f_value, strlen(service_object.characteristic_f_value), offset, buffer, buffer_size);
// 	}
// 	if (attribute_handle == service_object.characteristic_f_user_description_handle) {
// 		return att_read_callback_handle_blob(service_object.characteristic_f_user_description, strlen(service_object.characteristic_f_user_description), offset, buffer, buffer_size);
// 	}

// 	return 0;
// }

// // Write callback
// static int custom_service_write_callback(hci_con_handle_t con_handle, uint16_t attribute_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
// 	UNUSED(transaction_mode);
// 	UNUSED(offset);
// 	UNUSED(buffer_size);

// 	// Enable/disable notifications
// 	if (attribute_handle == service_object.characteristic_a_client_configuration_handle){
// 		service_object.characteristic_a_client_configuration = little_endian_read_16(buffer, 0);
// 		service_object.con_handle = con_handle;
// 	}
// 	// Enable/disable notifications
// 	if (attribute_handle == service_object.characteristic_b_client_configuration_handle){
// 		service_object.characteristic_b_client_configuration = little_endian_read_16(buffer, 0);
// 		service_object.con_handle = con_handle;
// 	}

// 	// Write characteristic value
// 	if (attribute_handle == service_object.characteristic_b_handle) {
// 		custom_service_t * instance = &service_object ;
// 		buffer[buffer_size] = 0 ;
// 		memset(service_object.characteristic_b_value, 0, strlen(service_object.characteristic_b_value)) ;
// 		memcpy(service_object.characteristic_b_value, buffer, strlen(buffer)) ;
// 		// If client has enabled notifications, register a callback
// 		if (instance->characteristic_b_client_configuration) {
// 			instance->callback_b.callback = &characteristic_b_callback ;
// 			instance->callback_b.context = (void*) instance ;
// 			att_server_register_can_send_now_callback(&instance->callback_b, instance->con_handle) ;
// 		}
// 		// Alert the application of a bluetooth RX
//         PT_SEM_SDK_SIGNAL(pt, &BLUETOOTH_READY) ;
// 	}

// 	// Enable/disable notificatins
// 	if (attribute_handle == service_object.characteristic_c_client_configuration_handle){
// 		service_object.characteristic_c_client_configuration = little_endian_read_16(buffer, 0);
// 		service_object.con_handle = con_handle;
// 	}

// 	// Enable/disable notificatins
// 	if (attribute_handle == service_object.characteristic_d_client_configuration_handle){
// 		service_object.characteristic_d_client_configuration = little_endian_read_16(buffer, 0);
// 		service_object.con_handle = con_handle;
// 	}

// 	// Write characteristic value
// 	if (attribute_handle == service_object.characteristic_d_handle) {
// 		custom_service_t * instance = &service_object ;
// 		buffer[buffer_size] = 0 ;
// 		memset(service_object.characteristic_d_value, 0, sizeof(service_object.characteristic_d_value)) ;
// 		memcpy(service_object.characteristic_d_value, buffer, buffer_size) ;
// 		// Null-terminate the string
// 		service_object.characteristic_d_value[buffer_size] = 0 ;
// 		// If client has enabled notifications, register a callback
// 		if (instance->characteristic_d_client_configuration) {
// 			instance->callback_d.callback = &characteristic_d_callback ;
// 			instance->callback_d.context = (void*) instance ;
// 			att_server_register_can_send_now_callback(&instance->callback_d, instance->con_handle) ;
// 		}
// 		// Alert the application of a bluetooth RX
//         PT_SEM_SDK_SIGNAL(pt, &BLUETOOTH_READY) ;
// 	}

// 	// Write characteristic value
// 	if (attribute_handle == service_object.characteristic_e_handle) {
// 		custom_service_t * instance = &service_object ;
// 		buffer[buffer_size] = 0 ;
// 		if (!strcmp(buffer, "OFF")) {
// 			memset(service_object.characteristic_e_value, 0, sizeof(service_object.characteristic_e_value)) ;
// 			memcpy(service_object.characteristic_e_value, buffer, buffer_size) ;
// 			service_object.characteristic_e_value[buffer_size] = 0 ;
// 			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
// 			// Alert the application of a bluetooth RX
// 			PT_SEM_SDK_SIGNAL(pt, &BLUETOOTH_READY) ;
// 		}
// 		else if (!strcmp(buffer, "ON")) {
// 			memset(service_object.characteristic_e_value, 0, sizeof(service_object.characteristic_e_value)) ;
// 			memcpy(service_object.characteristic_e_value, buffer, buffer_size) ;
// 			service_object.characteristic_e_value[buffer_size] = 0 ;
// 			cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
// 			// Alert the application of a bluetooth RX
// 			PT_SEM_SDK_SIGNAL(pt, &BLUETOOTH_READY) ;
// 		}
// 	}

// 	// Write characteristic value
// 	if (attribute_handle == service_object.characteristic_f_handle) {
// 		custom_service_t * instance = &service_object ;
// 		buffer[buffer_size] = 0 ;
// 		if(atoi(buffer)<16) {
// 			memset(service_object.characteristic_f_value, 0, strlen(service_object.characteristic_f_value)) ;
// 			memcpy(service_object.characteristic_f_value, buffer, strlen(buffer)) ;
// 			// Null-terminate the string
// 			service_object.characteristic_f_value[buffer_size] = 0 ;
// 			// Alert the application of a bluetooth RX
//         	PT_SEM_SDK_SIGNAL(pt, &BLUETOOTH_READY) ;
// 		}
// 	}

// 	return 0;
// }


// /////////////////////////////////////////////////////////////////////////////
// ////////////////////////////// USER API /////////////////////////////////////
// /////////////////////////////////////////////////////////////////////////////

// // Initialize our custom service handler
// void custom_service_server_init(std::array<std::pair<unsigned char*, size_t>, 6>& characteristic_values_ptrs);

// // Update Characteristic A value
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
