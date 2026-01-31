#include <array>
#include <cstddef>

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
// Nearest graduation of Flash page size (256 bytes) not less than PWM_CONFIG_SIZE for program operations.
constexpr size_t PAGE_SIZE_ALLOC_CONFIG = 1024;
constexpr size_t HASH_SIZE = 8;  // 64-bit FNV-1a hash size
constexpr size_t PROGRAM_ENTRY_SIZE = 9;  // 1 byte duration or mode + 8 bytes for 2 channels.
constexpr size_t PROGRAM_ENTRIES = 20;
constexpr size_t PROGRAMS = 4;
constexpr size_t PROGRAM_START_OFFSET = 9 + 8; // 8 bytes for hash, 1 byte for mode, 8 bytes for static settings
constexpr size_t PROGRAM_SIZE = 189; // 9 (starting entry) + 20 * 9 (entries)

class PwmConfigData {
 public:
  struct Program {
    struct ProgramEntry {
      struct Channel {
        struct Color {
            Color(unsigned char& byte_ref) : value(byte_ref) {}
            unsigned char& value;
            bool is_fade() const { return (value & 0x80) != 0; }
            unsigned char get_magnitude() const { return value & 0x7F; }
        };
        Channel(unsigned char* base) : red(base[0]), green(base[1]), blue(base[2]), white(base[3]) {}
        Color red, green, blue, white;
      };
      ProgramEntry(unsigned char* base, size_t index)
          : duration(base[index * PROGRAM_ENTRY_SIZE]),
          left_channel(base + index * PROGRAM_ENTRY_SIZE + 1),
          right_channel(base + index * PROGRAM_ENTRY_SIZE + 5) {}
      unsigned char& duration;
      Channel left_channel, right_channel;
    };
    Program(unsigned char* base, size_t index)
        : base_addr_(base + index * PROGRAM_SIZE),
            starting_entry_(base_addr_, 0),
            entries_{{  // First '{' is for the std::array, second is for the initializer list 
                {base_addr_, 1}, {base_addr_, 2}, {base_addr_, 3}, {base_addr_, 4}, {base_addr_, 5},
                {base_addr_, 6}, {base_addr_, 7}, {base_addr_, 8}, {base_addr_, 9}, {base_addr_, 10},
                {base_addr_, 11}, {base_addr_, 12}, {base_addr_, 13}, {base_addr_, 14}, {base_addr_, 15},
                {base_addr_, 16}, {base_addr_, 17}, {base_addr_, 18}, {base_addr_, 19}, {base_addr_, 20}}} {}

   private:
    unsigned char* base_addr_;
    ProgramEntry starting_entry_;
    std::array<ProgramEntry, 20> entries_;
  };

 private:
  // Config size is PWM_CONFIG_SIZE bytes but we need this to be an even flash page size increment.
  unsigned char data_[PAGE_SIZE_ALLOC_CONFIG];
  unsigned long long& hash_bits_ = *reinterpret_cast<unsigned long long*>(data_);
  Program::ProgramEntry static_settings_{data_ + HASH_SIZE, 0};
  std::array<Program, 4> programs_ = {{  // First '{' is for the std::array, second is for the initializer list
      {data_ + PROGRAM_START_OFFSET, 0}, {data_ + PROGRAM_START_OFFSET, 1},
      {data_ + PROGRAM_START_OFFSET, 2}, {data_ + PROGRAM_START_OFFSET, 3}}};

 public:
   PwmConfigData();
  // Delete the copy & assignment constructors
  PwmConfigData(const PwmConfigData&) = delete;
  PwmConfigData& operator=(const PwmConfigData&) = delete;
};