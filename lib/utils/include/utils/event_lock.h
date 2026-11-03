#pragma once
#include "pico/mutex.h"

class EventLock {
 public:
  EventLock(mutex& mtx) : mtx_(mtx) {
    mutex_enter_blocking(&mtx_);
  }
  ~EventLock() {
    if (!released) {
      mutex_enter_blocking(&mtx_);
      mutex_exit(&mtx_);
    }
  }
  void release() {
    released = true;
    mutex_exit(&mtx_);
  }
 private:
  mutex& mtx_;
  bool released = false;
};
