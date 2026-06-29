#pragma once

#include "icompiler.hpp"
#include <expected>
#include <memory>
#include <string>

namespace mokai {
class ToolchainFinder {
public:
  ToolchainFinder() = default;

  std::expected<std::unique_ptr<ICompiler>, std::string>
  discover(const std::string &user_pref);

private:
  std::string findBinary(const std::string &name) const;
  std::string findUnixBinary(const std::string &name) const;
  std::string findWindowsBinary(const std::string &name) const;
};
} // namespace mokai
