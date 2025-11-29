//
// SPDX-FileCopyrightText: Copyright 2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
//

// Arm Developer Ecosystem HM01B0 Driver

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <string_view>
extern "C" {
#include "cmsis_gcc.h"
}
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

extern "C" {
#include "hardware/i2c.h"
#include "hardware/pio.h"
}

#include "pico/hm01b0.h"

void logError(int line, Hm01b0Err code) {
  printf("ADE HM01B0 encountered error: code(%d), line(%d).\n", static_cast<int>(code), line);
}

#define RETURN_IF_FAIL(x) if (Hm01b0Err err = (x); err != Hm01b0Err::HM01B0_ERR_OK) {logError(__LINE__, x); return x;}

namespace {

#define HM01B0_I2C_ADDRESS 0x24
#define CAPTURE_TIME_TOLERANCE_MS 200 // This allows for 0.1 sec to read a full frame.

constexpr unsigned int LED_PIN = 15;  // FOR_SM_TEST

struct AdeHm01b0Config {
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
    AdeHm01b0Config config;
    i2c_inst_t* i2c;
    PIO pio;
    uint pio_program_offset;
    pio_sm_config pio_sm_cfg;
    uint num_pclk_per_px;
};

static hm01b0 hm01b0_inst;

static Hm01b0Err hm01b0_reset();

static Hm01b0Err hm01b0_read_reg8(uint16_t address, uint8_t& result);
static Hm01b0Err hm01b0_read_reg16(uint16_t address, uint16_t& result);
static Hm01b0Err hm01b0_write_reg8(uint16_t address, uint8_t value);
static Hm01b0Err hm01b0_write_reg16(uint16_t address, uint16_t value);
static absolute_time_t capture_start;

void print_dma_stats(unsigned int channel, std::string_view msg) {
  dma_channel_hw_t* channel_addr = dma_channel_hw_addr(channel);
  printf("%s:  DMA Channel Stats:\n"
    "  Write Address:   %08X\n"
    "  Transfer Count:  %08X\n"
    "  Unoffsetted:     %08X\n"
    "  CTRL_TRIG:       %08X\n",
    msg.data(), channel_addr->write_addr, channel_addr->transfer_count,
      channel_addr->write_addr + 76800 - channel_addr->transfer_count,
      channel_addr->al1_ctrl);
}

class AdeHm01b0Streamer : public Hm01b0Streamer {
 public:
  AdeHm01b0Streamer() {}

  Hm01b0Err hm01b0Init(const Hm01b0Config* config, float fps) override {

    ///~~~~~~ Configuration Setup ~~~~~~~~/
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
    hm01b0_inst.pio = config->pio ? pio1 : pio0;

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

    // UNUSED Our Camera board doesn't have these pins.
    // if (config->reset_pin > -1) {
    //     gpio_init(config->reset_pin);
    //     gpio_set_dir(config->reset_pin, GPIO_OUT);
    //     gpio_put(config->reset_pin, 0);
    //     sleep_ms(100);
    //     gpio_put(config->reset_pin, 1);
    // }

    // if (config->mclk_pin > -1) {
    //     gpio_set_function(config->mclk_pin, GPIO_FUNC_PWM);
    //     uint mclk_slice_num = pwm_gpio_to_slice_num(config->mclk_pin);
    //     uint mclk_channel = pwm_gpio_to_channel(config->mclk_pin);

    //     // PWM @ ~25 MHz, 50% duty cycle
    //     pwm_set_clkdiv(mclk_slice_num, 1.25f);
    //     pwm_set_wrap(mclk_slice_num, 3);
    //     pwm_set_chan_level(mclk_slice_num, mclk_channel, 2);
    //     pwm_set_enabled(mclk_slice_num, true);
    // }

    ///~~~~~~       I2C SETUP     ~~~~~~~~/
    gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(config->sda_pin);
    gpio_pull_up(config->scl_pin);

    i2c_init(hm01b0_inst.i2c, 100 * 1000);

    ///~~~~~~     HM01B0 SETUP    ~~~~~~~~/
    uint16_t model_id;
    RETURN_IF_FAIL(hm01b0_read_reg16(0x0000, model_id));
    if (model_id != 0x01b0) {
      printf("AdeHm01b0Streamer::hm01b0Init: Invalid model id.  Should be 0x01B0, but was %04hX\n", model_id);
      return Hm01b0Err::HM01B0_ERR_INVALID_MODEL;
    }

    if (Hm01b0Err err = hm01b0_reset(); err != Hm01b0Err::HM01B0_ERR_OK) {
        printf("AdeHm01b0Streamer::hm01b0Init: Reset failed!\n");
        return err;
    }

    RETURN_IF_FAIL(hm01b0_write_reg8(0x3059, bit_control_val));

    RETURN_IF_FAIL(hm01b0_write_reg8(0x0383, readout_x_val));
    RETURN_IF_FAIL(hm01b0_write_reg8(0x0387, readout_y_val));
    RETURN_IF_FAIL(hm01b0_write_reg8(0x0390, binning_mode_val));
    RETURN_IF_FAIL(hm01b0_write_reg8(0x3010, qvga_win_en_val));
    // RETURN_IF_FAIL(hm01b0_write_reg8(0x2101, 0x80));  // Auto Exposure mean (default is 0x3C)
    RETURN_IF_FAIL(hm01b0_write_reg16(0x0340, frame_length_lines_val));
    RETURN_IF_FAIL(hm01b0_write_reg16(0x0342, line_length_pclk_val));
    RETURN_IF_FAIL(hm01b0_write_reg16(0x3020, 1));  // Frames to stream before auto sleep
    

    RETURN_IF_FAIL(hm01b0_write_reg8(0x3060, 0x08 | 0x00)); // OSC_CLK_DIV, div by 8
    // RETURN_IF_FAIL(hm01b0_write_reg8(0x3060, 0x08 | 0x01)); // OSC_CLK_DIV, div by 4

    RETURN_IF_FAIL(hm01b0_write_reg16(0x0202, line_length_pclk_val / 2)); // INTEGRATION_H

    RETURN_IF_FAIL(hm01b0_write_reg8(0x0104, 0x01)); // GRP_PARAM_HOLD

    ///~~~~~~     PIO SM SETUP    ~~~~~~~~/
    // FULL HM01B0 DMA STREAM WITH LED FLASH
    unsigned short pio_program_instructions[] = {
        /* 01 */ (unsigned short)pio_encode_pull(false, true),
        /* 03 */ (unsigned short)pio_encode_wait_gpio(false, config->vsync_pin),
        /* 00 */ (unsigned short)pio_encode_set(pio_pins, 1),
        /* 04 */ (unsigned short)pio_encode_wait_gpio(true, config->vsync_pin),
        /* 02 */ (unsigned short)pio_encode_set(pio_pins, 0),
        /* 05 */ (unsigned short)pio_encode_set(pio_y, num_border_px - 1),
        /* 06 */ (unsigned short)pio_encode_wait_gpio(true, config->hsync_pin), // border pixel y
        /* 07 */ (unsigned short)pio_encode_wait_gpio(false, config->hsync_pin),
        /* 08 */ (unsigned short)pio_encode_jmp_y_dec(6),
        /* .wrap_target */
        /* 09 */ (unsigned short)pio_encode_mov(pio_x, pio_osr),
        /* 10 */ (unsigned short)pio_encode_wait_gpio(true, config->hsync_pin),
        /* 11 */ (unsigned short)pio_encode_set(pio_y, num_border_px * hm01b0_inst.num_pclk_per_px - 1),
        /* 12 */ (unsigned short)pio_encode_wait_gpio(true, config->pclk_pin), // border pixel x
        /* 13 */ (unsigned short)pio_encode_wait_gpio(false, config->pclk_pin),
        /* 14 */ (unsigned short)pio_encode_jmp_y_dec(12),
        /* 15 */ (unsigned short)pio_encode_wait_gpio(true, config->pclk_pin),
        /* 16 */ (unsigned short)pio_encode_in(pio_pins, config->data_bits),
        /* 17 */ (unsigned short)pio_encode_wait_gpio(false, config->pclk_pin),
        /* 18 */ (unsigned short)pio_encode_jmp_x_dec(15),
        /* 19 */ (unsigned short)pio_encode_wait_gpio(false, config->hsync_pin),
        /* .wrap */
    };
    unsigned int wrap_offset = 9;

    // FULL HM01B0 DMA STREAM
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
    // unsigned int wrap_offset = 7;

    pio_program_t pio_program;
    pio_program.instructions = pio_program_instructions;
    pio_program.length = sizeof(pio_program_instructions) / sizeof(pio_program_instructions[0]);
    pio_program.origin = -1;
    pio_program.pio_version = 0;

    hm01b0_inst.pio_program_offset = pio_add_program(hm01b0_inst.pio, &pio_program);
    hm01b0_inst.pio_sm_cfg = pio_get_default_sm_config();

    // Comment out FOR_SM_TEST {
    sm_config_set_in_pins(&hm01b0_inst.pio_sm_cfg, config->data_pin_base);
    // sm_config_set_in_shift(&hm01b0_inst.pio_sm_cfg, true, true, 8);  // Original
    sm_config_set_in_shift(&hm01b0_inst.pio_sm_cfg, true, true, 32);
    // }
    sm_config_set_wrap(
        &hm01b0_inst.pio_sm_cfg,
        hm01b0_inst.pio_program_offset + wrap_offset,
        hm01b0_inst.pio_program_offset + pio_program.length - 1
    );

    // Comment out FOR_SM_TEST {
    pio_gpio_init(hm01b0_inst.pio, config->vsync_pin);
    pio_gpio_init(hm01b0_inst.pio, config->hsync_pin);
    pio_gpio_init(hm01b0_inst.pio, config->pclk_pin);
    // } FOR_SM_TEST 
  
    // FOR_SM_TEST {
    pio_gpio_init(hm01b0_inst.pio, LED_PIN);
    // pio_gpio_init(hm01b0_inst.pio, config->vsync_pin);
    sm_config_set_set_pins(&hm01b0_inst.pio_sm_cfg, LED_PIN, 1);
    pio_sm_set_consecutive_pindirs(hm01b0_inst.pio, config->pio_sm, LED_PIN, 1, true);
    // sm_config_set_clkdiv(&hm01b0_inst.pio_sm_cfg, (float)clock_get_hz(clk_sys) / 2000); // FOR STRAIGHTUP BLINKY
    // } FOR_SM_TEST
    return Hm01b0Err::HM01B0_ERR_OK;
  }

  void hm01b0Deinit() override {
    printf("\nAdeHm01b0Streamer::hm01b0Deinit ~~~~~~~~~~~~~~~~~~~~~\n\n");
    AdeHm01b0Config* config = &hm01b0_inst.config;

    i2c_deinit(hm01b0_inst.i2c);

    gpio_set_function(config->sda_pin, GPIO_FUNC_NULL);
    gpio_set_function(config->scl_pin, GPIO_FUNC_NULL);

    if (config->mclk_pin > -1)
      gpio_set_function(config->mclk_pin, GPIO_FUNC_NULL);

    if (config->reset_pin > -1)
      gpio_set_function(config->reset_pin, GPIO_FUNC_NULL);
  }

  Hm01b0Err hm01b0ReadFrame(uint8_t* buffer, size_t length, int& dma_channel) override {
    AdeHm01b0Config* config = &hm01b0_inst.config;

    pio_sm_init(hm01b0_inst.pio, config->pio_sm, hm01b0_inst.pio_program_offset, &hm01b0_inst.pio_sm_cfg);

    ///~~~~~~     DMA SETUP     ~~~~~~~~/
    dma_channel = dma_claim_unused_channel(true);

    dma_channel_config dcc = dma_channel_get_default_config(dma_channel);
    channel_config_set_read_increment(&dcc, false);
    channel_config_set_write_increment(&dcc, true);
    channel_config_set_dreq(&dcc, pio_get_dreq(hm01b0_inst.pio, config->pio_sm, false));
    channel_config_set_transfer_data_size(&dcc, DMA_SIZE_32);
    
    dma_channel_configure(
        dma_channel,
        &dcc,
        buffer,
        ((uint8_t*)&hm01b0_inst.pio->rxf[config->pio_sm]) + 3,
        // length,  // Original
        length / 4,
        false
    );

    dma_channel_start(dma_channel);
    pio_sm_set_enabled(hm01b0_inst.pio, config->pio_sm, true);
    pio_sm_put_blocking(hm01b0_inst.pio, config->pio_sm, config->width * hm01b0_inst.num_pclk_per_px - 1);
    capture_start = get_absolute_time();
    // Start the HM01B0 camera stream.
    RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x03)); // MODE_SELECT Auto sleep after streaming one frame
//    RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x01)); // MODE_SELECT Original

    return Hm01b0Err::HM01B0_ERR_OK;
  }

  Hm01b0Err waitForFrameFinish(int dma_channel) override {
    AdeHm01b0Config* config = &hm01b0_inst.config;
    int i = 0;
    bool exit = true;
    unsigned int millis = 0;
    bool restart_tried = false;
    while (dma_channel_is_busy(dma_channel)) {
      unsigned int time_ms =
          static_cast<unsigned int>(absolute_time_diff_us(capture_start, get_absolute_time()) / 1000);
      if (time_ms > 2 && !restart_tried && dma_channel_hw_addr(dma_channel)->transfer_count == 0x4B00) {
        printf("waitForFrameFinish:  Trying restart!!!\n");
        if (hm01b0_write_reg8(0x0100, 0x03) != Hm01b0Err::HM01B0_ERR_OK)
          printf("waitForFrameFinish:  Restart FAILED!!!!\n");
        restart_tried = true;
      }
        
      if (time_ms > CAPTURE_TIME_TOLERANCE_MS) {
        printf("hm01b0:wait_for_frame_finish:  Aborting!!\n");
        print_dma_stats(dma_channel, "ABORTING!!");
        dma_channel_abort(dma_channel);
        exit = false;
      }
    }

    pio_sm_set_enabled(hm01b0_inst.pio, config->pio_sm, false);
    dma_channel_unclaim(dma_channel); // Comment out FOR_SM_TEST
    // RETURN_IF_FAIL(hm01b0_write_reg8(0x0100, 0x00)); // MODE_SELECT
    return exit ? Hm01b0Err::HM01B0_ERR_OK : Hm01b0Err::HM01B0_ERR_DMA_FAILURE;  // Comment out FOR_SM_TEST
  }

  void hm01b0SetCoarseIntegration(unsigned int lines) override {
    if (lines < 2) lines = 2;
    else if (lines > 0xffff) lines = 0xffff;

    lines -= 2;

    hm01b0_write_reg16(0x0202, lines); // INTEGRATION_H
    hm01b0_write_reg8(0x0104, 0x01); // GRP_PARAM_HOLD
  }

  ~AdeHm01b0Streamer() {
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
  return static_cast<Hm01b0Streamer*>(new AdeHm01b0Streamer());
}