#include "too_jpeg/too_jpeg.h"
#include "pico/stdlib.h"


namespace {

class MockTooJpeg : public too_jpeg::ITooJpeg {
  too_jpeg::GET_BUFFER output_;
  unsigned char quality_;
 public:
  MockTooJpeg(too_jpeg::GET_BUFFER output, unsigned char quality) : output_(output), quality_(quality) {}
  virtual ~MockTooJpeg() {}
  too_jpeg::WRITE_JPEG_RESULT writeJpeg(const void* pixels, std::string_view comment = "") override {
    if (unsigned char* buffer = output_(0); !buffer) return too_jpeg::WRITE_JPEG_RESULT::WJR_NO_BUFFER;
    sleep_ms(quality_ * 10);
    return too_jpeg::WRITE_JPEG_RESULT::WJR_OK;
  }
};


}

too_jpeg::ITooJpeg* too_jpeg::getTooJpeg(
    GET_BUFFER output, size_t buffer_size, unsigned short width, unsigned short height, unsigned char quality) {
  return dynamic_cast<too_jpeg::ITooJpeg*>(new MockTooJpeg(output, quality));
}
