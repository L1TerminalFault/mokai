#pragma once
#include "config/tom.hpp" // Swapped to your flat parser engine
#include "core/os.hpp"
#include "graph/types.hpp"
#include <expected>
#include <string>

namespace mokai {

struct GlobalOptions;

struct FuzzyFindResult {
  bool found;
  std::string best_match;
};

class Config {
public:
  Config(std::string workingDir, GlobalOptions &options);

  const ProjectManifest &getManifest() const { return m_manifest; }

private:
  std::string m_file_content;
  tom::Parser m_parser;
  ProjectManifest m_manifest;

  Platform m_host_platform = OS::GetCurrentPlatform();

  std::expected<void, std::string> loadConfig(const std::string &path);
  std::expected<void, std::string> parseConfig();
  std::expected<void, std::string> extractProjectData(GlobalOptions &ops);

  bool isGitDep(std::string &str);

  bool isLocDep(std::string &str);
  bool runTarget(const std::string &targetName);
  bool createProject(const std::string &projectName);

  bool checkIsFileAndExists(const std::string &path);
  bool checkIsFolderAndExists(const std::string &path);
  FuzzyFindResult fuzzyFindCloseFile(std::string &path);
  FuzzyFindResult fuzzyFindCloseFolder(std::string &path);
};

} // namespace mokai
