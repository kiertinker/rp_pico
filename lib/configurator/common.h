#pragma once

#include <vector>
#include <string_view>

class ConfigurationValues {
 public:
  virtual bool operator()(std::vector<const char*>& values) = 0;
};

