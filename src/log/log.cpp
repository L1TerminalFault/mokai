#include "log.h"
#include <chrono>
#include <ctime>
#include <iostream>

namespace mokai::log {

Logger::Logger() {}
Logger::~Logger() {}

void Logger::Init() {}
void Logger::Shutdown() {}

void Logger::SetPrefix(const std::string &prefix) { m_prefix = prefix; }
void Logger::SetLevel(Level minLevel) { m_minLevel = minLevel; }

bool Logger::ShouldLog(Level level) const {
  return static_cast<int>(level) >= static_cast<int>(m_minLevel);
}

std::string Logger::FormatTimestamp() const {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tmBuf{};
  localtime_r(&t, &tmBuf);

  char buf[16]; // "HH:MM:SS"
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmBuf);
  return std::string(buf);
}

void Logger::WriteLine(Level level, const std::string &label,
                       const std::string &msg) {
  if (!ShouldLog(level)) {
    return;
  }

  const char *labelColor = theme::info_label;
  switch (level) {
  case Level::Debug:
    labelColor = theme::debug_label;
    break;
  case Level::Info:
    labelColor = theme::info_label;
    break;
  case Level::Warn:
    labelColor = theme::warn_label;
    break;
  case Level::Error:
    labelColor = theme::error_label;
    break;
  case Level::Success:
    labelColor = theme::success_label;
    break;
  }

  std::cout << theme::timestamp << FormatTimestamp() << theme::reset << " "
            << theme::prefix << "[" << m_prefix << "]" << theme::reset << " | "
            << labelColor << label << theme::reset << " |: " << msg << "\n";
}

void Logger::Debug(const std::string &msg) {
  WriteLine(Level::Debug, "debug", msg);
}
void Logger::Info(const std::string &msg) {
  WriteLine(Level::Info, "info", msg);
}
void Logger::Warn(const std::string &msg) {
  WriteLine(Level::Warn, "warn", msg);
}
void Logger::Error(const std::string &msg) {
  WriteLine(Level::Error, "error", msg);
}
void Logger::Success(const std::string &msg) {
  WriteLine(Level::Success, "success", msg);
}

void Logger::Step(int current, int total, const std::string &msg) {
  if (!ShouldLog(Level::Info)) {
    return;
  }
  std::cout << theme::timestamp << FormatTimestamp() << theme::reset << " "
            << theme::prefix << "[" << m_prefix << "]" << theme::reset << " | "
            << theme::info_label << "[" << current << "/" << total << "]"
            << theme::reset << " |: " << msg << "\n";
}

void Logger::ErrorInline(const std::string &sourceLine, const std::string &hint,
                         int lineNumber, int caretStart, int caretLength) {
  if (!ShouldLog(Level::Error)) {
    return;
  }

  std::string lineNoStr = std::to_string(lineNumber);
  std::string gutter(lineNoStr.size(), ' ');

  int underlineLen = (caretLength == -1)
                         ? static_cast<int>(sourceLine.size()) - caretStart
                         : caretLength;
  if (underlineLen < 1)
    underlineLen = 1;

  // some spacing
  std::cout << "\n";

  // "2 | actualLine"
  std::cout << theme::line_no << lineNoStr << " |" << theme::reset << " "
            << theme::source_line << sourceLine << theme::reset << "\n";

  // "  | ^^^^^^^  did you mean: something"
  std::cout << theme::line_no << gutter << " |" << theme::reset << " "
            << std::string(caretStart, ' ') << theme::caret
            << std::string(underlineLen, '^') << theme::reset << "  "
            << theme::hint << hint << theme::reset << "\n";

  std::cout << "\n";
}

} // namespace mokai::log
