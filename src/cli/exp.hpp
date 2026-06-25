
namespace mokai {
#define CREATEUSEANDEXPLANATION                                                \
  "mokai create [projectName]",                                                \
      "Initializes a new C++ project. The command initiates an "               \
      "interactive "                                                           \
      "CLI prompt, asking a series of configuration questions (such as "       \
      "build tool, "                                                           \
      "C++ standard, and dependency preferences) to scaffold a tailored "      \
      "project structure."

#define ADDUSEANDEXPLANATION                                                   \
  "mokai add [packageName]",                                                   \
      "Installs and integrates a specific package into your project "          \
      "configuration. "                                                        \
      "If the package is not found locally, it resolves dependencies "         \
      "through our registry. "                                                 \
      "Note: A centralized package repository website is currently under "     \
      "active "                                                                \
      "development and will be available in the near future."

#define BUILDUSEANDEXPLANATION                                                 \
  "mokai build [path]", "Compiles the project files. The command "             \
                        "accepts an optional directory path as an "            \
                        "argument. If a path is provided (e.g., "              \
                        "'mokai Build /src/core'), it builds that "            \
                        "specific directory. If the path is omitted, "         \
                        "it defaults to the current working "                  \
                        "directory ('.')."
#define RUNUSEANDEXPLANATION                                                   \
  "mokai run [target]",                                                        \
      "executes the executable you are refering too or finds the most likely " \
      "executable to run and runs that if target is not specified "

} // namespace mokai
