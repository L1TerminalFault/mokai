#include "cli.hpp"

namespace mokai {
void cli::initCommands() {
  supportedCommands = {
      {"create",
       {CREATEUSEANDEXPLANATION,
        [this](auto &args) { handleCreateProject(args); }}},
      {"add",
       {ADDUSEANDEXPLANATION, [this](auto &args) { handlePackageAdd(args); }}},
      {"build",
       {BUILDUSEANDEXPLANATION, [this](auto &args) { handleBuild(args); }}}};
};
} // namespace mokai
