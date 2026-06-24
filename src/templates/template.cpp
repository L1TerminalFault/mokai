#include "template.hpp"
#include <fstream>
#include <iostream>

constexpr std::string_view BASIC_MOKAI_TOML = R"([project]
name = "{{PROJECT_NAME}}"
cpp_version = "{{CPP_VERSION}}"

[target.{{PROJECT_NAME}}]
type = "executable"
sources = ["src/main.cpp"]
)";

constexpr std::string_view BASIC_MAIN_CPP = R"(#include <print>

int main() {
    std::println("Hello World");
    return 0;
}
)";

TemplateGen::TemplateGen() { registerTemplates(); }

void TemplateGen::registerTemplates() {
  m_templates["basic"] = ProjectTemplate{
      .name = "basic",
      .description =
          "A minimal, modern C++ executable scaffolding with std::print",
      .files = {{"mokai.toml", BASIC_MOKAI_TOML},
                {"src/main.cpp", BASIC_MAIN_CPP}}};
}

std::vector<std::pair<std::string, std::string>>
TemplateGen::getAvailableTemplates() const {
  std::vector<std::pair<std::string, std::string>> list;
  for (const auto &[name, tmpl] : m_templates) {
    list.push_back({name, tmpl.description});
  }
  return list;
}

std::string TemplateGen::replacePlaceholders(std::string_view source,
                                             const std::string &project_name,
                                             const std::string &cpp_version) {
  std::string result(source);

  size_t pos = 0;
  while ((pos = result.find("{{PROJECT_NAME}}", pos)) != std::string::npos) {
    result.replace(pos, 16, project_name);
    pos += project_name.length();
  }

  pos = 0;
  while ((pos = result.find("{{CPP_VERSION}}", pos)) != std::string::npos) {
    result.replace(pos, 15, cpp_version);
    pos += cpp_version.length();
  }

  return result;
}

bool TemplateGen::create(const std::string &template_name,
                         const fs::path &output_dir,
                         const std::string &project_name,
                         const std::string &cpp_version) {
  auto it = m_templates.find(template_name);
  if (it == m_templates.end()) {
    std::cerr << "Error: Unknown scaffolding template '" << template_name
              << "'\n";
    return false;
  }

  const auto &project_template = it->second;

  for (const auto &file : project_template.files) {
    fs::path target_file_path = output_dir / file.relative_path;

    // Ensure the subdirectories (like src/) exist safely
    fs::create_directories(target_file_path.parent_path());

    std::ofstream out(target_file_path);
    if (!out.is_open()) {
      std::cerr << "Error: Failed to write scaffolding file to "
                << target_file_path << "\n";
      return false;
    }

    std::string processed_content =
        replacePlaceholders(file.content, project_name, cpp_version);
    out << processed_content;
    out.close();
  }

  return true;
}
