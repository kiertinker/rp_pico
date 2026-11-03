#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include <string_view>
#include "configurator.h"

using namespace config_provisioning;
class ConfiguratorTesterValues : public ConfigurationValues {
 public:
  ConfiguratorTesterValues() : ConfigurationValues() {}
  bool operator()(std::vector<const char*>& values) override {
    if (values.size() != 2) {
      printf("Error: Expected 2 values, got %zu\n", values.size());
      return false; // Error code for incorrect number of values
    }

    bool success = true; // Set this to true if you want to treat empty values as errors
    // Process the first value
    std::string_view firstValue = values[0];
    printf("First value: %.*s\n", static_cast<int>(firstValue.size()), firstValue.data());
    if (firstValue.empty()) {
      printf("Error: First value is empty\n");
      values[0] = nullptr; // Set to nullptr to indicate an error
      success = false;
    }

    // Process the second value
    std::string_view secondValue = values[1];
    printf("Second value: %.*s\n", static_cast<int>(secondValue.size()), secondValue.data());
    if (secondValue.empty()) {
      printf("Error: Second value is empty\n");
      values[1] = nullptr; // Set to nullptr to indicate an error
      success = false;
    }

    return success;
  }

};

int main() {
  stdio_init_all();

  ConfigOpts opts = {WPA_AUTH::OPEN, "test_ssid", ""};
  ConfiguratorTesterValues configValues;
  getConfigurationValues(opts, "WIFI Credentials", std::vector<std::string_view>{"value1", "value2"}, static_cast<ConfigurationValues*>(&configValues));
  cyw43_arch_init();  // Initialize the CYW43 architecture for led blinking
  bool led = false;
  printf("Starting LED blink loop.\n");
  while (true) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led = !led);
    sleep_ms(500);
  }
  return 0;
}
