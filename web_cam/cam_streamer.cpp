#include <memory.h>
#include <stdio.h>
#include "cam_streamer.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/platform/sections.h"
#include "pico/stdlib.h"
#include "too_jpeg/too_jpeg.h"

#include "pico/hm01b0.h"
// #include "hardware/i2c.h"
// #include "hardware/pio.h"

#include <functional>
#include <memory>
#include <iostream>
#include <bitset>

namespace {

// Global constants
constexpr unsigned char QUALITY = 80;  // Default

// Core 2 Injection routine definition
std::function<void()> globalLambda;

// Global variables
int capture_counter = 0;
int encoder_counter = 0;
FrameBufferManager* g_pframe_buffer_manager = nullptr;
FrameBuffer* g_encoding_frame = nullptr;
// unsigned char* g_pjpeg_bytes_pointer;
// unsigned char* g_pjpeg_bytes_start;
// unsigned char* g_pjpeg_bytes_end;
size_t g_encoded_frame_size;

Hm01b0Config cam_config = {
    .i2c_inst      = false,
    .sda_pin       = PICO_DEFAULT_I2C_SDA_PIN,
    .scl_pin       = PICO_DEFAULT_I2C_SCL_PIN,

    .vsync_pin     = 6,
    .hsync_pin     = 7,
    .pclk_pin      = 8,
    .data_pin_base = 9,
    .data_bits     = 1,
    .pio           = false,
    .pio_sm        = 0,
    .reset_pin     = -1,   // Not connected
    .mclk_pin      = -1,   // Not connected
    .mclk_freq     = 25000000,

    .width         = 0,
    .height        = 0,
};

unsigned char* GET_BUFFER(unsigned char* old_buffer_end) {
  if (g_encoding_frame) {
    g_encoding_frame->setBytesSet(old_buffer_end - g_encoding_frame->getBufferBytes());
    g_pframe_buffer_manager->readyForSend(g_encoding_frame);
  }
  absolute_time_t fill_buffer_start = get_absolute_time();
  g_encoding_frame = g_pframe_buffer_manager->getFillBuffer();
  return g_encoding_frame ? g_encoding_frame->getBufferBytes() : nullptr;
}
// // Global routines
// inline void finalizeAndSendFrameBuffer(bool final_send) {
//   if (g_encoding_frame == nullptr) return;
//   unsigned int& buffer_bytes = reinterpret_cast<unsigned int*>(g_pjpeg_bytes_start)[0];
//   buffer_bytes = g_pjpeg_bytes_pointer - g_pjpeg_bytes_start - 4;
//   if (!final_send)
//     buffer_bytes |= 0x80000000;
//   g_encoding_frame->setBytesSet(g_pjpeg_bytes_pointer - g_pjpeg_bytes_start);
//   g_pframe_buffer_manager->readyForSend(g_encoding_frame);
//   g_encoding_frame = nullptr;
// }

// inline bool getAndSetupFrameBuffer() {
//   absolute_time_t fill_buffer_start = get_absolute_time();
//   g_encoding_frame = g_pframe_buffer_manager->getFillBuffer();
//   printf("FILL BUFFER LATENCY:  Time diff for fill buffer latency: %lld\n",
//       capture_counter, absolute_time_diff_us(fill_buffer_start, get_absolute_time()) / 1000);
//   if (g_encoding_frame == nullptr) {
//     g_pjpeg_bytes_end = g_pjpeg_bytes_pointer = g_pjpeg_bytes_start = nullptr;
//     return false;
//   }
//   g_pjpeg_bytes_start = g_encoding_frame->getBufferBytes();
//   g_pjpeg_bytes_pointer = g_pjpeg_bytes_start + 4;  // First 4 bytes are for jpeg data packet size
//   g_pjpeg_bytes_end = g_pjpeg_bytes_start + g_encoding_frame->getBufferSize();
//   return true;
// }


// void __not_in_flash_func(CAPTURE_BYTE)(const unsigned char* bytes, size_t size) {
//   const unsigned char* bytes_end = bytes + size;
//   g_encoded_frame_size += size;
//   do {
//     size_t copyable_bytes = g_pjpeg_bytes_end - g_pjpeg_bytes_pointer;
//     if (copyable_bytes >=  size) {
//       memcpy(g_pjpeg_bytes_pointer, bytes, size);
//       g_pjpeg_bytes_pointer += size;
//       return;
//     }
//     memcpy(g_pjpeg_bytes_pointer, bytes, copyable_bytes);
//     bytes += copyable_bytes;
//     size -= copyable_bytes;
//     g_pjpeg_bytes_pointer += copyable_bytes;
//     finalizeAndSendFrameBuffer(false);
//   } while (getAndSetupFrameBuffer() && bytes_end > bytes);
// }

class CamStreamer : public ICamStreamer {
  FrameBufferManager& frame_buffer_manager_;
  FrameBuffer& capture_frame1_;
  FrameBuffer& capture_frame2_;
  unsigned int width_;
  unsigned int height_;
  std::unique_ptr<Hm01b0Streamer> hm01b0_streamer;
  std::unique_ptr<too_jpeg::ITooJpeg> too_jpg_;
  mutex exit_;
  bool capturing_frame1_;
  int dma_channel_;

 public:
  CamStreamer(
      FrameBufferManager& frame_buffer_manager, FrameBuffer& capture_frame1, FrameBuffer& capture_frame2, unsigned short width, unsigned short height, Hm01b0Streamer* hm01b0_streamer) :
      frame_buffer_manager_(frame_buffer_manager), capture_frame1_(capture_frame1), capture_frame2_(capture_frame2),
      width_(width), height_(height),
      hm01b0_streamer(hm01b0_streamer),
      capturing_frame1_(true) {
    g_pframe_buffer_manager = &frame_buffer_manager_;
    too_jpg_.reset(too_jpeg::getTooJpeg(GET_BUFFER, frame_buffer_manager_.getFrameBufferSize(),  width, height, QUALITY));
    printf("Capture Buffer 1 Addr:  %08X\n", capture_frame1_.getBufferBytes());
    printf("Capture Buffer 2 Addr:  %08X\n", capture_frame2_.getBufferBytes());
    mutex_init(&exit_);
  }

  virtual ~CamStreamer() {
    frame_buffer_manager_.signalExit();
    mutex_enter_blocking(&exit_);
  }

  void streamFrames() {
    mutex_enter_blocking(&exit_);

    absolute_time_t capture_latency_start, capture_latency_end, encode_time_start, cycle_start;
    unsigned int frame_size = width_ * height_;
    capture_latency_start = get_absolute_time();
    Hm01b0Err err = hm01b0_streamer->hm01b0ReadFrame(capture_frame1_.getBufferBytes(), frame_size, dma_channel_);

    while(err == Hm01b0Err::HM01B0_ERR_OK) {
      cycle_start = get_absolute_time();
      if (err = hm01b0_streamer->waitForFrameFinish(dma_channel_); err != Hm01b0Err::HM01B0_ERR_OK) {
        capture_latency_end = get_absolute_time();
        printf("Hm01b0Streamer::waitForFrameFinish failed at %lld msec.\n",
            absolute_time_diff_us(capture_latency_start, capture_latency_end) / 1000);
        break;
      }
      capture_latency_end = get_absolute_time();
      printf("CAPTURE LATENCY:  Time diff for capture latency iteration %d: %lld\n",
          capture_counter++, absolute_time_diff_us(capture_latency_start, capture_latency_end) / 1000);

      capture_latency_start = get_absolute_time();
      capturing_frame1_ = !capturing_frame1_;
      if (err = hm01b0_streamer->hm01b0ReadFrame(
          capturing_frame1_ ? capture_frame1_.getBufferBytes() : capture_frame2_.getBufferBytes(),
          frame_size, dma_channel_);
          err != Hm01b0Err::HM01B0_ERR_OK)  {
        printf("Hm01b0Streamer::hm01b0ReadFrame failed.\n");
        break;
      }
      capture_latency_end = get_absolute_time();
      printf("CamStreamer::streamFrames:  Captured Frame\n");
      encode_time_start = get_absolute_time();
      g_encoded_frame_size = 0;
      if (too_jpeg::WRITE_JPEG_RESULT result = too_jpg_->writeJpeg(
          capturing_frame1_ ? capture_frame2_.getBufferBytes() : capture_frame1_.getBufferBytes());
          result != too_jpeg::WRITE_JPEG_RESULT::WJR_OK) {
        if (result == too_jpeg::WRITE_JPEG_RESULT::WJR_NO_BUFFER) {
          printf("CamStreamer::streamFrames: Iterations have been exhausted.\n");
          hm01b0_streamer->hm01b0Deinit();
          mutex_exit(&exit_);
          return;
        } else {  // In the event of an error: log, wait a couple tenths of a second and retry.
          printf("FAILURE: TooJpeg::writeJpeg FAILED!!!\n");
          sleep_ms(200);
          // Put the frame buffer back in the fill queue so at can get reused.
          frame_buffer_manager_.readyForFill(g_encoding_frame);
          continue;
        }
      }
      printf("ENCODING:  Time diff for encoding iteration %d: %lld, encoded size %u, copied bytes %u\n",
          encoder_counter++, absolute_time_diff_us(encode_time_start, get_absolute_time()) / 1000, g_encoded_frame_size);
      printf("CYCLE:  Total cycle time for iteration %d: %lld\n",
          encoder_counter - 1, absolute_time_diff_us(cycle_start, get_absolute_time()) / 1000);
    }
    if (err != Hm01b0Err::HM01B0_ERR_OK)
      printf("CamStreamer::streamFrames streaming loop exited on iteration %d with failure with code: %d.\n",
          capture_counter, static_cast<int>(err));
    hm01b0_streamer->hm01b0Deinit();
    mutex_exit(&exit_);
  }

  static void globalLambdaWrapper() {
    printf("In CamStreamer::globalLambdaWrapper...\n");
    globalLambda();
  };
  
};

}  // anonymous namspace

ICamStreamer* startCamStream(
    FrameBufferManager& frame_buffer_manager, FrameBuffer& capture_frame1, FrameBuffer& capture_frame2, unsigned int width, unsigned int height) {
  cam_config.width = width;
  cam_config.height = height;

  std::unique_ptr<Hm01b0Streamer> hm01b0_streamer(makeHm01b0Streamer());
  if (Hm01b0Err err = hm01b0_streamer->hm01b0Init(&cam_config, 25); err != Hm01b0Err::HM01B0_ERR_OK) {
    printf("hm01b0_init FAILED with code %d.\n", static_cast<int>(err));
    return nullptr;
  }
  printf("hm01b0_init SUCCEEDED!!!!\n");
  CamStreamer* cam_streamer = new CamStreamer(
      frame_buffer_manager, capture_frame1, capture_frame2, static_cast<unsigned short>(width), static_cast<unsigned short>(height), hm01b0_streamer.release());

  globalLambda = [cam_streamer]() {
    printf("Starting Cam Streaming!!!\n");
    cam_streamer->streamFrames();
  };
  multicore_launch_core1(CamStreamer::globalLambdaWrapper);
  return dynamic_cast<ICamStreamer*>(cam_streamer);
}