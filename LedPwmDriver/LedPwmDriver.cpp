#include <stdio.h>

#include "gatt_led_pwm_control_server.h"

// stl
#include "vector"

// BTstack
#include "btstack.h"

// Pico SDK
#include "pico/stdlib.h"
#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"

int64_t alarm_callback(alarm_id_t id, void *user_data) {
    // Put your timeout handler code in here
    return 0;
}

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

int main() {
  stdio_init_all();
  // Initialise the BT/Wi-Fi chip
  if (int fail_code = cyw43_arch_init(); fail_code != 0) {
      printf("Bluetooth init failed with code %d\n", fail_code);
      return -1;
  }

  // Initialize L2CAP and security manager
  l2cap_init();
  sm_init();
  // Initialize ATT server, no general read/write callbacks because we'll set one up for each service
  att_server_init(profile_data, NULL, NULL);   

  // Instantiate our custom service handler
  // inform about BTstack state
  hci_event_callback_registration.callback = &packet_handler;
  hci_add_event_handler(&hci_event_callback_registration);
  
  // register for ATT event
  att_server_register_packet_handler(packet_handler);
  
  // turn on bluetooth!
  hci_power_control(HCI_POWER_ON);
  
  // Timer example code - This example fires off the callback after 2000ms
  add_alarm_in_ms(2000, alarm_callback, NULL, false);
  // For more examples of timer use see https://github.com/raspberrypi/pico-examples/tree/master/timer

//   printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
//   printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
  // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks
  // Enable wifi station
  cyw43_arch_enable_sta_mode();
  while (true) {
    printf("Hello, world!\n");
    sleep_ms(1000);
  }
}
