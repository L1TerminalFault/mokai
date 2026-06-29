#pragma once

#define MOKAI_LOG_LEVEL_DEBUG 0
#define MOKAI_LOG_LEVEL_INFO 1
#define MOKAI_LOG_LEVEL_WARN 2
#define MOKAI_LOG_LEVEL_ERROR 3
#define MOKAI_LOG_LEVEL_SUCCESS 4
#define MOKAI_LOG_LEVEL_NONE 5

#ifndef MOKAI_LOG_LEVEL
#define MOKAI_LOG_LEVEL MOKAI_LOG_LEVEL_INFO
#endif

#define MOKAI_DISABLE_METRICS

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <format>
#include <mutex>
#include <print>
#include <string>
#include <string_view>

namespace mokai {

struct SourceLocation {
  std::string_view source_line;
  std::string_view hint;
  int line;
  int caret_start;
  int caret_length;
};

class Log {
public:
  using SinkFn = void (*)(std::string_view);

private:
  struct TimeCache {
    static inline std::atomic<std::time_t> last_sec{0};
    static inline char cached_str[16]{};
    static inline std::mutex mtx;

    static std::string_view get() {
      auto now = std::chrono::system_clock::now();
      std::time_t sec = std::chrono::system_clock::to_time_t(now);
      if (sec != last_sec.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(mtx);
        if (sec != last_sec.load(std::memory_order_relaxed)) {
          std::tm tm_info;
#if defined(_MSC_VER)
          localtime_s(&tm_info, &sec);
#else
          localtime_r(&sec, &tm_info);
#endif
          std::strftime(cached_str, sizeof(cached_str), "%H:%M:%S", &tm_info);
          last_sec.store(sec, std::memory_order_release);
        }
      }
      return {cached_str};
    }
  };

  static inline std::mutex g_mutex;
  static inline std::atomic<SinkFn> g_sink{nullptr};

  static int utf8_visual_width(std::string_view str, int byte_limit) {
    int width = 0;
    int limit = std::min(static_cast<int>(str.size()), byte_limit);
    for (int i = 0; i < limit; ++i) {
      if ((static_cast<unsigned char>(str[i]) & 0xC0) != 0x80) {
        ++width;
      }
    }
    return width;
  }

  template <typename... Args>
  static void write_formatted(int level, std::string_view category,
                              const char *color, std::string_view fmt_str,
                              Args &&...args) {
    std::string content = std::vformat(fmt_str, std::make_format_args(args...));
    std::string output =
        std::format("\033[90m{}\033[0m {}{:<10}\033[0m {}", TimeCache::get(),
                    color, category, content);

    {
      std::lock_guard<std::mutex> lock(g_mutex);
      std::println("{}", output);
      auto current_sink = g_sink.load(std::memory_order_relaxed);
      if (current_sink) {
        current_sink(output);
      }
    }
  }

public:
  static void SetSink(SinkFn sink) {
    g_sink.store(sink, std::memory_order_relaxed);
  }

  static void Debug(std::string_view msg) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_DEBUG
    write_formatted(MOKAI_LOG_LEVEL_DEBUG, "Debug", "\033[90m", "{}", msg);
#endif
  }

  static void Info(std::string_view msg) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO
    write_formatted(MOKAI_LOG_LEVEL_INFO, "Info", "\033[34m", "{}", msg);
#endif
  }

  static void Warn(std::string_view msg) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_WARN
    write_formatted(MOKAI_LOG_LEVEL_WARN, "Warning", "\033[33m", "{}", msg);
#endif
  }

  static void Error(std::string_view msg) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_ERROR
    write_formatted(MOKAI_LOG_LEVEL_ERROR, "Error", "\033[31m", "{}", msg);
#endif
  }

  static void Success(std::string_view msg) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_SUCCESS
    write_formatted(MOKAI_LOG_LEVEL_SUCCESS, "Success", "\033[32m", "{}", msg);
#endif
  }

  static void Compile(int current, int total, std::string_view path) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO
    write_formatted(MOKAI_LOG_LEVEL_INFO, "Compiling", "\033[36m",
                    "{} \033[90m[{}/{}]\033[0m", path, current, total);
#endif
  }

  static void Metric(std::string_view phase, long long micros) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    if (micros >= 1'000'000) {
      double s = micros / 1'000'000.0;
      write_formatted(MOKAI_LOG_LEVEL_INFO, "Metric", "\033[35m",
                      "{:<40} | {:>8.2f} s", phase, s);
    } else if (micros >= 1'000) {
      double ms = micros / 1'000.0;
      write_formatted(MOKAI_LOG_LEVEL_INFO, "Metric", "\033[35m",
                      "{:<40} | {:>8.2f} ms", phase, ms);
    } else {
      write_formatted(MOKAI_LOG_LEVEL_INFO, "Metric", "\033[35m",
                      "{:<40} | {:>8} us", phase, micros);
    }
#endif
  }

  static void ErrorInline(const SourceLocation &loc) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_ERROR
    std::string lineNo = std::to_string(loc.line);
    std::string gutter(lineNo.size(), ' ');

    int visual_start = utf8_visual_width(loc.source_line, loc.caret_start);
    int raw_len =
        (loc.caret_length <= 0)
            ? static_cast<int>(loc.source_line.size()) - loc.caret_start
            : loc.caret_length;
    int visual_len = utf8_visual_width(
        loc.source_line.substr(std::min(static_cast<size_t>(loc.caret_start),
                                        loc.source_line.size())),
        raw_len);

    if (visual_len < 1) {
      visual_len = 1;
    }

    std::string output = std::format(
        "\n \033[90m{} |\033[0m {}\n \033[90m{} |\033[0m "
        "{}{}{} \033[33m{}\033[0m\n",
        lineNo, loc.source_line, gutter, std::string(visual_start, ' '),
        "\033[31m", std::string(visual_len, '^'), loc.hint);

    {
      std::lock_guard<std::mutex> lock(g_mutex);
      std::print("{}", output);
      auto current_sink = g_sink.load(std::memory_order_relaxed);
      if (current_sink) {
        current_sink(output);
      }
    }
#endif
  }
};

class PerfTimer {
private:
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
  std::chrono::time_point<std::chrono::steady_clock> m_start;
#endif

public:
  PerfTimer() {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    m_start = std::chrono::steady_clock::now();
#endif
  }

  void reset() {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    m_start = std::chrono::steady_clock::now();
#endif
  }

  long long elapsed_micros() const {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - m_start)
        .count();
#else
    return 0;
#endif
  }

  void Mark([[maybe_unused]] std::string_view phase) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    auto now = std::chrono::steady_clock::now();
    auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now - m_start)
            .count();
    m_start = now;
    Log::Metric(phase, micros);
#endif
  }
};

class PerfScope {
private:
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
  std::string_view m_phase;
  PerfTimer m_timer;
#endif

public:
  explicit PerfScope([[maybe_unused]] std::string_view phase) {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    m_phase = phase;
#endif
  }

  ~PerfScope() {
#if MOKAI_LOG_LEVEL <= MOKAI_LOG_LEVEL_INFO && !defined(MOKAI_DISABLE_METRICS)
    m_timer.Mark(m_phase);
#endif
  }
};
} // namespace mokai
