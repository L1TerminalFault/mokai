#include "config.hpp"
#include <filesystem>
#include <string>

namespace mokai {
bool Config::checkIsFileAndExists(const std::string &path) {
  return (std::filesystem::exists(path) &&
          !std::filesystem::is_directory(path));
}
bool Config::checkIsFolderAndExists(const std::string &path) {
  return (std::filesystem::exists(path) && std::filesystem::is_directory(path));
}
FuzzyFindResult fuzzyFindCloseFile(std::string &path) {}
FuzzyFindResult fuzzyFindCloseFolder(std::string &path) {};

} // namespace mokai
