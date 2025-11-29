#include "frame_buffer.h"

class ICamStreamer {
 protected:
  ICamStreamer() {}
 public:
  virtual ~ICamStreamer() {}
};

ICamStreamer* startCamStream(
    FrameBufferManager& frame_buffer_manager,
    FrameBuffer& capture_frame1, FrameBuffer& capture_frame2,
    unsigned int width, unsigned int height);