//
// SPDX-FileCopyrightText: Copyright 2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
//

// Arm Developer Ecosystem HM01B0 Driver

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/platform/sections.h"
#include <string_view>
extern "C" {
#include "cmsis_gcc.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
}

//#include "hardware/clocks.h"


#include "pico/hm01b0.h"

int pixel_row_counter = 0;
void logTimeoutError(int line) {
  printf("BIT BANG timed out waiting for pin state.  Row: %d, code line(%d).\n", pixel_row_counter, line);
}

void logError(int line, Hm01b0Err code) {
  printf("ADE HM01B0 encountered error: code(%d), line(%d).\n", static_cast<int>(code), line);
}

#define RETURN_IF_FAIL(x) if (Hm01b0Err err = (x); err != Hm01b0Err::HM01B0_ERR_OK) {logError(__LINE__, x); return x;}
#define RETURN_IF_TIMEOUT_FAIL(x) if (!(x)) {logTimeoutError(__LINE__); return Hm01b0Err::HM01B0_ERR_PIN_STATE_CHANGE_TIMEOUT;}

namespace {

#define HM01B0_I2C_ADDRESS 0x24
#define CAPTURE_TIME_TOLERANCE_MS 100


struct BitBangHm01b0Config {
    uint sda_pin;
    uint scl_pin;

    uint vsync_pin;
    uint hsync_pin;
    uint pclk_pin;

    uint data_pin_base;
    uint data_bits;
    uint pio_sm;

    int reset_pin;
    int mclk_pin;

    uint width;
    uint height;
};

struct hm01b0 {
    BitBangHm01b0Config config;
    i2c_inst_t* i2c;
    uint pio_program_offset;
    uint num_pclk_per_px;
};


static hm01b0 hm01b0_inst;

static Hm01b0Err hm01b0_reset();

static Hm01b0Err hm01b0_read_reg8(uint16_t address, uint8_t& result);
static Hm01b0Err hm01b0_read_reg16(uint16_t address, uint16_t& result);
static Hm01b0Err hm01b0_write_reg8(uint16_t address, uint8_t value);
static Hm01b0Err hm01b0_write_reg16(uint16_t address, uint16_t value);
static absolute_time_t capture_start;

Hm01b0Err __not_in_flash_func(bitBanger)(uint8_t* buffer, size_t length, BitBangHm01b0Config* config) {
  constexpr int COUNT = 100;
  unsigned int hsync_false_counts[COUNT], hsync_true_counts[COUNT];
  
  RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x01)); // MODE_SELECT

  // Now do quick wait for the vsync pin to go true;
  unsigned int timeout = 1000000UL; while (!gpio_get(config->vsync_pin) && --timeout);
  RETURN_IF_TIMEOUT_FAIL(timeout);

  for (int i=0; i<COUNT; ++i) {
    timeout = 0;  while (!gpio_get(config->hsync_pin)) ++timeout;
    hsync_false_counts[i] = timeout;
    timeout = 0;  while (gpio_get(config->hsync_pin)) ++timeout;
    hsync_true_counts[i] = timeout;
  }

  // clock cycle timing
  uint64_t timing_start = get_absolute_time();
  for (int i = 1000; i; --i) {
    while (!gpio_get(config->pclk_pin));
    while (gpio_get(config->pclk_pin));
  }
  uint64_t timing_end = get_absolute_time();

  RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x00)); // MODE_SELECT

  printf("\nHSYNC FALSE TRUE COUNTS\n");
  for (int i=0; i<COUNT; ++i)
    printf("HSYNC %03d: %u, %u\n", i, hsync_false_counts[i], hsync_true_counts[i]);
  printf ("\n");

  printf("Timing for 1000 clock cycles = %lld\n", absolute_time_diff_us(timing_start, timing_end));

  // Timing test
  timing_start = get_absolute_time();
  timeout = 751200UL; while (gpio_get(config->vsync_pin) && --timeout);
  timing_end = get_absolute_time();
  printf("bitBangerTest iteration %u timing: %lld\n\n", 751200UL - timeout, absolute_time_diff_us(timing_start, timing_end));
  return Hm01b0Err::HM01B0_ERR_FAILED_TO_START;
}

// Hm01b0Err __not_in_flash_func(bitBanger)(uint8_t* buffer, size_t length, BitBangHm01b0Config* config) {
//   //memset(buffer, 0, length);
//   size_t pixel_index = 0;
//   int border_pixel_bit = 0;
//   int pixel_column_counter = 0;
//   int column_pixel_bit = 0;
//   pixel_row_counter = config->height;
//   int width = config->width;
//   int timeout = 10000000UL;  // Timeout tracker for pin state change waiting.
//   RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x01)); // MODE_SELECT
//   // Now do quick wait for the vsync pin to go true;
//   timeout = 1000000UL; while (!gpio_get(config->vsync_pin) && --timeout);
//   RETURN_IF_TIMEOUT_FAIL(timeout);
//   // Now that the camera is feeding pixel bits, we eat the first two scan lines.
//   // We wait for the hsync pin to go true (i.e. scan line started), then false (scan line finished).
//   timeout = 10000UL; while (!gpio_get(config->hsync_pin) && --timeout);
//   RETURN_IF_TIMEOUT_FAIL(timeout);
//   timeout = 10000UL; while (gpio_get(config->hsync_pin) && --timeout);
//   RETURN_IF_TIMEOUT_FAIL(timeout);
//   timeout = 10000UL; while (!gpio_get(config->hsync_pin) && --timeout);
//   RETURN_IF_TIMEOUT_FAIL(timeout);
//   timeout = 10000UL; while (gpio_get(config->hsync_pin) && --timeout);
//   RETURN_IF_TIMEOUT_FAIL(timeout);
//   // Now we start reading pixel bits for real.
//   // Loop through the scan line reading for as many rows as are in the image
//   do {
//     border_pixel_bit = 16;
//     pixel_column_counter = width;
//     // For each row we wait for the hsync pin to be true, read the row,
//     // then wait for the hsync pin to go false.
//     timeout = 10000UL; while (!gpio_get(config->hsync_pin) && --timeout);
//     RETURN_IF_TIMEOUT_FAIL(timeout);
//     // For each pixel bit, we wait for the hm01b0 clock pin to go true, read a bit, then go false.
//     // First two pixels are border pixels and don't count (just wait for clock transitions)
//     do {
//       timeout = 10UL; while (!gpio_get(config->pclk_pin) && --timeout);
//       RETURN_IF_TIMEOUT_FAIL(timeout);
//       timeout = 10UL; while (gpio_get(config->pclk_pin) && --timeout);
//       RETURN_IF_TIMEOUT_FAIL(timeout);
//     } while (--border_pixel_bit);
//     do {
//       column_pixel_bit = 8;
//       unsigned char& column_pixel = buffer[pixel_index];
//       ++pixel_index;
//       do {
//         // timeout = 10UL; while (!gpio_get(config->pclk_pin) && --timeout);
//         // RETURN_IF_TIMEOUT_FAIL(timeout);
//         while (!gpio_get(config->pclk_pin));
//         column_pixel = (column_pixel << 1) | (gpio_get(config->data_pin_base) ? 0x01 : 0x00);
//         // timeout = 10UL; while (gpio_get(config->pclk_pin) && --timeout);
//         // RETURN_IF_TIMEOUT_FAIL(timeout);
//         while (gpio_get(config->pclk_pin));
//       } while (--column_pixel_bit);
//     } while (--pixel_column_counter);
//   } while (--pixel_row_counter);
//   return Hm01b0Err::HM01B0_ERR_OK;
// }


class BitBangHm01b0Streamer : public Hm01b0Streamer {
 public:
  BitBangHm01b0Streamer() {}

  Hm01b0Err hm01b0Init(const Hm01b0Config* config, float fps) override {
    hm01b0_inst.config = {
      .sda_pin = config->sda_pin,
      .scl_pin = config->scl_pin,
      .vsync_pin = config->vsync_pin,
      .hsync_pin = config->hsync_pin,
      .pclk_pin = config->pclk_pin,
      .data_pin_base = config->data_pin_base,
      .data_bits = config->data_bits,
      .pio_sm = config->pio_sm,
      .reset_pin = config->reset_pin,
      .mclk_pin = config->mclk_pin,
      .width = config->width,
      .height = config->height
    };
    hm01b0_inst.i2c = config->i2c_inst ? i2c1 : i2c0;

    uint8_t readout_x_val;           // 0x0383
    uint8_t readout_y_val;           // 0x0387
    uint8_t binning_mode_val;        // 0x0390
    uint8_t qvga_win_en_val;         // 0x3010
    uint16_t frame_length_lines_val; // 0x0340
    uint16_t line_length_pclk_val;   // 0x0342
    uint8_t bit_control_val;         // 0x3059

    uint8_t num_border_px;

    if (config->width == 320 && config->height == 320) {
        readout_x_val          = 0x01;
        readout_y_val          = 0x01;
        binning_mode_val       = 0x00;
        qvga_win_en_val        = 0x00;
        frame_length_lines_val = 0x0158;
        line_length_pclk_val   = 0x0178;

        num_border_px = 2;
    } else if (config->width == 320 && config->height == 240) {
        readout_x_val          = 0x01;
        readout_y_val          = 0x01;
        binning_mode_val       = 0x00;
        qvga_win_en_val        = 0x01;
        frame_length_lines_val = 0x0104;
        line_length_pclk_val   = 0x0178;

        num_border_px = 2;
    } else if (config->width == 160 && config->height == 120) {
        readout_x_val          = 0x03;
        readout_y_val          = 0x03;
        binning_mode_val       = 0x03;
        qvga_win_en_val        = 0x01;
        frame_length_lines_val = 0x0080;
        line_length_pclk_val   = 0x00D7;

        num_border_px = 2;
    } else {
        printf("Invalid resolution!\n");
        return Hm01b0Err::HM01B0_ERR_INVALID_RESOLUTION;
    }

    if (config->data_bits == 8) {
        bit_control_val = 0x02;
        hm01b0_inst.num_pclk_per_px = 1;
    } else if (config->data_bits == 4) {
        bit_control_val = 0x42;
        hm01b0_inst.num_pclk_per_px = 2;
    } else if (config->data_bits == 1) {
        bit_control_val = 0x22;
        hm01b0_inst.num_pclk_per_px = 8;
    } else {
        printf("Invalid data bits!\n");
        return Hm01b0Err::HM01B0_ERR_INVALID_DATA_BIT_WIDTH;
    }

    gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(config->sda_pin);
    gpio_pull_up(config->scl_pin);

    i2c_init(hm01b0_inst.i2c, 100 * 1000);

    uint16_t model_id;
    RETURN_IF_FAIL(hm01b0_read_reg16(0x0000, model_id));
    if (model_id != 0x01b0) {
      printf("BitBangHm01b0Streamer::hm01b0Init: Invalid model id.  Should be 0x01B0, but was %04hX\n", model_id);
      return Hm01b0Err::HM01B0_ERR_INVALID_MODEL;
    }

    if (Hm01b0Err err = hm01b0_reset(); err != Hm01b0Err::HM01B0_ERR_OK) {
        printf("BitBangHm01b0Streamer::hm01b0Init: Reset failed!\n");
        return err;
    }

    RETURN_IF_FAIL(hm01b0_write_reg8(0x3059, bit_control_val));

    RETURN_IF_FAIL(hm01b0_write_reg8(0x0383, readout_x_val));
    RETURN_IF_FAIL(hm01b0_write_reg8(0x0387, readout_y_val));
    RETURN_IF_FAIL(hm01b0_write_reg8(0x0390, binning_mode_val));
    RETURN_IF_FAIL(hm01b0_write_reg8(0x3010, qvga_win_en_val));
    RETURN_IF_FAIL(hm01b0_write_reg16(0x0340, frame_length_lines_val));
    RETURN_IF_FAIL(hm01b0_write_reg16(0x0342, line_length_pclk_val));
    

    //RETURN_IF_FAIL(hm01b0_write_reg8(0x3060, 0x08 | 0)); // OSC_CLK_DIV - div by 8(Original)
    RETURN_IF_FAIL(hm01b0_write_reg8(0x3060, 0x08 | 2)); // OSC_CLK_DIV - div by 2

    RETURN_IF_FAIL(hm01b0_write_reg16(0x0202, line_length_pclk_val / 2)); // INTEGRATION_H


    RETURN_IF_FAIL(hm01b0_write_reg8(0x0104, 0x01)); // GRP_PARAM_HOLD
    gpio_init(config->vsync_pin);
    gpio_init(config->hsync_pin);
    gpio_init(config->pclk_pin);
    gpio_init(config->data_pin_base);
    gpio_set_dir(config->vsync_pin, GPIO_IN);
    gpio_set_dir(config->hsync_pin, GPIO_IN);
    gpio_set_dir(config->pclk_pin, GPIO_IN);
    gpio_set_dir(config->data_pin_base, GPIO_IN);

    // pio_program_t pio_program;
    // unsigned short pio_program_instructions[] = {
    //     /* 00 */ (unsigned short)pio_encode_pull(false, true),
    //     /* 01 */ (unsigned short)pio_encode_wait_gpio(false, config->vsync_pin),
    //     /* 02 */ (unsigned short)pio_encode_wait_gpio(true, config->vsync_pin),
    //     /* 03 */ (unsigned short)pio_encode_set(pio_y, num_border_px - 1),
    //     /* 04 */ (unsigned short)pio_encode_wait_gpio(true, config->hsync_pin), // border pixel y
    //     /* 05 */ (unsigned short)pio_encode_wait_gpio(false, config->hsync_pin),
    //     /* 06 */ (unsigned short)pio_encode_jmp_y_dec(4),
    //     /* .wrap_target */
    //     /* 07 */ (unsigned short)pio_encode_mov(pio_x, pio_osr),
    //     /* 08 */ (unsigned short)pio_encode_wait_gpio(true, config->hsync_pin),
    //     /* 09 */ (unsigned short)pio_encode_set(pio_y, num_border_px * hm01b0_inst.num_pclk_per_px - 1),
    //     /* 10 */ (unsigned short)pio_encode_wait_gpio(true, config->pclk_pin), // border pixel x
    //     /* 11 */ (unsigned short)pio_encode_wait_gpio(false, config->pclk_pin),
    //     /* 12 */ (unsigned short)pio_encode_jmp_y_dec(10),
    //     /* 13 */ (unsigned short)pio_encode_wait_gpio(true, config->pclk_pin),
    //     /* 14 */ (unsigned short)pio_encode_in(pio_pins, config->data_bits),
    //     /* 15 */ (unsigned short)pio_encode_wait_gpio(false, config->pclk_pin),
    //     /* 16 */ (unsigned short)pio_encode_jmp_x_dec(13),
    //     /* 17 */ (unsigned short)pio_encode_wait_gpio(false, config->hsync_pin),
    //     /* .wrap */
    // };

    return Hm01b0Err::HM01B0_ERR_OK;
  }

  void hm01b0Deinit() override {
    BitBangHm01b0Config* config = &hm01b0_inst.config;

    i2c_deinit(hm01b0_inst.i2c);

    gpio_set_function(config->sda_pin, GPIO_FUNC_NULL);
    gpio_set_function(config->scl_pin, GPIO_FUNC_NULL);
  }

  Hm01b0Err hm01b0ReadFrame(uint8_t* buffer, size_t length, int& dma_channel) override {
    capture_start = get_absolute_time();
    //Hm01b0Err err = bitBangerTest(buffer, length, &hm01b0_inst.config);
    Hm01b0Err err = bitBanger(buffer, length, &hm01b0_inst.config);
    RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x00)); // MODE_SELECT
    return err;
  }

  Hm01b0Err waitForFrameFinish(int dma_channel) override {
    // Nothing to do here, since bit banging is synchronous.
    return Hm01b0Err::HM01B0_ERR_OK;
  }

  void hm01b0SetCoarseIntegration(unsigned int lines) override {
    if (lines < 2) {
        lines = 2;
    } else if (lines > 0xffff) {
        lines = 0xffff;
    }

    lines -= 2;

    hm01b0_write_reg16(0x0202, lines); // INTEGRATION_H

    hm01b0_write_reg8(0x0104, 0x01); // GRP_PARAM_HOLD
  }

  ~BitBangHm01b0Streamer() {
    hm01b0Deinit();
  }
};

static Hm01b0Err hm01b0_reset() {
  RETURN_IF_FAIL(hm01b0_write_reg8(0x0103, 0x01));

  uint8_t reg;
  for (int retries = 0; retries < 10; retries++) {
    RETURN_IF_FAIL(hm01b0_read_reg8(0x0100, reg)); 
    if (reg == 0)
      return Hm01b0Err::HM01B0_ERR_OK;
    sleep_ms(100);
  }
  return Hm01b0Err::HM01B0_ERR_RESET_FAILED;
}

static Hm01b0Err hm01b0_read_reg8(uint16_t address, uint8_t& result) {
  address = __REV16(address);

  result = 0xff;

  if (i2c_write_blocking(hm01b0_inst.i2c, HM01B0_I2C_ADDRESS, (const uint8_t*)&address, sizeof(address), false) == PICO_ERROR_GENERIC)
    return Hm01b0Err::HM01B0_ERR_I2C_GENERIC;
  if (i2c_read_blocking(hm01b0_inst.i2c, HM01B0_I2C_ADDRESS, (uint8_t*)&result, sizeof(result), false) == PICO_ERROR_GENERIC)
    return Hm01b0Err::HM01B0_ERR_I2C_GENERIC;

  return Hm01b0Err::HM01B0_ERR_OK;
}

static Hm01b0Err hm01b0_read_reg16(uint16_t address, uint16_t& result) {
  address = __REV16(address);

  result = 0xffff;

  if (i2c_write_blocking(hm01b0_inst.i2c, HM01B0_I2C_ADDRESS, (const uint8_t*)&address, sizeof(address), false) == PICO_ERROR_GENERIC)
    return Hm01b0Err::HM01B0_ERR_I2C_GENERIC;
  if (i2c_read_blocking(hm01b0_inst.i2c, HM01B0_I2C_ADDRESS, (uint8_t*)&result, sizeof(result), false) == PICO_ERROR_GENERIC)
    return Hm01b0Err::HM01B0_ERR_I2C_GENERIC;
  result = __REV16(result);

  return Hm01b0Err::HM01B0_ERR_OK;
}


static Hm01b0Err hm01b0_write_reg8(uint16_t address, uint8_t value) {
  uint8_t data[3];

  *((uint16_t*)data) = __REV16(address);
  data[2] = value;

  switch (i2c_write_blocking(hm01b0_inst.i2c, HM01B0_I2C_ADDRESS, data, sizeof(data), false)) {
  case PICO_ERROR_GENERIC:
    return Hm01b0Err::HM01B0_ERR_I2C_GENERIC;
  case PICO_ERROR_TIMEOUT:
    return Hm01b0Err::HM01B0_ERR_I2C_TIMEOUT;
  }
  return Hm01b0Err::HM01B0_ERR_OK;
}

static Hm01b0Err hm01b0_write_reg16(uint16_t address, uint16_t value) {
  uint8_t data[4];

  *((uint16_t*)data + 0) = __REV16(address);
  *((uint16_t*)data + 1) = __REV16(value);

  switch (i2c_write_blocking(hm01b0_inst.i2c, HM01B0_I2C_ADDRESS, data, sizeof(data), false)) {
  case PICO_ERROR_GENERIC:
    return Hm01b0Err::HM01B0_ERR_I2C_GENERIC;
  case PICO_ERROR_TIMEOUT:
    return Hm01b0Err::HM01B0_ERR_I2C_TIMEOUT;
  }
  return Hm01b0Err::HM01B0_ERR_OK;
}

}  // anonymous namespace

Hm01b0Streamer* makeHm01b0Streamer() {
  return static_cast<Hm01b0Streamer*>(new BitBangHm01b0Streamer());
}