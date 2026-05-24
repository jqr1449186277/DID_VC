#pragma once

#include <chrono>
#include <thread>

template <class Probe>
bool wait_until(std::chrono::milliseconds timeout,
                std::chrono::milliseconds interval,
                Probe&& probe) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int attempt = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    ++attempt;
    if (probe(attempt)) return true;
    std::this_thread::sleep_for(interval);
  }
  return false;
}
