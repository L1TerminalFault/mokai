#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tom {

enum class Type : uint8_t { String, Bool, Number, Array };

struct Field {
  std::string scope;
  std::string key;
  std::string value;
  Type type;
};

inline std::string unescape_string(std::string_view str) {
  std::string result;
  result.reserve(str.size());
  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\\' && i + 1 < str.size()) {
      char next = str[i + 1];
      if (next == '\\') {
        result += '\\';
        ++i;
      } else if (next == '"') {
        result += '"';
        ++i;
      } else if (next == 'n') {
        result += '\n';
        ++i;
      } else if (next == 't') {
        result += '\t';
        ++i;
      } else {
        result += str[i];
      }
    } else {
      result += str[i];
    }
  }
  return result;
}

inline std::string_view trim(std::string_view str) {
  auto start = str.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos)
    return "";
  auto end = str.find_last_not_of(" \t\r\n");
  std::string_view res = str.substr(start, end - start + 1);

  if (res.size() >= 2) {
    if ((res.front() == '"' && res.back() == '"') ||
        (res.front() == '\'' && res.back() == '\'')) {
      res.remove_prefix(1);
      res.remove_suffix(1);
    }
  }
  return res;
}

inline std::string_view strip_comments(std::string_view line) {
  bool in_quotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
      in_quotes = !in_quotes;
    }
    if (line[i] == '#' && !in_quotes) {
      return line.substr(0, i);
    }
  }
  return line;
}

struct Parser {
  std::vector<Field> fields;

  void parse(std::string_view content) {
    std::string current_scope = "";
    size_t pos = 0;

    int group_idx = 0;
    int block_idx = 0;
    int hook_idx = 0;

    std::string accumulator = "";
    bool in_multiline = false;
    char opening_char = 0;

    while (pos < content.size()) {
      size_t next_newline = content.find('\n', pos);
      if (next_newline == std::string_view::npos) {
        next_newline = content.size();
      }

      std::string_view raw_line = content.substr(pos, next_newline - pos);
      pos = next_newline + 1;

      std::string_view line = strip_comments(raw_line);
      auto trimmed_line = trim(line);
      if (trimmed_line.empty())
        continue;

      if (!in_multiline) {
        size_t eq_pos = trimmed_line.find('=');
        if (eq_pos != std::string_view::npos) {
          std::string_view right_side = trim(trimmed_line.substr(eq_pos + 1));
          if (!right_side.empty() &&
              (right_side.front() == '[' || right_side.front() == '{')) {
            char closing = (right_side.front() == '[') ? ']' : '}';
            if (right_side.back() != closing) {
              in_multiline = true;
              opening_char = right_side.front();
              accumulator = std::string(trimmed_line);
              continue;
            }
          }
        }
      } else {
        accumulator += " " + std::string(trimmed_line);
        char closing_char = (opening_char == '[') ? ']' : '}';
        if (trimmed_line.back() == closing_char ||
            trimmed_line.find(closing_char) != std::string_view::npos) {
          in_multiline = false;
          trimmed_line = accumulator;
        } else {
          continue;
        }
      }

      auto block_statement = trimmed_line;

      if (block_statement.front() == '[' && block_statement.back() == ']') {
        if (block_statement.size() > 4 && block_statement[1] == '[' &&
            block_statement[block_statement.size() - 2] == ']') {
          std::string_view raw_scope =
              block_statement.substr(2, block_statement.size() - 4);

          if (raw_scope == "group") {
            current_scope = "group[" + std::to_string(group_idx++) + "]";
          } else if (raw_scope == "block") {
            current_scope = "block[" + std::to_string(block_idx++) + "]";
          } else if (raw_scope == "hook") {
            current_scope = "hook[" + std::to_string(hook_idx++) + "]";
          } else {
            current_scope = std::string(raw_scope);
          }
        } else {
          current_scope = std::string(
              block_statement.substr(1, block_statement.size() - 2));
        }
        continue;
      }

      if (size_t eq = block_statement.find('='); eq != std::string_view::npos) {
        std::string key = std::string(trim(block_statement.substr(0, eq)));
        std::string_view val = trim(block_statement.substr(eq + 1));

        if (val.front() == '{' && val.back() == '}') {
          std::string inline_scope =
              current_scope.empty() ? key : current_scope + "." + key;
          std::string_view inner = val.substr(1, val.size() - 2);

          size_t pair_pos = 0;
          while (pair_pos < inner.size()) {
            size_t next_comma = inner.find(',', pair_pos);
            if (next_comma == std::string_view::npos)
              next_comma = inner.size();

            std::string_view pair =
                trim(inner.substr(pair_pos, next_comma - pair_pos));
            if (size_t inner_eq = pair.find('=');
                inner_eq != std::string_view::npos) {
              std::string inner_key =
                  std::string(trim(pair.substr(0, inner_eq)));
              std::string_view inner_val = trim(pair.substr(inner_eq + 1));
              fields.push_back({inline_scope, inner_key,
                                unescape_string(inner_val), Type::String});
            }
            pair_pos = next_comma + 1;
          }
        } else if (val.front() == '[' && val.back() == ']') {
          std::string_view arr_content = val.substr(1, val.size() - 2);
          size_t item_pos = 0;
          while (item_pos < arr_content.size()) {
            size_t next_comma = arr_content.find(',', item_pos);
            if (next_comma == std::string_view::npos)
              next_comma = arr_content.size();

            std::string_view raw_item =
                trim(arr_content.substr(item_pos, next_comma - item_pos));
            if (!raw_item.empty()) {
              fields.push_back(
                  {current_scope, key, unescape_string(raw_item), Type::Array});
            }
            item_pos = next_comma + 1;
          }
        } else {
          Type t =
              (val == "true" || val == "false") ? Type::Bool : Type::String;
          fields.push_back({current_scope, key, unescape_string(val), t});
        }
      }
    }
  }

  std::string_view find_value(std::string_view scope,
                              std::string_view key) const {
    for (const auto &field : fields) {
      if (field.scope == scope && field.key == key) {
        return field.value;
      }
    }
    return "";
  }
};
} // namespace tom
