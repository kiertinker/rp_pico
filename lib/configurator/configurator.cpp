#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"
#include "dhcpserver.h"   // from pico-examples: pico_w/wifi/access_point/dhcpserver

#include "configurator.h"
#include "http_server.h"
#include "utils/scope_guard.h"


namespace config_provisioning {

ConfigurationError getConfigurationValues(
    const ConfigOpts& config_opts,
    std::string_view page_title,
    const std::vector<std::string_view>& value_descriptions,
    ConfigurationValues* config_values) {
  if (config_opts.ssid.size() == 0) return ConfigurationError::INVALID_SSID;
  if (config_opts.auth != WPA_AUTH::OPEN && config_opts.password.size() == 0) return ConfigurationError::WPA_AUTH_REQUIRED_PASSWORD;
  if (cyw43_arch_init()) return ConfigurationError::ACCESS_POINT_FAILURE;

  ScopeGuard arch_deinit_guard([]() { cyw43_arch_deinit(); });

  // Start the access point (WPA2)
  cyw43_arch_enable_ap_mode(
      config_opts.ssid.data(),
      config_opts.auth == WPA_AUTH::OPEN ? nullptr : config_opts.password.data(),
      static_cast<uint32_t>(config_opts.auth));
  ScopeGuard ap_mode_disable_guard([]() { cyw43_arch_disable_ap_mode(); });
 
  // Gateway address and subnet mask handed out to clients
  ip_addr_t gw, mask;
  IP4_ADDR(ip_2_ip4(&gw), 192, 168, 4, 1);
  IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

  // DHCP server so connected devices get an address automatically
  dhcp_server_t dhcp_server;
  dhcp_server_init(&dhcp_server, &gw, &mask);
  ScopeGuard dhcp_server_deinit_guard([&dhcp_server]() { dhcp_server_deinit(&dhcp_server); });

  // Blink the onboard LED so you can see it's running
  getConfigsFromHttpServer(page_title, value_descriptions, config_values);
  return ConfigurationError::OK;
}

}