#include "frame_buffer.h"

#include <string_view>


class ITcpStreamer {
 public:
  ITcpStreamer() {}
  virtual ~ITcpStreamer() {}

  virtual bool connect(std::string_view ip, unsigned short port) = 0;
  virtual void startTcpStream(FrameBufferManager& frame_buffer_manager) = 0;
};

ITcpStreamer* getTcpStreamer(std::string_view wifi_ssid, std::string_view wifi_pwd);