#pragma once
#include "config/toml.hpp"
#include "graph/types.hpp"
#include "log/log.h"
#include <expected>
#include <memory>
#include <string>

namespace mokai {

struct FuzzyFindResult {
  bool found;
  std::string best_match;
};

class Config {
public:
  Config(std::string workingDir);
  std::shared_ptr<ProjectManifest> getManifest() const {
    return std::make_shared<ProjectManifest>(m_manifest);
  }

private:
  log::Logger m_logger;
  std::string m_file_content;
  toml::table m_config_toml;
  ProjectManifest m_manifest;
  // parsing
  std::expected<void, std::string> loadConfig(const std::string &path);
  std::expected<void, std::string> parseConfig();
  std::expected<void, std::string> extractProjectData();
  // fetch from git
  bool isGitDep(std::string &str);
  // local config
  bool isLocDep(std::string &str);
  bool runTarget(const std::string &targetName);
  bool createProject(const std::string &projectName);
  // helpers
  bool checkIsFileAndExists(const std::string &path);
  bool checkIsFolderAndExists(const std::string &path);
  FuzzyFindResult fuzzyFindCloseFile(std::string &path);
  FuzzyFindResult fuzzyFindCloseFolder(std::string &path);
};

} // namespace mokai
