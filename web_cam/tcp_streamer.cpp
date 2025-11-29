#include <stdio.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "tcp_streamer.h"
#include "tcp_client/tcp_client.h"

#include <malloc.h>

void printBss() {
  extern char __bss_end__, __bss_start__;
  printf("BSS:  end = %u, start = %u\n", &__bss_end__, &__bss_start__);
}

uint32_t getTotalHeap(void) {
  extern char __StackLimit, __bss_end__;
   
  return &__StackLimit  - &__bss_end__;
}

uint32_t getFreeHeap(void) {
  struct mallinfo m = mallinfo();

  return getTotalHeap() - m.uordblks;
}


namespace {

constexpr int ITERATIONS = 5000;
int stream_counter = 0;

class CallbackReceiver : public tcp_client::IReceiver {
 public:
  ~CallbackReceiver() {}
  void callback(const unsigned char bytes[], unsigned int cb) override {
  }
};

}  // anonymous namespace

class TcpStreamer : public ITcpStreamer, tcp_client::IReceiver {
 private:
  std::unique_ptr<tcp_client::TcpConnection> connection_;
  bool connected_ = false;

 public:
  TcpStreamer() {}
  ~TcpStreamer() {}

  // IReceiver method
  void callback(const unsigned char bytes[], unsigned int cb) override {}

  // ITcpStreamer methods
  bool connect(std::string_view ip, unsigned short port) {
    if (connected_) {
      printf("TcpStreamer::connect:  Already connected.  Nothing to do.\n");
      return true;
    }
    printf("make_connection: Getting connection!!!\n");
    auto result = connection_to_hostip(ip, port, dynamic_cast<IReceiver*>(this));

    if (result.index() == 1) {
      printf("make_connection:  Connect error: %d\n", static_cast<int>(std::get<tcp_client::TcpErr>(result)));
      return false;
    }
    printf("make_connection: Connection made.\n");
    connection_.reset(std::get<tcp_client::TcpConnection*>(result));
    connected_ = true;
    return true;
  }

  void startTcpStream(FrameBufferManager& frame_buffer_manager) {
    if (!connected_) {
      printf("TcpStreamer::startTcpStream:  No connection.  Can't stream\n");
      return;
    }

    printf("TcpStreamer::startTcpStream...\n");
    int64_t write_cumulative_time = 0;
    int64_t complete_cumalative_time = 0;
    absolute_time_t stream_start;

    for (int i = 0; i < ITERATIONS; ++i) {
      FrameBuffer* pframe_buffer = frame_buffer_manager.getSendBuffer();
      stream_start = get_absolute_time();
      printf("TcpStreamer::startTcpStream:  Buffer Addr = %08X, bytes to stream = %u.\n", pframe_buffer->getBufferBytes(), pframe_buffer->getBytesSet());
      if (tcp_client::TcpErr err = connection_->send(pframe_buffer->getBufferBytes(), pframe_buffer->getBytesSet());
          err != tcp_client::TcpErr::TCP_ERR_OK) {
        printf("Frame send returned error: %d\n", static_cast<int>(err));
        printf("startTcpStream:  In error: Free heap = %d.\n", getFreeHeap());
        break;
      }
      printf("TcpStreamer::startTcpStream:  Finished stream.\n");
      int64_t write_diff = connection_->getWriteTime();
      int64_t complete_diff = connection_->getSentTime();
      // printf("startTcpStream:  memory after iteration %d: Free heap = %d.\n", stream_counter, getFreeHeap());
      // printf("TCP STREAM  for iterations %d: write: %lld, complete: %lld, total stream time: %lld\n",
      //     stream_counter++, write_diff, complete_diff, absolute_time_diff_us(stream_start, get_absolute_time()));
      write_cumulative_time += write_diff;
      complete_cumalative_time += complete_diff;
      frame_buffer_manager.readyForFill(pframe_buffer);
    }
    // std::cout << "Cumulative time = " << write_cumulative_time << ", Average time for TCP Transmission = " << write_cumulative_time / (int64_t)ITERATIONS <<"\n";
    // std::cout << "Cumulative time = " << complete_cumalative_time << ", Average time for TCP Transmission = " << complete_cumalative_time / (int64_t)ITERATIONS <<"\n";
    printf("Exiting...\n");
    return;
  }
};

ITcpStreamer* getTcpStreamer(std::string_view wifi_ssid, std::string_view wifi_pwd) {
  pico_error_codes err = tcp_client::wifi_init(wifi_ssid, wifi_pwd);
  printBss();
  printf("getTcpStreamer:  after wifi init Free heap = %d.\n", getFreeHeap());
  if (err != 0) {
    printf("getTcpStreamer: Failed to init wifi: %d\n", err);
    return nullptr;
  }
  return dynamic_cast<ITcpStreamer*>(new TcpStreamer());
}
