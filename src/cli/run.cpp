#include "cli.hpp"
#include "config/config.hpp"
#include "graph/types.hpp"
#include <iostream>

namespace mokai {

std::expected<std::monostate, CliError>
Cli::handleRun(const std::vector<std::string> &args) {
  auto build_result = handleBuild(args);
  if (!build_result) {
    return std::unexpected(build_result.error());
  }

  std::string explicit_target = "";
  if (args.size() > 1) {
    explicit_target = args[1];
  }

  if (!m_config || !m_config->getManifest()) {
    return std::unexpected(CliError{CliError::Code::InvalidWorkspace,
                                    "Mokai configuration or manifest context "
                                    "was not initialized properly."});
  }

  const auto &targets_vec = m_config->getManifest()->targets;

  Target selected_target;
  bool found = false;

  if (!explicit_target.empty()) {
    for (const auto &target : targets_vec) {
      if (target.name == explicit_target) {
        selected_target = target;
        found = true;
        break;
      }
    }
    if (!found) {
      return std::unexpected(
          CliError{CliError::Code::InvalidArguments,
                   "Target '" + explicit_target +
                       "' does not exist in your mokai.toml manifest."});
    }
  } else {
    for (const auto &target : targets_vec) {
      if (target.type == TargetType::Executable && target.is_default) {
        selected_target = target;
        found = true;
        break;
      }
    }

    if (!found) {
      std::string project_name = m_config->getManifest()->project.name;
      for (const auto &target : targets_vec) {
        if (target.name == project_name &&
            target.type == TargetType::Executable) {
          selected_target = target;
          found = true;
          break;
        }
      }
    }

    if (!found) {
      std::vector<Target> executables;
      for (const auto &target : targets_vec) {
        if (target.type == TargetType::Executable) {
          executables.push_back(target);
        }
      }

      if (executables.size() == 1) {
        selected_target = executables[0];
        found = true;
      } else if (executables.size() > 1) {
        return std::unexpected(
            CliError{CliError::Code::InvalidArguments,
                     "Ambiguous execution: Multiple executable targets found. "
                     "Specify choice via 'mokai run [target_name]'"});
      }
    }
  }

  if (!found) {
    return std::unexpected(CliError{CliError::Code::BuildFailed,
                                    "No buildable executable targets could be "
                                    "resolved in this project workspace."});
  }

  if (selected_target.type != TargetType::Executable) {
    auto type_to_string = [](TargetType t) -> std::string {
      switch (t) {
      case TargetType::StaticLibrary:
        return "static_library";
      case TargetType::SharedLibrary:
        return "dynamic_library";
      default:
        return "non-executable target type";
      }
    };

    return std::unexpected(CliError{CliError::Code::InvalidArguments,
                                    "Resolved target '" + selected_target.name +
                                        "' is a " +
                                        type_to_string(selected_target.type) +
                                        ", not an executable binary."});
  }

  std::filesystem::path artifact_path =
      m_options.root_dir / "build" /
      (m_options.profile == BuildProfile::Debug ? "debug" : "release") /
      (selected_target.name + OS::GetExecutableExtension());

  if (!std::filesystem::exists(artifact_path)) {
    return std::unexpected(
        CliError{CliError::Code::GeneralFailure,
                 "Compiled binary artifact missing from disk path: " +
                     artifact_path.string()});
  }

  m_logger.Info("Launching runtime target context: " + selected_target.name);
  std::cout << Style::Dim
            << "──────────────────────────────────────────────────"
            << Style::Reset << "\n"
            << std::endl;

  int exit_code = OS::ExecuteBinaryAndForwardStreams(artifact_path.string());

  std::cout << "\n"
            << Style::Dim
            << "──────────────────────────────────────────────────"
            << Style::Reset << std::endl;

  if (exit_code != 0) {
    return std::unexpected(CliError{
        CliError::Code::GeneralFailure,
        "Application runtime process terminated with non-zero exit status: " +
            std::to_string(exit_code)});
  }

  return std::monostate{};
}

} // namespace mokai
