// //////////////////////////////////////////////////////////
// toojpeg.cpp
// written by Stephan Brumme, 2018-2019
// see https://create.stephan-brumme.com/toojpeg/
//

#include "too_jpeg/too_jpeg.h"
#include "pico/platform/sections.h"
#include "pico/stdlib.h"

#include <memory.h>
#include <stdio.h>

// - the "official" specifications: https://www.w3.org/Graphics/JPEG/itu-t81.pdf and https://www.w3.org/Graphics/JPEG/jfif3.pdf
// - Wikipedia has a short description of the JFIF/JPEG file format: https://en.wikipedia.org/wiki/JPEG_File_Interchange_Format
// - the popular STB Image library includes Jon's JPEG encoder as well: https://github.com/nothings/stb/blob/master/stb_image_write.h
// - the most readable JPEG book (from a developer's perspective) is Miano's "Compressed Image File Formats" (1999, ISBN 0-201-60443-4),
//   used copies are really cheap nowadays and include a CD with C++ sources as well (plus great format descriptions of GIF & PNG)
// - much more detailled is Mitchell/Pennebaker's "JPEG: Still Image Data Compression Standard" (1993, ISBN 0-442-01272-1)
//   which contains the official JPEG standard, too - fun fact: I bought a signed copy in a second-hand store without noticing

namespace {  // anonymous namespace to hide local functions / constants / etc.

// ////////////////////////////////////////
// data types
using uint8_t  = unsigned char;
using uint16_t = unsigned short;
using  int16_t =          short;
using  int32_t =          int; // at least four bytes


// ////////////////////////////////////////
// constants

// quantization tables from JPEG Standard, Annex K
const uint8_t DefaultQuantLuminance[8*8] =
    { 16, 11, 10, 16, 24, 40, 51, 61, // there are a few experts proposing slightly more efficient values,
      12, 12, 14, 19, 26, 58, 60, 55, // e.g. https://www.imagemagick.org/discourse-server/viewtopic.php?t=20333
      14, 13, 16, 24, 40, 57, 69, 56, // btw: Google's Guetzli project optimizes the quantization tables per image
      14, 17, 22, 29, 51, 87, 80, 62,
      18, 22, 37, 56, 68,109,103, 77,
      24, 35, 55, 64, 81,104,113, 92,
      49, 64, 78, 87,103,121,120,101,
      72, 92, 95, 98,112,100,103, 99 };

// 8x8 blocks are processed in zig-zag order
// most encoders use a zig-zag "forward" table, I switched to its inverse for performance reasons
// note: ZigZagInv[ZigZag[i]] = i
const uint8_t ZigZagInv[8*8] =
    {  0, 1, 8,16, 9, 2, 3,10,   // ZigZag[] =  0, 1, 5, 6,14,15,27,28,
      17,24,32,25,18,11, 4, 5,   //             2, 4, 7,13,16,26,29,42,
      12,19,26,33,40,48,41,34,   //             3, 8,12,17,25,30,41,43,
      27,20,13, 6, 7,14,21,28,   //             9,11,18,24,31,40,44,53,
      35,42,49,56,57,50,43,36,   //            10,19,23,32,39,45,52,54,
      29,22,15,23,30,37,44,51,   //            20,22,33,38,46,51,55,60,
      58,59,52,45,38,31,39,46,   //            21,34,37,47,50,56,59,61,
      53,60,61,54,47,55,62,63 }; //            35,36,48,49,57,58,62,63

// static Huffman code tables from JPEG standard Annex K
// - CodesPerBitsize tables define how many Huffman codes will have a certain bitsize (plus 1 because there nothing with zero bits),
//   e.g. DcLuminanceCodesPerBitsize[2] = 5 because there are 5 Huffman codes being 2+1=3 bits long
// - Values tables are a list of values ordered by their Huffman code bitsize,
//   e.g. AcLuminanceValues => Huffman(0x01,0x02 and 0x03) will have 2 bits, Huffman(0x00) will have 3 bits, Huffman(0x04,0x11 and 0x05) will have 4 bits, ...

// Huffman definitions for first DC/AC tables (luminance / Y channel)
const uint8_t DcLuminanceCodesPerBitsize[16]   = { 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 };   // sum = 12
const uint8_t DcLuminanceValues         [12]   = { 0,1,2,3,4,5,6,7,8,9,10,11 };         // => 12 codes
const uint8_t AcLuminanceCodesPerBitsize[16]   = { 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125 }; // sum = 162
const uint8_t AcLuminanceValues        [162]   =                                        // => 162 codes
    { 0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xA1,0x08, // 16*10+2 symbols because
      0x23,0x42,0xB1,0xC1,0x15,0x52,0xD1,0xF0,0x24,0x33,0x62,0x72,0x82,0x09,0x0A,0x16,0x17,0x18,0x19,0x1A,0x25,0x26,0x27,0x28, // upper 4 bits can be 0..F
      0x29,0x2A,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59, // while lower 4 bits can be 1..A
      0x5A,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x83,0x84,0x85,0x86,0x87,0x88,0x89, // plus two special codes 0x00 and 0xF0
      0x8A,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6, // order of these symbols was determined empirically by JPEG committee
      0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE1,0xE2,
      0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA };
constexpr int16_t CodeWordLimit = 2048; // +/-2^11, maximum value after DCT
#ifdef FIXED64
using fixed_point = int64_t;
constexpr int64_t INTNEGCHECK = 0xFFFFFFFFFFFFFF00;
constexpr int64_t SCALEDNEGCHECK = 0xFFFFFFFFFFFFFF00;
#else
constexpr unsigned int FIXED_POINT_FRACTIONAL_BITS = 9;
constexpr unsigned int SCALED_POINT_SCALED_FRACTIONAL_BITS = 14;
using fixed_point = int;
constexpr int SCALEDNEGCHECK = 0xFFFFE000;
constexpr int INTNEGCHECK = 0xFFFFFF00;
#endif
// For fx_mul
// constexpr unsigned int FIXED_POINT_FRACTIONAL_BITS = 16;
// constexpr unsigned int SCALED_POINT_SCALED_FRACTIONAL_BITS = 16;
// For int64 fixed
// constexpr unsigned int FIXED_POINT_FRACTIONAL_BITS = 14;
// constexpr unsigned int SCALED_POINT_SCALED_FRACTIONAL_BITS = 19;
// For int32 fixed
constexpr fixed_point float_to_fixed(const float input) {
  return static_cast<fixed_point>(input * static_cast<float>(1 << FIXED_POINT_FRACTIONAL_BITS));
}
constexpr fixed_point scaled_float_to_fixed(const float input) {
  return static_cast<fixed_point>(input * static_cast<float>(1 << SCALED_POINT_SCALED_FRACTIONAL_BITS));
}
inline fixed_point fixed_to_int(fixed_point fixed) {
  return ((fixed & INTNEGCHECK) == INTNEGCHECK) ? 0 : (fixed >> FIXED_POINT_FRACTIONAL_BITS);
}
inline fixed_point fixed_to_int_rounded(fixed_point fixed) {
  return fixed_to_int(fixed + (1 << (FIXED_POINT_FRACTIONAL_BITS - 1)));
}
inline fixed_point fixed_scaled_to_int(fixed_point fixed) {
  return ((fixed & SCALEDNEGCHECK) == SCALEDNEGCHECK) ? 0 : (fixed >> SCALED_POINT_SCALED_FRACTIONAL_BITS);
}
inline int fixed_scaled_to_int_rounded(fixed_point fixed) {
  return fixed_scaled_to_int(fixed + (1 << (SCALED_POINT_SCALED_FRACTIONAL_BITS - 1)));
}

inline fixed_point fx_mul(fixed_point x, fixed_point y) {
  // RELEASE (comment out for debug)
  fixed_point ret = fixed_to_int_rounded(x * y);
  // DEBUG (comment out for release)
  // int64_t result = static_cast<int64_t>(x) * static_cast<int64_t>(y);
  // fixed_point ret = fixed_to_int_rounded(static_cast<fixed_point>(result));
  // result &= 0xFFFFFFFF00000000;
  // if (result != 0xFFFFFFFF00000000 && result != 0) {
  //   printf("!!!!!!!!!!!!!!!! fx_mul:  overflow/underflow result: %lld  return = %d !!!!!!!!\n", result, ret);
  // }
  return ret;
}

inline fixed_point fx_scaled_mul(fixed_point x, fixed_point y) {
  // RELEASE (comment out for debug)
  fixed_point ret = fixed_scaled_to_int_rounded(x * y);
  // DEBUG (comment out for release)
  // int64_t result = static_cast<int64_t>(x) * static_cast<int64_t>(y);
  // fixed_point ret = fixed_scaled_to_int_rounded(static_cast<fixed_point>(result));
  // result &= 0xFFFFFFFF00000000;
  // if (result != 0xFFFFFFFF00000000 && result != 0) {
  //   printf("!!!!!!!!!!!!!!!! fx_scaled_mul:  overflow/underflow result: %lld  return = %d !!!!!!!!\n", result, ret);
  // }
  return ret;
}

too_jpeg::GET_BUFFER g_getFillBuffer;

size_t OUTBUFFER_SIZE;
unsigned char* g_out_buffer;
unsigned char* g_out_buffer_end;
size_t* g_packet_header;
unsigned int g_encoded_image_size;
inline void getFillBuffer(unsigned int partial) {
  if (g_packet_header != nullptr) {
    unsigned int tempVal = (g_out_buffer - reinterpret_cast<unsigned char*>(g_packet_header + 1)) | partial;
    // We have to memcpy packet size into packet header because the RP2040 doesn't support unaligned 32 bit writes.
    memcpy(g_packet_header, &tempVal, 4);
  }
  g_packet_header = reinterpret_cast<unsigned int*>(g_getFillBuffer(g_out_buffer));
  
  if (!g_packet_header) {
    g_out_buffer = g_out_buffer_end = nullptr;
    return;
  }
  g_out_buffer = reinterpret_cast<unsigned char*>(g_packet_header + 1);
  g_out_buffer_end = reinterpret_cast<unsigned char*>(g_packet_header) + OUTBUFFER_SIZE;
}

inline void writeAByte(unsigned char byte) {
  ++g_encoded_image_size;
  if (g_out_buffer == g_out_buffer_end) {
    getFillBuffer(0x80000000);
    if (!g_out_buffer) return;
  }
  *(g_out_buffer++) = byte;
}

fixed_point scaledLuminance[8*8];
uint8_t quantLuminance[8*8];

// restrict a value to the interval [minimum, maximum]
template <typename Number, typename Limit>
Number clamp(Number value, Limit minValue, Limit maxValue) {
  if (value <= minValue) return minValue; // never smaller than the minimum
  if (value >= maxValue) return maxValue; // never bigger  than the maximum
  return value;                           // value was inside interval, keep it
}


void makeQuaintLuminance(unsigned char quality) {
  unsigned short qual = clamp<uint16_t>(quality, 1, 100);
  // convert to an internal JPEG quality factor, formula taken from libjpeg
  qual = qual < 50 ? 5000 / qual : 200 - qual * 2;

  for (auto i = 0; i < 8*8; i++) {
    int luminance = (DefaultQuantLuminance[ZigZagInv[i]] * qual + 50) / 100;
    // clamp to 1..255
    quantLuminance[i] = clamp(luminance, 1, 255);
  }

}

void makeScaledLuminence() {
  for (auto i = 0; i < 8*8; i++) {
    auto row    = ZigZagInv[i] / 8; // same as ZigZagInv[i] >> 3
    auto column = ZigZagInv[i] % 8; // same as ZigZagInv[i] &  7

    // scaling constants for AAN DCT algorithm: AanScaleFactors[0] = 1, AanScaleFactors[k=1..7] = cos(k*PI/16) * sqrt(2)
    constexpr float AanScaleFactors[8] = { 
      1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
      1.0f, 0.785694958f, 0.541196100f, 0.275899379f };
    float factor = 1.0f / (AanScaleFactors[row] * AanScaleFactors[column] * 8.0f);
    scaledLuminance[ZigZagInv[i]] = scaled_float_to_fixed(factor / static_cast<float>(quantLuminance[i]));
  }
}

// forward DCT computation "in one dimension" (fast AAN algorithm by Arai, Agui and Nakajima: "A fast DCT-SQ scheme for images")
void DCT_RAM(fixed_point block[8*8], uint8_t stride) // stride must be 1 (=horizontal) or 8 (=vertical)
// void __not_in_flash_func(__attribute__((noinline)) DCT_RAM)(fixed_point block[8*8], uint8_t stride) // stride must be 1 (=horizontal) or 8 (=vertical)
{
  constexpr fixed_point SqrtHalfSqrt = float_to_fixed(1.306562965f); //    sqrt((2 + sqrt(2)) / 2) = cos(pi * 1 / 8) * sqrt(2)
  constexpr fixed_point InvSqrt      = float_to_fixed(0.707106781f); // 1 / sqrt(2)                = cos(pi * 2 / 8)
  constexpr fixed_point HalfSqrtSqrt = float_to_fixed(0.382683432f); //     sqrt(2 - sqrt(2)) / 2  = cos(pi * 3 / 8)
  constexpr fixed_point InvSqrtSqrt  = float_to_fixed(0.541196100f); // 1 / sqrt(2 - sqrt(2))      = cos(pi * 3 / 8) * sqrt(2)

  // modify in-place
  auto& __restrict block0 = block[0         ];
  auto& __restrict block1 = block[1 * stride];
  auto& __restrict block2 = block[2 * stride];
  auto& __restrict block3 = block[3 * stride];
  auto& __restrict block4 = block[4 * stride];
  auto& __restrict block5 = block[5 * stride];
  auto& __restrict block6 = block[6 * stride];
  auto& __restrict block7 = block[7 * stride];

  // based on https://dev.w3.org/Amaya/libjpeg/jfdctflt.c , the original variable names can be found in my comments
  auto add07 = block0 + block7; auto sub07 = block0 - block7; // tmp0, tmp7
  auto add16 = block1 + block6; auto sub16 = block1 - block6; // tmp1, tmp6
  auto add25 = block2 + block5; auto sub25 = block2 - block5; // tmp2, tmp5
  auto add34 = block3 + block4; auto sub34 = block3 - block4; // tmp3, tmp4

  auto add0347 = add07 + add34; auto sub07_34 = add07 - add34; // tmp10, tmp13 ("even part" / "phase 2")
  auto add1256 = add16 + add25; auto sub16_25 = add16 - add25; // tmp11, tmp12

  block0 = add0347 + add1256; block4 = add0347 - add1256; // "phase 3"

  // auto z1 = fixed_to_int_rounded((sub16_25 + sub07_34) * InvSqrt); // all temporary z-variables kept their original names
  auto z1 = fx_mul((sub16_25 + sub07_34), InvSqrt); // all temporary z-variables kept their original names
  block2 = sub07_34 + z1; block6 = sub07_34 - z1; // "phase 5"

  auto sub23_45 = sub25 + sub34; // tmp10 ("odd part" / "phase 2")
  auto sub12_56 = sub16 + sub25; // tmp11
  auto sub01_67 = sub16 + sub07; // tmp12

  auto z5 = fx_mul((sub23_45 - sub01_67), HalfSqrtSqrt);
  auto z2 = fx_mul(sub23_45, InvSqrtSqrt)  + z5;
  auto z3 = fx_mul(sub12_56, InvSqrt);
  auto z4 = fx_mul(sub01_67, SqrtHalfSqrt) + z5;
  auto z6 = sub07 + z3; // z11 ("phase 5")
  auto z7 = sub07 - z3; // z13
  block1 = z6 + z4; block7 = z6 - z4; // "phase 6"
  block5 = z7 + z2; block3 = z7 - z2;
}

class TooJpeg : public too_jpeg::ITooJpeg {
  unsigned short width_;
  unsigned short height_;
  unsigned char quality_;
  std::vector<unsigned char> precomment_;
  std::vector<unsigned char> postcomment_;

  // represent a single Huffman code
  struct BitCode
  {
    BitCode() = default; // undefined state, must be initialized at a later time
    BitCode(uint16_t code_, uint8_t numBits_)
    : code(code_), numBits(numBits_) {}
    uint16_t code;       // JPEG's Huffman codes are limited to 16 bits
    uint8_t  numBits;    // number of valid bits
  };

  BitCode huffmanLuminanceDC[256];
  BitCode huffmanLuminanceAC[256];
  BitCode  codewordsArray[2 * CodeWordLimit];          // note: quantized[i] is found at codewordsArray[quantized[i] + CodeWordLimit]
  BitCode* codewords = &codewordsArray[CodeWordLimit]; // allow negative indices, so quantized[i] is at codewords[quantized[i]]

  // wrapper for bit output operations
  struct BitWriter
  {
    // user-supplied callback that writes/stores one byte
    // initialize writer
    explicit BitWriter() {}

    // store the most recently encoded bits that are not written yet
    struct BitBuffer
    {
      int32_t data    = 0; // actually only at most 24 bits are used
      uint8_t numBits = 0; // number of valid bits (the right-most bits)
    } buffer;

    // write Huffman bits stored in BitCode, keep excess bits in BitBuffer
    BitWriter& operator<<(const BitCode& data)
    {
      // append the new bits to those bits leftover from previous call(s)
      buffer.numBits += data.numBits;
      buffer.data   <<= data.numBits;
      buffer.data    |= data.code;

      // write all "full" bytes
      while (buffer.numBits >= 8)
      {
        // extract highest 8 bits
        buffer.numBits -= 8;
        auto oneByte = uint8_t(buffer.data >> buffer.numBits);
        writeAByte(oneByte);

        if (oneByte == 0xFF) // 0xFF has a special meaning for JPEGs (it's a block marker)
          writeAByte(0);         // therefore pad a zero to indicate "nope, this one ain't a marker, it's just a coincidence"

        // note: I don't clear those written bits, therefore buffer.bits may contain garbage in the high bits
        //       if you really want to "clean up" (e.g. for debugging purposes) then uncomment the following line
        //buffer.bits &= (1 << buffer.numBits) - 1;
      }
      return *this;
    }

    // write all non-yet-written bits, fill gaps with 1s (that's a strange JPEG thing)
    void flush()
    {
      // at most seven set bits needed to "fill" the last byte: 0x7F = binary 0111 1111
      *this << BitCode(0x7F, 7); // I should set buffer.numBits = 0 but since there are no single bits written after flush() I can safely ignore it
    }

    // NOTE: all the following BitWriter functions IGNORE the BitBuffer and write straight to output !
    // write a single byte
    BitWriter& operator<<(unsigned char oneByte)
    {
      writeAByte(oneByte);
      return *this;
    }

    // write an array of bytes
    template <typename T, int Size>
    BitWriter& operator<<(T (&many_bytes)[Size]) {
      for (const auto& byte : many_bytes)
        writeAByte(byte);
      return *this;
    }

    // write an array of bytes
    BitWriter& operator<<(std::string_view many_bytes) {
      for (const auto& byte : many_bytes)
        writeAByte(static_cast<unsigned char>(byte));
      return *this;
    }

    void addMarker(uint8_t id, uint16_t length)
    {
      writeAByte(0xFF);
      writeAByte(id);     // ID, always preceded by 0xFF
      writeAByte(static_cast<uint8_t>(length >> 8)); // length of the block (big-endian, includes the 2 length bytes as well)
      writeAByte(static_cast<uint8_t>(length & 0xFF));
    }
  };

  // ////////////////////////////////////////
  // functions / templates

  inline unsigned short minimum(unsigned short value, unsigned short maximum) {
    return value <= maximum ? value : maximum;
  }

  // run DCT, quantize and write Huffman bit codes
  int16_t encodeBlock(BitWriter& writer, fixed_point block[8][8], int16_t lastDC) {
    static int counter = 0;
    // "linearize" the 8x8 block, treat it as a flat array of 64 floats
    fixed_point* __restrict block64 = (fixed_point*)block;

    // DCT: rows
    for (auto offset = 0; offset < 8; ++offset)
      DCT_RAM(block64 + offset*8, 1);
    // DCT: columns
    for (auto offset = 0; offset < 8; ++offset)
      DCT_RAM(block64 + offset*1, 8);

    // scale
    for (auto i = 0; i < 8*8; i++)
      block64[i] = fx_scaled_mul(block64[i], scaledLuminance[i]);

    // encode DC (the first coefficient is the "average color" of the 8x8 block)
    short DC = fixed_to_int_rounded(block64[0]);
    // quantize and zigzag the other 63 coefficients
    auto posNonZero = 0; // find last coefficient which is not zero (because trailing zeros are encoded differently)
    int16_t quantized[8*8];
    for (auto i = 1; i < 8*8; i++) // start at 1 because block64[0]=DC was already processed
    {
      auto value = block64[ZigZagInv[i]];
      // round to nearest integer
      quantized[i] = fixed_to_int_rounded(value); // C++11's nearbyint() achieves a similar effect
      // remember offset of last non-zero coefficient
      if (quantized[i] != 0)
        posNonZero = i;
    }

    // same "average color" as previous block ?
    auto diff = DC - lastDC;
    if (diff == 0) {
      writer << huffmanLuminanceDC[0x00];   // yes, write a special short symbol
    } else {
      const BitCode& bits = codewords[diff]; // nope, encode the difference to previous block's average color
      writer << huffmanLuminanceDC[bits.numBits] << bits;
    }

    // encode ACs (quantized[1..63])
    auto offset = 0; // upper 4 bits count the number of consecutive zeros
    for (auto i = 1; i <= posNonZero; i++) {  // quantized[0] was already written, skip all trailing zeros, too
      // zeros are encoded in a special way
      while (quantized[i] == 0) {  // found another zero ?
        offset    += 0x10; // add 1 to the upper 4 bits
        // split into blocks of at most 16 consecutive zeros
        if (offset > 0xF0) {  // remember, the counter is in the upper 4 bits, 0xF = 15
          writer << huffmanLuminanceAC[0xF0]; // 0xF0 is a special code for "16 zeros"
          offset = 0;
        }
        ++i;
      }

      const BitCode& encoded = codewords[quantized[i]];
      // combine number of zeros with the number of bits of the next non-zero value
      writer << huffmanLuminanceAC[offset + encoded.numBits] << encoded; // and the value itself
      offset = 0;
    }

    // send end-of-block code (0x00), only needed if there are trailing zeros
    if (posNonZero < 8*8 - 1) // = 63
      writer << huffmanLuminanceAC[0x00];

    return DC;
  }

  // Jon's code includes the pre-generated Huffman codes
  // I don't like these "magic constants" and compute them on my own :-)
  constexpr void generateHuffmanTable(const uint8_t numCodes[16], const uint8_t* values, BitCode result[256]) {
    // process all bitsizes 1 thru 16, no JPEG Huffman code is allowed to exceed 16 bits
    auto huffmanCode = 0;
    for (auto numBits = 1; numBits <= 16; numBits++) {
      // ... and each code of these bitsizes
      for (auto i = 0; i < numCodes[numBits - 1]; i++) // note: numCodes array starts at zero, but smallest bitsize is 1
        result[*values++] = BitCode(huffmanCode++, numBits);

      // next Huffman code needs to be one bit wider
      huffmanCode <<= 1;
    }
  }

  void addPostCommentMarker(uint8_t id, uint16_t length)
  {
    postcomment_.push_back(0xFF);
    postcomment_.push_back(id);     // ID, always preceded by 0xFF
    postcomment_.push_back(static_cast<uint8_t>(length >> 8)); // length of the block (big-endian, includes the 2 length bytes as well)
    postcomment_.push_back(static_cast<uint8_t>(length & 0xFF));
  }

  // -------------------- externally visible code --------------------

 public:
  TooJpeg(too_jpeg::GET_BUFFER get_buffer_func, size_t buffer_size, unsigned short width, unsigned short height, unsigned char quality)
    : width_(width), height_(height), quality_(quality) {
    g_getFillBuffer = get_buffer_func;
    OUTBUFFER_SIZE = buffer_size;
    g_packet_header = nullptr;
    g_out_buffer = nullptr;
    g_out_buffer_end = nullptr;
    
    makeQuaintLuminance(quality);
    makeScaledLuminence();
    generateHuffmanTable(&DcLuminanceCodesPerBitsize[0], &DcLuminanceValues[0], huffmanLuminanceDC);
    generateHuffmanTable(&AcLuminanceCodesPerBitsize[0], &AcLuminanceValues[0], huffmanLuminanceAC);

    uint8_t numBits = 1; // each codeword has at least one bit (value == 0 is undefined)
    int32_t mask    = 1; // mask is always 2^numBits - 1, initial value 2^1-1 = 2-1 = 1
    for (int16_t value = 1; value < CodeWordLimit; ++value) {
      // numBits = position of highest set bit (ignoring the sign)
      // mask    = (2^numBits) - 1
      if (value > mask) {  // one more bit ?
        numBits++;
        mask = (mask << 1) | 1; // append a set bit
      }
      codewords[-value] = BitCode(mask - value, numBits); // note that I use a negative index => codewords[-value] = codewordsArray[CodeWordLimit  value]
      codewords[+value] = BitCode(       value, numBits);
    }
    // Precompute header part that comes before the comment.
    precomment_ = {
        0xFF,0xD8,         // SOI marker (start of image)
        0xFF,0xE0,         // JFIF APP0 tag
        0,16,              // length: 16 bytes (14 bytes payload + 2 bytes for this length field)
        'J','F','I','F',0, // JFIF identifier, zero-terminated
        1,1,               // JFIF version 1.1
        0,                 // no density units specified
        0,1,0,1,           // density: 1 pixel "per pixel" horizontally and vertically
        0,0 };             // no thumbnail (size 0 x 0)
    // Precompute the header part that comes after the comment
    // write quantization tables
    addPostCommentMarker(0xDB, 67); // length: 65 bytes per table + 2 bytes for this length field
                                  // each table has 64 entries and is preceded by an ID byte

    postcomment_.push_back(0x00);
    postcomment_.insert(postcomment_.end(), quantLuminance, quantLuminance + sizeof(quantLuminance));   // first  quantization table

    // ////////////////////////////////////////
    // write image infos (SOF0 - start of frame)
    addPostCommentMarker(0xC0, 2+6+3); // length: 6 bytes general info + 3 per channel + 2 bytes for this length field

    // 8 bits per channel
    postcomment_.push_back( 0x08);
    postcomment_.push_back( height >> 8);
    postcomment_.push_back( height & 0xFF);
    postcomment_.push_back( width >> 8);
    postcomment_.push_back( width & 0xFF);
    postcomment_.push_back( 1);
    postcomment_.push_back( 1);
    postcomment_.push_back( 0x11);
    postcomment_.push_back( 0);

    // ////////////////////////////////////////
    // Huffman tables
    // DHT marker - define Huffman tables
    addPostCommentMarker(0xC4, 2+208);
                              // 2 bytes for the length field, store chrominance only if needed
                              //   1+16+12  for the DC luminance
                              //   1+16+162 for the AC luminance   (208 = 1+16+12 + 1+16+162)

    // store luminance's DC+AC Huffman table definitions
    postcomment_.push_back(0);
    postcomment_.insert(postcomment_.end(), DcLuminanceCodesPerBitsize, DcLuminanceCodesPerBitsize + sizeof(DcLuminanceCodesPerBitsize));
    postcomment_.insert(postcomment_.end(), DcLuminanceValues, DcLuminanceValues + sizeof(DcLuminanceValues));
    postcomment_.push_back(0x10);
    postcomment_.insert(postcomment_.end(), AcLuminanceCodesPerBitsize, AcLuminanceCodesPerBitsize + sizeof(AcLuminanceCodesPerBitsize));
    postcomment_.insert(postcomment_.end(), AcLuminanceValues, AcLuminanceValues + sizeof(AcLuminanceValues));

    // ////////////////////////////////////////
    // start of scan (there is only a single scan for baseline JPEGs)
    addPostCommentMarker(0xDA, 2+1+2+3); // 2 bytes for the length field, 1 byte for number of components,
                                                      // then 2 bytes for each component and 3 bytes for spectral selection

    // assign Huffman tables to each component
    postcomment_.push_back(1);
    postcomment_.push_back(1);
    postcomment_.push_back(0);

    // constant values for our baseline JPEGs (which have a single sequential scan)
    //static const uint8_t Spectral[3] = { 0, 63, 0 }; // spectral selection: must be from 0 to 63; successive approximation must be 0
    postcomment_.insert(postcomment_.end(), { 0, 63, 0 });
  }
  ~TooJpeg() {}

  // ITooJpeg method
  too_jpeg::WRITE_JPEG_RESULT writeJpeg(const void* pixels_, std::string_view comment) override {
    // reject invalid pointers
    g_encoded_image_size = 0;
    if (pixels_ == nullptr)
      return too_jpeg::WRITE_JPEG_RESULT::WJR_ERROR;

    // wrapper for all output operations
    BitWriter bitWriter;

    for (const auto& byte : precomment_)
      writeAByte(byte);
    // ////////////////////////////////////////
    // comment (optional)
    if (!comment.empty()) {
      // write COM marker
      bitWriter.addMarker(0xFE, 2+comment.size()); // block size is number of bytes (without zero terminator) + 2 bytes for this length field
      // ... and write the comment itself
      bitWriter << comment;
    }

    for (const auto& byte : postcomment_)
      writeAByte(byte);

    // just convert image data from void*
    auto pixels = (const uint8_t*)pixels_;

    // the next two variables are frequently used when checking for image borders
    const unsigned short maxWidth  = width_  - 1; // "last row"
    const unsigned short maxHeight = height_ - 1; // "bottom line"

    // process MCUs (minimum codes units) => image is subdivided into a grid of 8x8 or 16x16 tiles
    constexpr unsigned short mcuSize = 8;

    // average color of the previous MCU
    int16_t lastYDC = 0;
    fixed_point Y[8][8];

    for (unsigned short mcuY = 0; mcuY < height_; mcuY += mcuSize) {  // each step is either 8 or 16 (=mcuSize)
      for (unsigned short mcuX = 0; mcuX < width_; mcuX += mcuSize) {
        // now we finally have an 8x8 block ...
        for (unsigned short y = 0; y < 8; ++y) {
          unsigned int column = mcuX; // must not exceed image borders, replicate last row/column if needed
          unsigned int row    = mcuY + y;
          for (unsigned short x = 0; x < 8; ++x) {
            // find actual pixel position within the current image
            auto pixelPos = row * static_cast<unsigned int>(width_) + column; // the cast ensures that we don't run into multiplication overflows
            if (column < maxWidth) ++column;
            Y[y][x] = (static_cast<int>(pixels[pixelPos]) - 128) << FIXED_POINT_FRACTIONAL_BITS;
          }
        }

        // encode Y channel
        lastYDC = encodeBlock(bitWriter, Y, lastYDC);
      }
    }

    bitWriter.flush(); // now image is completely encoded, write any bits still left in the buffer

    // ///////////////////////////
    // EOI marker
    bitWriter << 0xFF << 0xD9; // this marker has no length, therefore I can't use addMarker()

    printf("TooJpeg::writeJpeg:  Finished encoding.  Encoded image size = %u.\n", g_encoded_image_size);
    // Flush the output buffer if there is not enough room left for the next packet header + 1 at least 1 byte.
    // This prevents the packet header from being splinched.
    if (g_packet_header != nullptr) {
      if (g_out_buffer_end - g_out_buffer < 5) {
        getFillBuffer(0);
      } else {
        size_t tmp_val = g_out_buffer - reinterpret_cast<unsigned char*>(g_packet_header + 1);
        // We have to memcpy packet size into packet header because the RP2040 doesn't support unaligned 32 bit writes.
        memcpy(g_packet_header, &tmp_val, 4);
        g_packet_header = reinterpret_cast<unsigned int*>(g_out_buffer);
        g_out_buffer += 4;
      }
    }
    return (g_packet_header == nullptr) ? too_jpeg::WRITE_JPEG_RESULT::WJR_NO_BUFFER : too_jpeg::WRITE_JPEG_RESULT::WJR_OK;
  } // writeJpeg()
};

}

too_jpeg::ITooJpeg* too_jpeg::getTooJpeg(
    too_jpeg::GET_BUFFER get_buffer_func, size_t buffer_size, unsigned short width, unsigned short height, unsigned char quality) {
  return dynamic_cast<too_jpeg::ITooJpeg*>(new TooJpeg(get_buffer_func, buffer_size, width, height, quality));
}