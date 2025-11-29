#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

#include "pico/hm01b0.h"

namespace {

absolute_time_t capture_start;
const int PAUSE_MILLISECONDS = 60;
static uint8_t* g_buffer;
static size_t g_length;

void print_dma_stats(unsigned int channel, char* msg) {
  printf("%s, - This is a mock library.  No meaningfull stats to report:\n", msg);
}

class MockHm01b0Streamer : public Hm01b0Streamer {
 public:
  MockHm01b0Streamer() {};
  ~MockHm01b0Streamer() {};
  Hm01b0Err hm01b0Init(const struct Hm01b0Config* config, float fps) override { return Hm01b0Err::HM01B0_ERR_OK; }

  void hm01b0Deinit() {}

  Hm01b0Err hm01b0ReadFrame(uint8_t* buffer, size_t length, int& dma_channel) override {
    dma_channel = 1;
    g_buffer = buffer;
    g_length = length;
    capture_start = get_absolute_time();
    return Hm01b0Err::HM01B0_ERR_OK;
  }

  Hm01b0Err waitForFrameFinish(int dma_channel) override {
    for (size_t i = 0; i < g_length; ++i)
      g_buffer[i] = (uint8_t)(i & 0xFF);
    int wait = (int)(absolute_time_diff_us(capture_start, get_absolute_time()) / 1000);
    if (wait < PAUSE_MILLISECONDS)
      sleep_ms(PAUSE_MILLISECONDS - wait);
    return Hm01b0Err::HM01B0_ERR_OK;
  }

  void hm01b0SetCoarseIntegration(unsigned int lines) override {}
};
}  // anonymous namespace

Hm01b0Streamer* makeHm01b0Streamer() {
  return static_cast<Hm01b0Streamer*>(new MockHm01b0Streamer());
}