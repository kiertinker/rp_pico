#include "pwm_driver.h"
#include "gatt_service.h"
#include "pwm_config_buffer_manager.h"

#include "pico/stdio.h"

#include <stdio.h>

#include <memory>
#include <variant>


int main() {
  stdio_init_all();
  sleep_ms(5000);
  for (int i = 5; i > 0; --i) {
    printf("Starting in %d seconds...\n", i);
    sleep_ms(1000);
  }
  printf("Starting main function...\n");
  
  // Example GPIO pins for two RGBW channels
  std::unique_ptr<IPwmDriver> driver(ledPwmDriverInit());
  std::unique_ptr<PwmConfigDataInterface> dataInterface(pwmConfigDataInterfaceInit(driver.get()));
  std::variant<CustomServiceInterface*, CustomServiceErrorCode> serviceInitResult = customServiceServerInit(dataInterface.get());
  if (std::holds_alternative<CustomServiceErrorCode>(serviceInitResult)) {
    // Handle error case (e.g., log the error code)
    CustomServiceErrorCode error_code = std::get<CustomServiceErrorCode>(serviceInitResult);
    printf("Failed to initialize custom service with error code: %d\n", static_cast<int>(error_code));
    return -1; // Exit or handle as appropriate for your application
  }
  std::unique_ptr<CustomServiceInterface> customService(std::get<CustomServiceInterface*>(serviceInitResult));

  while (true) sleep_ms(1000);
  return 0;
}