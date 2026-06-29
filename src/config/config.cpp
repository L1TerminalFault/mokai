#include "config.hpp"
#include "cli/cli.hpp"
#include "config/toml.hpp"
#include "graph/types.hpp"
#include "log/log.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace mokai {

static size_t calculateDistance(std::string_view s1, std::string_view s2) {
  const size_t len1 = s1.size(), len2 = s2.size();
  std::vector<std::vector<size_t>> d(len1 + 1, std::vector<size_t>(len2 + 1));
  for (size_t i = 0; i <= len1; ++i) {
    d[i][0] = i;
  }
  for (size_t j = 0; j <= len2; ++j) {
    d[0][j] = j;
  }

  for (size_t i = 1; i <= len1; ++i) {
    for (size_t j = 1; j <= len2; ++j) {
      size_t cost =
          (std::tolower(s1[i - 1]) == std::tolower(s2[j - 1])) ? 0 : 1;
      d[i][j] =
          std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
    }
  }
  return d[len1][len2];
}

static fs::path getGlobalMokaiDir() {
#if defined(_WIN32) || defined(_WIN64)
  const char *localAppdata = std::getenv("LOCALAPPDATA");
  return fs::path(localAppdata ? localAppdata : "C:\\") / "mokai";
#else
  const char *home = std::getenv("HOME");
  return fs::path(home ? home : "/tmp") / ".mokai";
#endif
}

Config::Config(std::string workingDir, GlobalOptions &ops) {
  if (!checkIsFolderAndExists(workingDir)) {
    std::string hint = "Ensure the specified project root path is correct.";
    auto fuzzyDir = fuzzyFindCloseFolder(workingDir);
    if (fuzzyDir.found) {
      hint = std::format("Did you mean to use directory '{}'?",
                         fs::path(fuzzyDir.best_match).filename().string());
    }
    Log::Error(std::format(
        "Specified directory does not exist or is inaccessible: '{}'\nHint: {}",
        workingDir, hint));
    return;
  }

  m_manifest.base_dir = workingDir;

  fs::path config_path = fs::path(workingDir) / "mokai.toml";
  std::string config_path_str = config_path.string();

  if (!checkIsFileAndExists(config_path_str)) {
    std::string hint = "Run 'mokai create' to scaffold a new workspace.";
    auto fuzzyFile = fuzzyFindCloseFile(config_path_str);
    if (fuzzyFile.found) {
      hint = std::format(
          "A similar file exists: '{}'. Check if the file is correctly named "
          "'mokai.toml'.",
          fs::path(fuzzyFile.best_match).filename().string());
    }
    Log::Error(
        std::format("Missing project manifest file at location: '{}'\nHint: {}",
                    config_path.string(), hint));
    return;
  }

  if (auto res = loadConfig(config_path_str); !res) {
    Log::Error(res.error());
    return;
  }

  if (auto res = parseConfig(); !res) {
    if (!res.error().empty()) {
      Log::Error(res.error());
    }
    return;
  }
  if (auto res = extractProjectData(ops); !res) {
    Log::Error("Failed to build project model from 'mokai.toml'.");
    Log::Error(res.error());
    return;
  }
}

std::expected<void, std::string> Config::loadConfig(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return std::unexpected(
        std::format("Unable to open manifest file: '{}'", path));
  }

  std::stringstream stream;
  stream << file.rdbuf();
  m_file_content = stream.str();

  if (m_file_content.empty()) {
    return std::unexpected(std::format(
        "Manifest file contains no configuration data: '{}'", path));
  }
  return {};
}

std::expected<void, std::string> Config::parseConfig() {
  try {
    m_config_toml = toml::parse(m_file_content);
  } catch (const toml::parse_error &e) {
    auto source = e.source();
    size_t target_line = source.begin.line;
    size_t target_col = source.begin.column;

    std::string source_line;
    std::stringstream ss(m_file_content);
    size_t current_line = 1;
    while (std::getline(ss, source_line)) {
      if (current_line == target_line) {
        break;
      }
      current_line++;
    }

    int caret_len = 1;
    if (source.end.line == target_line && source.end.column > target_col) {
      caret_len = static_cast<int>(source.end.column - target_col);
    }

    Log::Error("Failed to parse project manifest (syntax error).");
    Log::ErrorInline({source_line, e.description(),
                      static_cast<int>(target_line),
                      static_cast<int>(target_col - 1), caret_len});

    return std::unexpected("");
  }
  return {};
}

std::expected<void, std::string>
Config::extractProjectData(GlobalOptions &ops) {
  auto extract_string_array = [](auto &&node_view,
                                 std::vector<std::string> &dest) {
    if (auto *arr = node_view.as_array()) {
      for (auto &&el : *arr) {
        if (auto val = el.template value<std::string>()) {
          dest.push_back(*val);
        }
      }
    }
  };

  ProjectMetadata metadata;
  toml::node_view<toml::node> projectTable = m_config_toml["project"];

  if (!projectTable.is_table()) {
    return std::unexpected(
        "Missing required [project] header inside the configuration file.");
  }

  metadata.name = projectTable["name"].value_or("");
  if (metadata.name.empty()) {
    return std::unexpected("Required field 'name' is missing or empty from the "
                           "[project] configuration.");
  }

  metadata.version = projectTable["version"].value_or("");
  metadata.license = projectTable["license"].value_or("");
  metadata.description = projectTable["description"].value_or("");
  metadata.homepage = projectTable["homepage"].value_or("");
  metadata.default_target = projectTable["default_target"].value_or("");
  ops.default_compiler = projectTable["default_compiler"].value_or("");

  if (ops.default_compiler.empty()) {
    ops.default_compiler = projectTable["default_compiler"].value_or("");
  }

  if (auto *vf_table = projectTable["version_from"].as_table()) {
    VersionFromSpec vf;
    vf.file = (*vf_table)["file"].value_or("");
    vf.pattern = (*vf_table)["pattern"].value_or("");
    metadata.version_from = std::move(vf);
  }

  auto cppversion = projectTable["cpp_version"].value<std::string>();
  static const std::vector<std::string> known_versions{
      "c++11", "c++14", "c++17", "c++20", "c++23", "c++26"};
  metadata.cpp_version = cppversion.value_or("c++23");
  if (!std::ranges::contains(known_versions, metadata.cpp_version)) {
    return std::unexpected(std::format("Unsupported C++ standard version: '{}'",
                                       metadata.cpp_version));
  }

  static const std::vector<std::string> known_c_versions{"c89", "c90", "c99",
                                                         "c11", "c17", "c23"};

  auto cversion = projectTable["c_version"].value<std::string>();
  metadata.c_version = cversion.value_or("c11");
  if (!std::ranges::contains(known_c_versions, metadata.c_version)) {
    return std::unexpected(std::format("Unsupported C standard version: '{}'",
                                       metadata.c_version));
  }

  extract_string_array(projectTable["authors"], metadata.authors);
  extract_string_array(projectTable["include_dirs"], metadata.include_dirs);
  auto depTable = projectTable["dependencies"];

  if (auto *arr = depTable.as_array()) {
    for (auto &&el : *arr) {
      if (auto val = el.value<std::string>()) {
        std::string depStr = *val;
        metadata.dependencies.push_back(depStr);

        if (isLocDep(depStr)) {
          fs::path dep_path = fs::path(m_manifest.base_dir) / depStr;
          Log::Info(
              std::format("Resolving local path dependency: '{}'", depStr));

          if (!fs::exists(dep_path)) {
            return std::unexpected(
                std::format("Local dependency directory not found: '{}'",
                            dep_path.string()));
          }

          fs::path canonical_path = fs::canonical(dep_path);
          Config depconfig(canonical_path.string(), ops);
          DependencySpec spec{depStr, ""};
          m_manifest.resolved_dependencies[depStr] =
              ResolvedDependency{spec, depconfig.getManifest()};

        } else if (isGitDep(depStr)) {
          return std::unexpected(
              "Direct 'git:' dependency URLs are no longer supported. Please "
              "register and refer to packages using the global registry.");
        } else {
          std::string pkgName = depStr;
          std::string pkgVersionSpec = "latest";
          size_t versionDelim = depStr.find('@');
          if (versionDelim != std::string::npos) {
            pkgName = depStr.substr(0, versionDelim);
            pkgVersionSpec = depStr.substr(versionDelim + 1);
          }

          fs::path globalMokai = getGlobalMokaiDir();
          fs::path registryDir = globalMokai / "registry";

          if (!fs::exists(registryDir)) {
            Log::Info("Initializing global registry clone...");
            fs::create_directories(registryDir.parent_path());
            std::string regCloneCmd =
                "git clone --depth=1 --progress "
                "https://github.com/L1TerminalFault/mokai_confs " +
                registryDir.string();
            if (std::system(regCloneCmd.c_str()) != 0) {
              return std::unexpected("Critical failure while downloading "
                                     "dependencies metadata maps "
                                     "from remote repository.");
            }
          }

          fs::path manifestRecipeFile = registryDir / (pkgName + ".toml");

          if (!fs::exists(manifestRecipeFile)) {
            Log::Info(std::format(
                "Package '{}' not found in local cache. Synchronizing index...",
                pkgName));
            std::string regCleanCmd = "git -C " + registryDir.string() +
                                      " reset --hard HEAD > /dev/null 2>&1";
            std::system(regCleanCmd.c_str());

            std::string regPullCmd =
                "git -C " + registryDir.string() + " pull --rebase --progress";
            std::system(regPullCmd.c_str());
          }

          if (fs::exists(manifestRecipeFile)) {
            std::ifstream rFile(manifestRecipeFile.string());
            std::stringstream rStream;
            rStream << rFile.rdbuf();

            try {
              auto rootRegistryNode = toml::parse(rStream.str());
              std::string gitRepo = rootRegistryNode["project"]["git_repo"]
                                        .value<std::string>()
                                        .value_or("");
              if (gitRepo.empty()) {
                return std::unexpected(std::format(
                    "Registry manifest for '{}' is missing required 'git_repo' "
                    "mapping.",
                    pkgName));
              }

              std::string packageGitUrl = gitRepo;
              if (!packageGitUrl.starts_with("http")) {
                packageGitUrl = "https://github.com/" + packageGitUrl;
              }

              std::string recipeTomlContent = "";
              if (auto *recipesTable = rootRegistryNode["recipes"].as_table()) {
                if (auto specNode = recipesTable->get(pkgVersionSpec)) {
                  recipeTomlContent =
                      specNode->value<std::string>().value_or("");
                } else if (auto latestNode = recipesTable->get("latest")) {
                  recipeTomlContent =
                      latestNode->value<std::string>().value_or("");
                  Log::Warn(std::format(
                      "Version '{}' not found for '{}'. Falling back to "
                      "'latest'.",
                      pkgVersionSpec, pkgName));
                }
              }

              if (recipeTomlContent.empty()) {
                return std::unexpected(std::format(
                    "Unable to resolve a valid build recipe for package '{}'.",
                    pkgName));
              }

              fs::path targetPkgBuildDir = globalMokai / "packages" / pkgName;
              fs::create_directories(targetPkgBuildDir.parent_path());

              if (!fs::exists(targetPkgBuildDir)) {
                Log::Info(std::format("Cloning global repository for '{}': {}",
                                      pkgName, packageGitUrl));
                std::string pkgCloneCmd = "git clone --progress " +
                                          packageGitUrl + " " +
                                          targetPkgBuildDir.string();
                if (std::system(pkgCloneCmd.c_str()) != 0) {
                  return std::unexpected(std::format(
                      "Git clone operation failed for package '{}'.", pkgName));
                }
              }

              fs::path destinationConfTarget = targetPkgBuildDir / "mokai.toml";
              std::ofstream outToml(destinationConfTarget.string());
              outToml << recipeTomlContent;
              outToml.close();

              Config depconfig(targetPkgBuildDir.string(), ops);
              DependencySpec spec{depStr, pkgVersionSpec};
              m_manifest.resolved_dependencies[pkgName] =
                  ResolvedDependency{spec, depconfig.getManifest()};

            } catch (const std::exception &e) {
              return std::unexpected(std::format(
                  "Registry metadata parsing failure: {}", e.what()));
            }
          } else {
            return std::unexpected(
                std::format("Package '{}' is not registered in the central "
                            "package registry.",
                            pkgName));
          }
        }
      }
    }
  }

  m_manifest.project = std::move(metadata);

  if (auto *options_table = m_config_toml["options"].as_table()) {
    for (auto &&[key, value] : *options_table) {
      if (auto val = value.value<bool>()) {
        m_manifest.options[std::string(key.str())] = *val;
      }
    }
  }

  if (auto *comp_table = m_config_toml["compatibility"].as_table()) {
    Compatibility comp;
    comp.min_cpp_version = (*comp_table)["min_cpp_version"].value_or("");
    comp.preferred_cpp_version =
        (*comp_table)["preferred_cpp_version"].value_or("");
    extract_string_array((*comp_table)["unsupported_cpp_versions"],
                         comp.unsupported_cpp_versions);
    extract_string_array((*comp_table)["compilers"]["supported"],
                         comp.compilers.supported);
    extract_string_array((*comp_table)["compilers"]["unsupported"],
                         comp.compilers.unsupported);
    m_manifest.compatibility = std::move(comp);
  }

  if (auto *fg_table = m_config_toml["file_group"].as_table()) {
    for (auto &&[key, value] : *fg_table) {
      if (auto *inner_table = value.as_table()) {
        FileGroup group;
        group.name = std::string(key.str());
        extract_string_array((*inner_table)["patterns"], group.patterns);
        m_manifest.file_groups.push_back(std::move(group));
      }
    }
  }

  if (auto *pg_table = m_config_toml["property_group"].as_table()) {
    for (auto &&[key, value] : *pg_table) {
      if (auto *inner_table = value.as_table()) {
        PropertyGroup group;
        group.name = std::string(key.str());
        extract_string_array((*inner_table)["defines"], group.defines);
        if (auto cond = (*inner_table)["condition"].value<std::string>()) {
          group.condition = std::move(*cond);
        }
        m_manifest.property_groups.push_back(std::move(group));
      }
    }
  }

  if (auto *target_table = m_config_toml["target"].as_table()) {
    for (auto &&[key, value] : *target_table) {
      if (auto *inner_table = value.as_table()) {
        Target target;
        target.name = std::string(key.str());

        std::string type_str = (*inner_table)["type"].value_or("");
        if (type_str == "executable") {
          target.type = TargetType::Executable;
        } else if (type_str == "static_library") {
          target.type = TargetType::StaticLibrary;
        } else if (type_str == "shared_library") {
          target.type = TargetType::SharedLibrary;
        } else {
          return std::unexpected(std::format(
              "Target '{}' specifies an invalid type. Supported types: "
              "'executable', 'static_library', 'shared_library'.",
              target.name));
        }

        extract_string_array((*inner_table)["sources"], target.sources);
        extract_string_array((*inner_table)["include_dirs"],
                             target.include_dirs);
        extract_string_array((*inner_table)["properties"], target.properties);
        extract_string_array((*inner_table)["flags"], target.flags);
        extract_string_array((*inner_table)["system_libs"], target.system_libs);
        extract_string_array((*inner_table)["depends_on"], target.depends_on);

        m_manifest.targets.push_back(std::move(target));
      }
    }
  }

  if (auto *hook_table = m_config_toml["hook"].as_table()) {
    for (auto &&[key, value] : *hook_table) {
      if (auto *inner_table = value.as_table()) {
        Hook hook;
        hook.name = std::string(key.str());

        std::string trigger_str = (*inner_table)["on"].value_or("");
        if (trigger_str == "pre_build") {
          hook.trigger = HookTrigger::PreBuild;
        } else if (trigger_str == "post_build") {
          hook.trigger = HookTrigger::PostBuild;
        } else if (trigger_str == "pre_target_build") {
          hook.trigger = HookTrigger::PreTargetBuild;
        } else if (trigger_str == "post_target_build") {
          hook.trigger = HookTrigger::PostTargetBuild;
        } else if (trigger_str == "file_change") {
          hook.trigger = HookTrigger::FileChange;
        } else {
          return std::unexpected(
              std::format("Hook '{}' specifies an invalid 'on' trigger action.",
                          hook.name));
        }

        hook.run = (*inner_table)["run"].value_or("");
        if (hook.run.empty()) {
          return std::unexpected(std::format(
              "Hook '{}' is missing the required 'run' command parameter.",
              hook.name));
        }

        if (auto tgt = (*inner_table)["target"].value<std::string>()) {
          hook.target = std::move(*tgt);
        }
        if (auto pat = (*inner_table)["pattern"].value<std::string>()) {
          hook.pattern = std::move(*pat);
        }

        m_manifest.hooks.push_back(std::move(hook));
      }
    }
  }

  if (auto *exports_table = m_config_toml["exports"].as_table()) {
    Exports exp;
    extract_string_array((*exports_table)["default_targets"],
                         exp.default_targets);
    if (exp.default_targets.empty()) {
      return std::unexpected(
          "The [exports] block layout requires the 'default_targets' list to "
          "be specified.");
    }

    extract_string_array((*exports_table)["include_dirs"], exp.include_dirs);
    extract_string_array((*exports_table)["defines_required"],
                         exp.defines_required);
    extract_string_array((*exports_table)["defines_optional"],
                         exp.defines_optional);

    if (auto *profile_table = (*exports_table)["profile"].as_table()) {
      for (auto &&[key, value] : *profile_table) {
        if (auto *inner_table = value.as_table()) {
          ExportProfile profile;
          extract_string_array((*inner_table)["targets"], profile.targets);
          exp.profiles[std::string(key.str())] = std::move(profile);
        }
      }
    }
    m_manifest.exports = std::move(exp);
  }

  toml::node_view output_view = m_config_toml["output"];
  m_manifest.output.directory = output_view["directory"].value_or("./build");

  if (auto *configs_table = m_config_toml["output"]["configs"].as_table()) {
    for (auto &&[key, value] : *configs_table) {
      if (auto *cfg_table = value.as_table()) {
        OutputProfile profile;
        profile.enabled = (*cfg_table)["enabled"].value_or(true);
        profile.subdir = (*cfg_table)["subdir"].value_or("");
        m_manifest.output.configs[std::string(key.str())] = std::move(profile);
      }
    }
  }

  return {};
}

bool Config::createProject(const std::string &projectName) {
  std::string chosenName = projectName;
  if (chosenName.empty()) {
    std::cout << "Enter project name [untitled_project]: ";
    std::getline(std::cin, chosenName);
    if (chosenName.empty()) {
      chosenName = "untitled_project";
    }
  }

  fs::path projectRoot = fs::current_path() / chosenName;
  if (fs::exists(projectRoot)) {
    Log::Error(
        std::format("Target directory '{}' already exists.", chosenName));
    return false;
  }

  try {
    fs::create_directories(projectRoot / "src");

    std::ofstream tomlFile(projectRoot / "mokai.toml");
    tomlFile << "[project]\n"
             << "name = \"" << chosenName << "\"\n"
             << "version = \"0.1.0\"\n"
             << "cpp_version = \"c++23\"\n\n"
             << "[target." << chosenName << "]\n"
             << "type = \"executable\"\n"
             << "sources = [\"src/main.cpp\"]\n";

    std::ofstream mainFile(projectRoot / "src" / "main.cpp");
    mainFile << "#include <iostream>\n\n"
             << "int main() {\n"
             << "    std::cout << \"Hello from " << chosenName << "!\\n\";\n"
             << "    return 0;\n"
             << "}\n";

    Log::Success(std::format("Scaffolded new project workspace at: {}",
                             projectRoot.string()));
    return true;
  } catch (const std::exception &e) {
    Log::Error(
        std::format("Failed to generate project skeleton: {}", e.what()));
    return false;
  }
}

bool Config::runTarget(const std::string &targetName) {
  const Target *matchedTarget = nullptr;
  for (const auto &t : m_manifest.targets) {
    if (t.name == targetName) {
      matchedTarget = &t;
      break;
    }
  }

  if (!matchedTarget) {
    std::string hint =
        "Verify target configurations defined in your mokai.toml.";
    std::string best_match;
    size_t min_dist = 4;
    for (const auto &t : m_manifest.targets) {
      size_t dist = calculateDistance(t.name, targetName);
      if (dist < min_dist) {
        min_dist = dist;
        best_match = t.name;
      }
    }
    if (!best_match.empty()) {
      hint = std::format("Did you mean target '{}'?", best_match);
    }

    Log::Error(
        std::format("Target '{}' not found.\nHint: {}", targetName, hint));
    return false;
  }

  if (matchedTarget->type != TargetType::Executable) {
    Log::Error(
        std::format("Target '{}' is not an executable type.", targetName));
    return false;
  }

  fs::path absoluteOutputBinPath =
      fs::path(m_manifest.base_dir) / m_manifest.output.directory;

  std::string profileSubdir = "";
  for (const auto &[key, profile] : m_manifest.output.configs) {
    if (profile.enabled) {
      profileSubdir = profile.subdir;
      break;
    }
  }
  if (!profileSubdir.empty()) {
    absoluteOutputBinPath /= profileSubdir;
  }
  absoluteOutputBinPath /= targetName;

  if (!fs::exists(absoluteOutputBinPath)) {
    Log::Error(std::format(
        "Executable binary not found at: '{}'\nHint: Build the target before "
        "executing it.",
        absoluteOutputBinPath.string()));
    return false;
  }

  Log::Info(std::format("Executing target '{}' ({})", targetName,
                        absoluteOutputBinPath.string()));

  int exitStatus = std::system(absoluteOutputBinPath.string().c_str());

  if (exitStatus == -1) {
    Log::Error(std::format(
        "Process execution terminated with error on target: '{}'", targetName));
    return false;
  }

#if defined(_WIN32) || defined(_WIN64)
  return exitStatus == 0;
#else
  return WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0;
#endif
}

bool Config::isGitDep(std::string &str) { return str.starts_with("git:"); }
bool Config::isLocDep(std::string &str) {
  return str.starts_with("./") || str.starts_with("../");
}
bool Config::checkIsFileAndExists(const std::string &path) {
  return fs::exists(path) && fs::is_regular_file(path);
}
bool Config::checkIsFolderAndExists(const std::string &path) {
  return fs::exists(path) && fs::is_directory(path);
}

FuzzyFindResult Config::fuzzyFindCloseFile(std::string &path) {
  fs::path p(path);
  fs::path parent = p.has_parent_path() ? p.parent_path() : fs::current_path();
  std::string filename = p.filename().string();

  if (!fs::exists(parent)) {
    return {false, ""};
  }

  std::string best_match;
  size_t min_dist = 4;
  try {
    for (const auto &entry : fs::directory_iterator(parent)) {
      if (entry.is_regular_file()) {
        std::string entryName = entry.path().filename().string();
        size_t dist = calculateDistance(entryName, filename);
        if (dist < min_dist && dist > 0) {
          min_dist = dist;
          best_match = entry.path().string();
        }
      }
    }
  } catch (...) {
  }

  if (!best_match.empty()) {
    return {true, best_match};
  }
  return {false, ""};
}

FuzzyFindResult Config::fuzzyFindCloseFolder(std::string &path) {
  fs::path p(path);
  fs::path parent = p.has_parent_path() ? p.parent_path() : fs::current_path();
  std::string folderName = p.filename().string();

  if (!fs::exists(parent)) {
    return {false, ""};
  }

  std::string best_match;
  size_t min_dist = 4;
  try {
    for (const auto &entry : fs::directory_iterator(parent)) {
      if (entry.is_directory()) {
        std::string entryName = entry.path().filename().string();
        size_t dist = calculateDistance(entryName, folderName);
        if (dist < min_dist && dist > 0) {
          min_dist = dist;
          best_match = entry.path().string();
        }
      }
    }
  } catch (...) {
  }

  if (!best_match.empty()) {
    return {true, best_match};
  }
  return {false, ""};
}

} // namespace mokai
