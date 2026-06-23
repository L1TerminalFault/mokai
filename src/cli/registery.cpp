#include "cli.hpp"

namespace mokai {
void Cli::initCommands() {
  m_supported_commands = {
      {"create",
       {CREATEUSEANDEXPLANATION,
        [this](auto &args) { handleCreateProject(args); }}},
      {"add",
       {ADDUSEANDEXPLANATION, [this](auto &args) { handlePackageAdd(args); }}},
      {"build",
       {BUILDUSEANDEXPLANATION, [this](auto &args) { handleBuild(args); }}}};
};
} // namespace mokai
