#pragma once
#include <functional>
#include <utility>

// A scope guard is a utility for ensuring that a function is called when the scope exits.
// It is particularly useful for resource management, ensuring that cleanup code such as
// disabling Wi-Fi or deinitializing hardware is executed even if an exception is thrown
// or the function returns early.
template <typename F, typename... Args>
class ScopeGuard {
public:
  ScopeGuard(F&& f, Args&&... args)
      : func_(std::bind(std::forward<F>(f), std::forward<Args>(args)...)) {}

  ~ScopeGuard() {
    func_();
  }

  // Non-copyable to avoid double execution
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;

  // Movable
  ScopeGuard(ScopeGuard&&) = default;

private:
  std::function<void()> func_;
};

// Deduction helper (useful pre-C++17, harmless after), otherwise it's a bit verbose to use
// E.g. auto guard = make_scope_guard(cleanup, "resource1", 42);
template <typename F, typename... Args>
auto make_scope_guard(F&& f, Args&&... args) {
  return ScopeGuard<F, Args...>(std::forward<F>(f), std::forward<Args>(args)...);
}