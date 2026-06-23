#pragma once
#include "exp.hpp"
#include "log/log.h"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

inline constexpr std::string_view MOKAI_VERSION = "0.0.1alpha";

namespace mokai {

enum class Verbosity { Quiet, Default, Verbose };
enum class ColorMode { Auto, Always, Never };

namespace Style {
inline constexpr std::string_view Reset = "\033[0m";
inline constexpr std::string_view Bold = "\033[1m";
inline constexpr std::string_view Dim = "\033[90m";
inline constexpr std::string_view Green = "\033[38;5;42m";
inline constexpr std::string_view Cyan = "\033[36m";
inline constexpr std::string_view Red = "\033[31m";
inline constexpr std::string_view Yellow = "\033[33m";
inline constexpr std::string_view Arrow = "❯ ";
inline constexpr std::string_view Success = "✔ ";
inline constexpr std::string_view Info = "ℹ ";
inline constexpr std::string_view Error = "✖ ";
} // namespace Style

struct GlobalOptions {
  std::filesystem::path root_dir = std::filesystem::current_path();
  Verbosity verbosity = Verbosity::Default;
  ColorMode color = ColorMode::Auto;
};

struct CommandInfo {
  std::string_view usage;
  std::string_view explanation;
  std::function<void(const std::vector<std::string> &)> callback;
};

class Cli {
public:
  Cli(int argc, char **argv);
  ~Cli();
  void ParseCliArgs(int argc, char *argv[]);

private:
  log::Logger m_logger;
  GlobalOptions m_options;
  std::unordered_map<std::string, CommandInfo> m_supported_commands;

  void initCommands();
  void logSupportedCommands();
  void handleHelp(const std::vector<std::string> &args);
  void handleBuild(const std::vector<std::string> &args);
  void handlePackageAdd(const std::vector<std::string> &args);
  void handleCreateProject(const std::vector<std::string> &args);
};
} // namespace mokai
