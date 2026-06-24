#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct TemplateFile {
  std::string relative_path; // e.g., "mokai.toml" or "src/main.cpp"
  std::string_view content;  // The actual raw template string literal
};

struct ProjectTemplate {
  std::string name;
  std::string description;
  std::vector<TemplateFile> files;
};

class TemplateGen {
public:
  TemplateGen();

  // Returns a list of all available template names and descriptions
  std::vector<std::pair<std::string, std::string>>
  getAvailableTemplates() const;

  // Generates the selected template into the target path, replacing
  // placeholders
  bool create(const std::string &template_name, const fs::path &output_dir,
              const std::string &project_name, const std::string &cpp_version);

private:
  std::unordered_map<std::string, ProjectTemplate> m_templates;

  void registerTemplates();
  std::string replacePlaceholders(std::string_view source,
                                  const std::string &project_name,
                                  const std::string &cpp_version);
};
