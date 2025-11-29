#include <stdio.h>
#include <malloc.h>
//#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "frame_buffer.h"
#include "cam_streamer.h"
#include "tcp_streamer.h"

#include <memory>

constexpr unsigned int FRAME_WIDTH = 320;
constexpr unsigned int FRAME_HEIGHT = 240;
constexpr unsigned char JPEG_FRAME_BUFFER_COUNT = 10;
constexpr unsigned int JPEG_FRAME_BUFFER_SIZE = 3000;
constexpr char WIFI_SSID[] = "JKATHOME";
constexpr char WIFI_PASSWORD[] = "06061969AD";
constexpr char SERVER_IP[] = "192.168.0.11";
constexpr unsigned short SERVER_PORT = 27016;

int main() {
  stdio_init_all();
  stdio_usb_init();

  sleep_ms(4000);
  printf("Starting!!!\n");

  FrameBuffer capture_frame1(FRAME_WIDTH * FRAME_HEIGHT);
  FrameBuffer capture_frame2(FRAME_WIDTH * FRAME_HEIGHT);
  FrameBufferManager buffer_manager(JPEG_FRAME_BUFFER_COUNT, JPEG_FRAME_BUFFER_SIZE);
  printf("Frame buffer 1: %08X\n", capture_frame1.getBufferBytes());
  printf("Frame buffer 2: %08X\n", capture_frame2.getBufferBytes());
  printf("Transfer buffer 1: %08X\n", buffer_manager.getFrameBufferAddr(0));
  printf("Transfer buffer 2: %08X\n", buffer_manager.getFrameBufferAddr(1));

  std::unique_ptr<ITcpStreamer> tcp_streamer(getTcpStreamer(WIFI_SSID, WIFI_PASSWORD));
  if (!tcp_streamer) {
    printf("main:  Failed to get TcpStreamer!!!\n");
    return 0;
  }
  while (!tcp_streamer->connect(SERVER_IP, SERVER_PORT));

  std::unique_ptr<ICamStreamer> cam_streamer(startCamStream(
      buffer_manager, capture_frame1, capture_frame2, FRAME_WIDTH, FRAME_HEIGHT));
  tcp_streamer->startTcpStream(buffer_manager);
  
  return 0;

}