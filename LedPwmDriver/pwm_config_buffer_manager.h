#pragma once

#include "gatt_service.h"

#include "pico/critical_section.h"
#include "pico/mutex.h"

#include <array>
#include <cstddef>
#include <variant>

// These need to go into a standalone library if we want to reuse them across projects, but for now we can just define them here.
struct CriticalSection {
  critical_section_t cs_;
  CriticalSection() { critical_section_init(&cs_); }
  ~CriticalSection() { critical_section_deinit(&cs_); }
  void enter() { critical_section_enter_blocking(&cs_); }
  void exit() { critical_section_exit(&cs_); }
};

struct Mutex {
  mutex_t mutex_;
  Mutex() { mutex_init(&mutex_); }
  ~Mutex() = default;
  void enter() { mutex_enter_blocking(&mutex_); }
  void exit() { mutex_exit(&mutex_); }
};

template<typename T>
class LockGuard {
  T& lock_;
 public:
  LockGuard(T& lock) : lock_(lock) { lock_.enter(); }
  ~LockGuard() { lock_.exit(); }
};

// Size of the PWM config data in bytes
// Data format is as follows:
// Bytes 0 - 7 Fowler-Noll-Vo hash of the data (excluding these first 8 bytes)
// Byte 8 [Mode]: Indicates the mode of operation (0 = Static, 1 - 4 = program 1,2,3, or 4)
// Bytes 9 - 16 [Static Settings]: Channel 1 R, G, B, W; Channel 2 R, G, B, W
// Bytes 16 - 204 [Program 1 Settings (189 Bytes)]:
//    Bytes 16 - 23 [Starting Entry]:
//      [16] First byte unused
//      [17 - 24] Channel 1 R, G, B, W; Channel 2 R, G, B, W
//    Entry 1 - Entry 20 (9 Bytes each):
//      First Byte is duration of the entry.
//      Remaining 8 Bytes are Channel 1 R, G, B, W; Channel 2 R, G, B, W.
//          Highest bit of each color byte indicates if that color is to be faded to (1) or jumped to (0).
// Repeat for Programs 2 (bytes 204 - 391), 3 (bytes 392 - 579), and 4 (bytes 580 - 767)
constexpr size_t PWM_CONFIG_SIZE = 773;  // Includes 8 bytes for hash + 765 bytes for data
constexpr size_t HASH_SIZE = 8;  // 64-bit FNV-1a hash size
constexpr size_t PROGRAM_ENTRY_SIZE = 9;  // 1 byte duration or mode + 8 bytes for 2 channels.
constexpr size_t PROGRAM_ENTRIES = 20;
constexpr size_t PROGRAMS = 4;
constexpr size_t PROGRAM_START_OFFSET = 9 + 8; // 8 bytes for hash, 1 byte for mode, 8 bytes for static settings
constexpr size_t PROGRAM_SIZE = 189; // 9 (starting entry) + 20 * 9 (entries)

// Program and ProgramEntry classes are simple wrappers around the raw config data that allow us to easily construct
// Program and ProgramEntry objects from the raw data and also easily access raw data in a structured way.
// As such Copy and Assignment operations for these classes do not actually copy the underlying data,
// they just create a new object that references the same underlying data (for copy construction)
// or overwrite the underlying data pointer (for assignment).
// If you need to actually copy the data, you can use the copyTo method which copies the data to a provided buffer.
struct Program {
  struct ProgramEntry {
    struct Channel {
      struct Color {
        Color(unsigned char& byte_ref) : value(byte_ref) {}
        unsigned char& value;
        bool is_fade() const { return (value & 0x80) != 0; }
        // For magnitude, we filter out fade bit and normalize to 0-255.
        unsigned short get_magnitude() const { return static_cast<unsigned short>((value & 0x7F) << 1); }
      };

      Channel(unsigned char* base) : red(base[0]), green(base[1]), blue(base[2]), white(base[3]) {}
      Channel(const Channel& other) = delete;  // Disable copy constructor to prevent accidentally copying the channel data instead of referencing it.
      void operator=(const Channel& other) = delete;  // Disable copy assignment to prevent accidentally copying the channel data instead of referencing it.
      Color red, green, blue, white;
    };

    ProgramEntry(unsigned char* base, size_t index)
        : duration(base[index * PROGRAM_ENTRY_SIZE]),
        left_channel(base + index * PROGRAM_ENTRY_SIZE + 1),
        right_channel(base + index * PROGRAM_ENTRY_SIZE + 5) {}
    // Copy constructor creates a new ProgramEntry object that references the same underlying data.
    ProgramEntry(const ProgramEntry& other) = delete; // We delete the copy constructor to prevent accidentally copying the entry data instead of referencing it. If we want to copy the data, we can use the copyTo method.
    unsigned char* getBaseAddr() const { return reinterpret_cast<unsigned char*>(&duration); }
    // Assignment operator just changes the reference to the underlying data, it does not copy the data itself.
    void operator=(const Program& other) = delete; // We delete the assignment operator to prevent accidentally copying the entry data instead of referencing it. If we want to copy the data, we can use the copyTo method.
    unsigned char& duration;
    Channel left_channel, right_channel;
    std::array<Channel::Color*, 8> color_array = {
      &left_channel.red, &left_channel.green, &left_channel.blue, &left_channel.white,
      &right_channel.red, &right_channel.green, &right_channel.blue, &right_channel.white
    };
    void copyTo(unsigned char* dest) const {
      unsigned char* base_addr = getBaseAddr();
      std::copy(base_addr, base_addr + PROGRAM_ENTRY_SIZE, dest);
    }
  };

  Program(unsigned char* base, size_t index)
      : base_addr_(base + index * PROGRAM_SIZE),
          starting_entry_(base_addr_, 0),
          entries_{{  // First '{' is for the std::array, second is for the initializer list 
              {base_addr_, 1}, {base_addr_, 2}, {base_addr_, 3}, {base_addr_, 4}, {base_addr_, 5},
              {base_addr_, 6}, {base_addr_, 7}, {base_addr_, 8}, {base_addr_, 9}, {base_addr_, 10},
              {base_addr_, 11}, {base_addr_, 12}, {base_addr_, 13}, {base_addr_, 14}, {base_addr_, 15},
              {base_addr_, 16}, {base_addr_, 17}, {base_addr_, 18}, {base_addr_, 19}, {base_addr_, 20}}} {}
  // We delete the copy and constructor to prevent accidentally copying the program data instead of referencing it. If we want to copy the data, we can use the copyTo method.
  Program(const Program& other) = delete;
  void operator=(const Program& other) = delete; // We delete the assignment operator to prevent accidentally copying the program data instead of referencing it. If we want to copy the data, we can use the copyTo method.  
  const unsigned char* getBaseAddr() const { return base_addr_; }
  void copyTo(unsigned char* dest) const { std::copy(base_addr_, base_addr_ + PROGRAM_SIZE, dest); }
  bool operator==(Program& other) const { return std::equal(base_addr_, base_addr_ + PROGRAM_SIZE, other.base_addr_); }

  unsigned char* base_addr_;
  ProgramEntry starting_entry_;
  std::array<ProgramEntry, 20> entries_;
};

class StateChangeListener {
 public:
  // This can be either monostate (off), a ProgramEntry (if mode is static), or a Program (if mode is one of the program modes).
  virtual void operator()(const std::variant<std::monostate, Program::ProgramEntry, Program>& new_state) = 0;
  virtual void waitForFlashAllowed() = 0;
  virtual void notifyFlashComplete() = 0;
};

class PwmConfigDataInterface : public CharacteristicUpdateListener {
 public:
  virtual ~PwmConfigDataInterface() = default;
  virtual std::array<std::pair<unsigned char*, size_t>, 6> getCharacteristicValuePtrs() = 0;
};

PwmConfigDataInterface* pwmConfigDataInterfaceInit(StateChangeListener* listener);
