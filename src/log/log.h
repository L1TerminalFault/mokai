#pragma once
#include <string>

namespace mokai::log {

// ---------------------------------------------------------------------------
// Theme: every color used by the logger lives here, named by role rather
// than by raw value, so the whole look can be re-themed from one place.
// Values are raw ANSI escape codes (truecolor where useful for nicer pastel
// tones; fall back to basic codes if you want max-compatibility instead).
// ---------------------------------------------------------------------------
namespace theme {
inline constexpr const char *reset = "\033[0m";

inline constexpr const char *timestamp = "\033[90m"; // gray
inline constexpr const char *prefix = "\033[34m";    // blue

inline constexpr const char *info_label = "\033[36m";         // cyan
inline constexpr const char *warn_label = "\033[33m";         // yellow
inline constexpr const char *error_label = "\033[31m";        // red
inline constexpr const char *success_label = "\033[38;5;42m"; // green
inline constexpr const char *debug_label = "\033[90m";        // gray

inline constexpr const char *message_default = "\033[0m"; // normal
inline constexpr const char *message_dim =
    "\033[2m"; // dim, for debug body text

inline constexpr const char *caret = "\033[31m";      // red, the ^^^^ underline
inline constexpr const char *hint = "\033[36m";       // cyan, "did you mean..."
inline constexpr const char *line_no = "\033[90m";    // gray, "2 |"
inline constexpr const char *source_line = "\033[0m"; // normal source text
} // namespace theme

enum class Level { Debug, Info, Warn, Error, Success };

class Logger {
public:
  Logger();
  ~Logger();

  void Init();
  void Shutdown();

  void SetPrefix(const std::string &prefix); // e.g. "mokai", shown as [mokai]
  void SetLevel(Level minLevel);             // suppress anything below this

  // Standard one-line logs: timestamp [prefix] | level |: message
  void Debug(const std::string &msg);
  void Info(const std::string &msg);
  void Warn(const std::string &msg);
  void Error(const std::string &msg);
  void Success(const std::string &msg);

  // Step/progress logging, e.g. "[3/7] Compiling src/main.cpp"
  void Step(int current, int total, const std::string &msg);

  // Rustc-style pointed error with source context:
  //   2 | actualLine
  //     | ^^^^^^^  did you mean: something
  void ErrorInline(const std::string &sourceLine, const std::string &hint,
                   int lineNumber, int caretStart = 0,
                   int caretLength = -1); // -1 = underline whole line

private:
  std::string m_prefix = "mokai";
  Level m_minLevel = Level::Debug;

  std::string FormatTimestamp() const;
  void WriteLine(Level level, const std::string &label, const std::string &msg);
  bool ShouldLog(Level level) const;
};

} // namespace mokai::log
