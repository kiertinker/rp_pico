//
// SPDX-FileCopyrightText: Copyright 2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
//

#ifndef _PICO_HM01B0_H_
#define _PICO_HM01B0_H_

enum class Hm01b0Err {
  HM01B0_ERR_OK,
  HM01B0_ERR_I2C_TIMEOUT,
  HM01B0_ERR_I2C_GENERIC,
  HM01B0_ERR_INVALID_RESOLUTION,
  HM01B0_ERR_INVALID_DATA_BIT_WIDTH,
  HM01B0_ERR_INVALID_MODEL,
  HM01B0_ERR_RESET_FAILED,
  HM01B0_ERR_DMA_FAILURE,
  HM01B0_ERR_FAILED_TO_START,
  HM01B0_ERR_PIN_STATE_CHANGE_TIMEOUT
};

struct Hm01b0Config {
    bool i2c_inst;  // false = i2c0, true = i2c1
    uint sda_pin;
    uint scl_pin;

    uint vsync_pin;
    uint hsync_pin;
    uint pclk_pin;

    uint data_pin_base;
    uint data_bits;
    bool pio;  // false = pio0,  // true = pio1
    uint pio_sm;

    int reset_pin;
    int mclk_pin;
    uint mclk_freq;

    uint width;
    uint height;
};

class Hm01b0Streamer {
 public:
  virtual ~Hm01b0Streamer() {}
  virtual Hm01b0Err hm01b0Init(const Hm01b0Config* config, float fps) = 0;
  virtual void hm01b0Deinit() = 0;
  virtual Hm01b0Err hm01b0ReadFrame(uint8_t* buffer, size_t length, int& dma_channel) = 0;
  virtual Hm01b0Err waitForFrameFinish(int dma_channel) = 0;
  virtual void hm01b0SetCoarseIntegration(unsigned int lines) = 0;
};

Hm01b0Streamer* makeHm01b0Streamer();
#endif