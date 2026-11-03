#pragma once

#include "common.h"

#include <vector>
#include <string_view>

void getConfigsFromHttpServer(
    std::string_view page_title,
    const std::vector<std::string_view>& value_descriptions,
    ConfigurationValues* config_values);