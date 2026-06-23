#include "graph.hpp"
#include <regex>

namespace mokai {

// Helper struct isolated to this translation unit for condition parsing
struct CondToken {
  std::string key;
  std::string op;
  std::string value;
};

// Internal parsing helper (keeps implementation details out of the header)
static CondToken parseConditionString(const std::string &expr) {
  // Matches expressions like: os == "linux" or compiler == "clang++"
  std::regex re(R"(([a-zA-Z0-9_\-]+)\s*(==|!=)\s*\"([^\"]*)\")");
  std::smatch match;
  if (std::regex_search(expr, match, re) && match.size() == 4) {
    return {match[1].str(), match[2].str(), match[3].str()};
  }
  return {};
}

bool Graph::evaluateCond(const std::string &cond_expr) {
  if (cond_expr.empty()) {
    return true; // No condition means always evaluate to true
  }

  CondToken token = parseConditionString(cond_expr);
  if (token.key.empty()) {
    m_logger.Error(
        "Mokai Syntax Error: Invalid condition expression format: '" +
        cond_expr + "'");
    return false;
  }

  // Context Evaluation Engine
  std::string context_value = "";

  if (token.key == "os") {
#if defined(__linux__)
    context_value = "linux";
#elif defined(__APPLE__)
    context_value = "macos";
#else
    context_value = "windows";
#endif
  } else if (token.key == "compiler") {
    // Tie this directly into your discoverToolchain() data later
    context_value = "clang++";
  } else {
    m_logger.Warn(
        "Evaluation Warning: Unknown conditional variable identifier: '" +
        token.key + "'");
    return false;
  }

  // Process evaluation operators securely (Fixed trailing space bug here)
  if (token.op == "==") {
    return context_value == token.value;
  } else if (token.op == "!=") {
    return context_value != token.value;
  }

  return false;
}

} // namespace mokai
