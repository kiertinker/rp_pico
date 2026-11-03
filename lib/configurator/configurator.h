#pragma once

#include "common.h"

#include <vector>
#include <string_view>

namespace config_provisioning {

enum class ConfigurationError {
  OK,
  INVALID_SSID,
  WPA_AUTH_REQUIRED_PASSWORD,
  ACCESS_POINT_FAILURE
};

enum class WPA_AUTH {
  OPEN = 0,
  WPA_TKIP_PSK = 0x00200002,
  WPA2_AES_PSK = 0x00400004,
  WPA2_MIXED_PSK = 0x00400006,
  WPA3_SAE_AES_PSK = 0x01000004,
  WPA3_WPA2_AES_PSK = 0x01400004
};

struct ConfigOpts {
  WPA_AUTH auth;
  std::string_view ssid;
  std::string_view password;
};

ConfigurationError getConfigurationValues(
    const ConfigOpts& config_opts,
    std::string_view page_title,
    const std::vector<std::string_view>& value_descriptions,
    ConfigurationValues *config_values);

} // namespace config_provisioning