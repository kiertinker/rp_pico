#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

enum class CustomServiceCharacteristicIndex : int {
  MODE_SELECTION = 0,
  STATIC_COLOR_SETTINGS = 1,
  PROGRAM_1_SETTINGS = 2,
  PROGRAM_2_SETTINGS = 3,
  PROGRAM_3_SETTINGS = 4,
  PROGRAM_4_SETTINGS = 5
};

enum class MODE_SELECTION_VALUES : unsigned char {
  MODE_OFF = 0,
  MODE_STATIC_COLOR = 1,
  MODE_PROGRAM_1 = 2,
  MODE_PROGRAM_2 = 3,
  MODE_PROGRAM_3 = 4,
  MODE_PROGRAM_4 = 5
};

static_assert(static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_STATIC_COLOR) == static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_STATIC_COLOR), "MODE_STATIC_COLOR must equal STATIC_COLOR_SETTINGS");
static_assert(static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_PROGRAM_1) == static_cast<unsigned char>(CustomServiceCharacteristicIndex::PROGRAM_1_SETTINGS), "MODE_PROGRAM_1 must equal PROGRAM_1_SETTINGS");
static_assert(static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_PROGRAM_2) == static_cast<unsigned char>(CustomServiceCharacteristicIndex::PROGRAM_2_SETTINGS), "MODE_PROGRAM_2 must equal PROGRAM_2_SETTINGS");
static_assert(static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_PROGRAM_3) == static_cast<unsigned char>(CustomServiceCharacteristicIndex::PROGRAM_3_SETTINGS), "MODE_PROGRAM_3 must equal PROGRAM_3_SETTINGS");
static_assert(static_cast<unsigned char>(MODE_SELECTION_VALUES::MODE_PROGRAM_4) == static_cast<unsigned char>(CustomServiceCharacteristicIndex::PROGRAM_4_SETTINGS), "MODE_PROGRAM_4 must equal PROGRAM_4_SETTINGS");

class CharacteristicUpdateListener {
 public:
  virtual void operator()(CustomServiceCharacteristicIndex index, const unsigned char* value, size_t value_size) = 0;
  virtual std::array<std::pair<unsigned char*, size_t>, 6> getCharacteristicValuePtrs() = 0;
};

enum class CustomServiceErrorCode : int {
  ERROR_OK = 0,
  CYW43_ARCH_INIT_FAILED = 1,
  INVALID_MODE = 2,
  INVALID_OFFSET = 3,
  INVALID_ATTRIBUTE_VALUE_LENGTH = 4
};

class CustomServiceInterface {
 public:
  virtual ~CustomServiceInterface() = default;
};

std::variant<CustomServiceInterface*, CustomServiceErrorCode> customServiceServerInit(CharacteristicUpdateListener* update_listener);
