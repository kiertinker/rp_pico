#ifndef FRAME_BUFFER
#define FRAME_BUFFER
#include <stdio.h>
#include "pico/mutex.h"

#include <cstdlib>
#include <queue>
#include <vector>

template<class T>
struct Mallocator
{
  typedef T value_type;
  Mallocator() = default;
  template<class U>
  constexpr Mallocator(const Mallocator <U>&) noexcept {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (auto p = static_cast<T*>(aligned_alloc(4, n * sizeof(T)))) return p;
    return nullptr;
  }

  void deallocate(T* p, std::size_t n) noexcept {
    std::free(p);
  }
};
 
template<class T, class U>
bool operator==(const Mallocator <T>&, const Mallocator <U>&) { return true; }
 
template<class T, class U>
bool operator!=(const Mallocator <T>&, const Mallocator <U>&) { return false; }

class FrameBuffer {
 public:
  FrameBuffer() {}
  FrameBuffer(unsigned int buffer_size) : frame_buffer_(buffer_size) {}
  unsigned char* getBufferBytes() { return &frame_buffer_[0]; }
  size_t getBufferSize() {return frame_buffer_.size(); }
  void setBytesSet(size_t cb) { bytes_set = cb; }
  size_t getBytesSet() { return bytes_set; }

 private:
  std::vector<unsigned char, Mallocator<unsigned char>> frame_buffer_;
  size_t bytes_set;
};

class FrameBufferManager {
 private:
  class ScopedLock {
   private:
    mutex* scoped_mtx = nullptr;
   public:
    ScopedLock(mutex *mtx) : scoped_mtx(mtx) { mutex_enter_blocking(scoped_mtx); }
    ~ScopedLock() { mutex_exit(scoped_mtx); }
  };

  class Event {
   private:
    mutex event_mutex;
   public:
    Event(bool set) {
      mutex_init(&event_mutex);
      if (set)
        mutex_enter_blocking(&event_mutex);
    }
    void setEvent() { mutex_enter_blocking(&event_mutex); }
    void signalEvent() { mutex_exit(&event_mutex); }
    void wait() {
      mutex_enter_blocking(&event_mutex);
      mutex_exit(&event_mutex);
    }
    ~Event() {
      mutex_exit(&event_mutex);
    }
  };

  std::vector<FrameBuffer> frame_buffers_;
  mutex fill_queue_mutex;
  mutex send_queue_mutex;
  Event fill_queue_event;
  Event send_queue_event;
  std::queue<FrameBuffer*> ready_for_send;
  std::queue<FrameBuffer*> ready_for_fill;
  size_t frame_buffer_size_;
  bool exit_ = false;

 public:
  FrameBufferManager(unsigned char frame_buffer_count, unsigned int frame_buffer_size) :
      fill_queue_event(false),
      send_queue_event(false),
      frame_buffer_size_(frame_buffer_size) {
    mutex_init(&fill_queue_mutex);
    mutex_init(&send_queue_mutex);
    for (unsigned char i = 0; i < frame_buffer_count; ++i) {
      // printf("FrameBufferManager:  Emplacing a frame buffer...\n");
      frame_buffers_.emplace_back(frame_buffer_size);
    }
    for (auto& frame_buffer : frame_buffers_) {
      // printf("FrameBufferManager:  Adding a frame buffer...\n");
      ready_for_fill.push(&frame_buffer);
    }
  }

  unsigned char* getFrameBufferAddr(int i) {
    return frame_buffers_[i].getBufferBytes();
  }

  size_t getFrameBufferSize() { return frame_buffer_size_; }

  void readyForFill(FrameBuffer* buffer) {
    // printf("FrameBuffer::readyForFill:<< Buffer Ready for Fill!!!\n");
    buffer->setBytesSet(0);
    ScopedLock lock(&fill_queue_mutex);
    ready_for_fill.push(buffer);
    // printf("FrameBuffer::readyForFill:<< Buffer Ready for Fill!!!\n");
    fill_queue_event.signalEvent();
  }

  FrameBuffer* getFillBuffer() {
    FrameBuffer* buffer = nullptr;
    while (buffer == nullptr && !exit_) {
      {
        ScopedLock lock(&fill_queue_mutex);
        if (ready_for_fill.size() > 0) {
          // printf("FrameBuffer::getFillBuffer:<<  Frame buffer is available!!!\n");
          buffer = ready_for_fill.front();
          ready_for_fill.pop();
        } else {
          // printf("FrameBuffer::getFillBuffer:<<  NO Frame buffer is available!!!\n");
          fill_queue_event.setEvent();
        }
      }
      if (buffer == nullptr) {
        // printf("FrameBuffer::getFillBuffer:<< Waiting for next fill buffer\n");
        fill_queue_event.wait();
      }
    }
    // printf("FrameBuffer::getFillBuffer:<< Acquired Fill Buffer!!!\n");
    return buffer;
  }

  void readyForSend(FrameBuffer* buffer) {
    // printf("FrameBuffer::readyForSend:>> Buffer Ready for Send!!!\n");
    ScopedLock lock(&send_queue_mutex);
    ready_for_send.push(buffer);
    // printf("FrameBuffer::readyForSend:>> Signalling Buffer Ready for Send!!!\n");
    send_queue_event.signalEvent();
  }

  FrameBuffer* getSendBuffer() {
    FrameBuffer* buffer = nullptr;
    while (buffer == nullptr) {
      {
        // printf("FrameBuffer::getSendBuffer:>> locking send_queue_mutex...\n");
        ScopedLock lock(&send_queue_mutex);
        // printf("FrameBuffer::getSendBuffer:>> Acquired send_queue_mutex lock...\n");
        if (ready_for_send.size() > 0) {
          // printf("FrameBuffer::getSendBuffer:>> Buffer for send available...\n");
          buffer = ready_for_send.front();
          ready_for_send.pop();
        } else {
          // printf("FrameBuffer::getSendBuffer:>> NO buffer for send available...\n");
          send_queue_event.setEvent();
        }
      }
      if (buffer == nullptr) {
        // printf("FrameBuffer::getSendBuffer:>> Waiting for send buffer!!!\n");
        send_queue_event.wait();
      }
    }
    // printf("FrameBuffer::getSendBuffer:>> Acquired Send Buffer!!!\n");
    return buffer;
  }

  void signalExit() {
    exit_ = true;
    fill_queue_event.signalEvent();
  }
};

#endif