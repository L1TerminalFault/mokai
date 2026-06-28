#pragma once

#include <chrono>
#include <print>
#include <string_view>

namespace mokai {

class Log {
public:
  static void Info(std::string_view msg) {
    std::print("\033[32m[ INFO  ]\033[0m {}\n", msg);
  }

  static void Warn(std::string_view msg) {
    std::print("\033[33m[ WARN  ]\033[0m {}\n", msg);
  }

  static void Error(std::string_view msg) {
    std::print("\033[31m[ ERROR ]\033[0m {}\n", msg);
  }

  static void Metric(std::string_view phase, long long micros) {
    std::print("\033[36m[ METRIC]\033[0m {:<40} {:>10} µs\n", phase, micros);
  }
};

class PerfTimer {
private:
  std::chrono::time_point<std::chrono::steady_clock> m_last_mark;

public:
  PerfTimer() : m_last_mark(std::chrono::steady_clock::now()) {}

  void Mark(std::string_view phase) {
    auto now = std::chrono::steady_clock::now();
    auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_mark)
            .count();
    m_last_mark = now;
    Log::Metric(phase, micros);
  }
};
class PerfScope {
private:
  std::string_view m_phase;
  PerfTimer m_timer;

public:
  explicit PerfScope(std::string_view phase) : m_phase(phase) {}

  ~PerfScope() { m_timer.Mark(m_phase); }
};

} // namespace mokai
