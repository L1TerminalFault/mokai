#include "graph.hpp"
#include "cli/cli.hpp"
#include "graph/types.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace mokai {

static int executeCommandFast(const std::vector<std::string> &args) {
  if (args.empty())
    return -1;
#if defined(_WIN32) || defined(_WIN64)
  size_t total_len = 0;
  for (const auto &a : args)
    total_len += a.size() + 1;
  std::string command;
  command.reserve(total_len);
  for (size_t i = 0; i < args.size(); ++i) {
    command += args[i];
    if (i + 1 < args.size())
      command += " ";
  }
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  std::vector<char> cmd_buffer(command.begin(), command.end());
  cmd_buffer.push_back('\0');
  if (CreateProcessA(NULL, cmd_buffer.data(), NULL, NULL, FALSE, 0, NULL, NULL,
                     &si, &pi)) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
  }
  return -1;
#else
  std::vector<char *> c_args;
  c_args.reserve(args.size() + 1);
  for (const auto &arg : args) {
    c_args.push_back(const_cast<char *>(arg.c_str()));
  }
  c_args.push_back(nullptr);
  pid_t pid;
  int status;
  if (posix_spawnp(&pid, c_args[0], NULL, NULL, c_args.data(), environ) == 0) {
    if (waitpid(pid, &status, 0) != -1) {
      if (WIFEXITED(status))
        return WEXITSTATUS(status);
    }
  }
  return -1;
#endif
}

struct Toolchain {
  std::string cpp_compiler, c_compiler, archiver;
  bool is_msvc = false;
};

static Toolchain discoverToolchain() {
  static Toolchain cached_toolchain;
  static bool discovered = false;
  if (!discovered) {
#if defined(_WIN32) || defined(_WIN64)
    if (std::system("where cl > NUL 2>&1") == 0) {
      cached_toolchain = {"cl", "cl", "lib", true};
    } else if (std::system("where clang++ > NUL 2>&1") == 0) {
      cached_toolchain = {"clang++", "clang", "llvm-ar", false};
    } else {
      cached_toolchain = {"g++", "gcc", "ar", false};
    }
#else
    if (std::system("which clang++ > /dev/null 2>&1") == 0) {
      cached_toolchain = {"clang++", "clang", "llvm-ar", false};
    } else {
      cached_toolchain = {"g++", "gcc", "ar", false};
    }
#endif
    discovered = true;
  }
  return cached_toolchain;
}

static std::string escapeJsonString(const std::string &input) {
  std::string output;
  for (char c : input) {
    if (c == '\\' || c == '"')
      output += '\\';
    output += c;
  }
  return output;
}

static std::string triggerToString(HookTrigger trigger) {
  switch (trigger) {
  case HookTrigger::PreBuild:
    return "pre_build";
  case HookTrigger::PostBuild:
    return "post_build";
  case HookTrigger::PreTargetBuild:
    return "pre_target_build";
  case HookTrigger::PostTargetBuild:
    return "post_target_build";
  case HookTrigger::FileChange:
    return "file_change";
  default:
    return "unknown";
  }
}

static std::string targetTypeToString(TargetType type) {
  switch (type) {
  case TargetType::Executable:
    return "executable";
  case TargetType::StaticLibrary:
    return "static_library";
  case TargetType::SharedLibrary:
    return "shared_library";
  default:
    return "unknown";
  }
}

const QualifiedTarget *
Graph::FindByQualifiedName(const std::string &qualified_name) const {
  for (const auto &qt : m_allTargets) {
    if (qt.qualifiedName == qualified_name)
      return &qt;
  }
  return nullptr;
}

void Graph::collectTransitive(const std::string &node,
                              std::unordered_set<std::string> &visited,
                              std::vector<std::string> &out_libs) {
  const QualifiedTarget *qt = FindByQualifiedName(node);
  if (!qt)
    return;
  for (const auto &raw_dep : qt->target.depends_on) {
    auto resolved = resolveDependsOnEntry(raw_dep, *qt);
    for (const auto &dep_name : resolved) {
      if (visited.find(dep_name) == visited.end()) {
        visited.insert(dep_name);
        collectTransitive(dep_name, visited, out_libs);
        out_libs.push_back(dep_name);
      }
    }
  }
}

std::vector<std::string>
Graph::getTransitiveDependencies(const std::string &qualified_name) {
  std::unordered_set<std::string> visited;
  std::vector<std::string> out_libs;
  collectTransitive(qualified_name, visited, out_libs);
  return out_libs;
}

std::vector<QualifiedTarget>
Graph::flattenManifestTree(std::shared_ptr<mokai::ProjectManifest> manifest,
                           const std::string &path_prefix) {
  std::vector<QualifiedTarget> result;
  for (auto &target : manifest->targets) {
    result.push_back({path_prefix + "::" + target.name, target, manifest});
  }
  for (auto &[dep_key, resolved] : manifest->resolved_dependencies) {
    if (resolved.manifest) {
      std::string child_prefix =
          path_prefix + "." + resolved.manifest->project.name;
      auto child_result = flattenManifestTree(resolved.manifest, child_prefix);
      result.insert(result.end(), child_result.begin(), child_result.end());
    }
  }
  return result;
}

static std::shared_ptr<ProjectManifest> FindResolvedDependency(
    const std::unordered_map<std::string, ResolvedDependency> &deps,
    const std::string &name_or_key) {
  auto to_lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
  };
  std::string target_name = to_lower(name_or_key);
  for (const auto &[key, resolved] : deps) {
    size_t at_pos = key.find('@');
    std::string base_key =
        to_lower((at_pos == std::string::npos) ? key : key.substr(0, at_pos));
    if (base_key == target_name ||
        (resolved.manifest &&
         to_lower(resolved.manifest->project.name) == target_name))
      return resolved.manifest;
  }
  return nullptr;
}

static const QualifiedTarget *
FindSiblingByName(const std::vector<QualifiedTarget> &all,
                  const QualifiedTarget &from, const std::string &name) {
  for (auto &qt : all) {
    if (qt.manifest == from.manifest && qt.target.name == name)
      return &qt;
  }
  return nullptr;
}

static bool SplitExplicitTarget(const std::string &raw, std::string &pkg,
                                std::string &target) {
  auto colon = raw.find(':');
  if (colon == std::string::npos)
    return false;
  pkg = raw.substr(0, colon);
  target = raw.substr(colon + 1);
  return true;
}

static bool SplitProfile(const std::string &raw, std::string &pkg,
                         std::string &profile) {
  auto slash = raw.find('/');
  if (slash == std::string::npos)
    return false;
  pkg = raw.substr(0, slash);
  profile = raw.substr(slash + 1);
  return true;
}

std::vector<std::string>
Graph::resolveDependsOnEntry(const std::string &raw_dep,
                             const QualifiedTarget &from_target) {
  std::vector<std::string> resolved;
  std::string pkg, explicit_target;
  if (SplitExplicitTarget(raw_dep, pkg, explicit_target)) {
    auto dep_manifest = FindResolvedDependency(
        from_target.manifest->resolved_dependencies, pkg);
    if (!dep_manifest) {
      m_logger.Error("Target '" + from_target.qualifiedName +
                     "' depends on unknown package '" + pkg + "'");
      return resolved;
    }
    bool found = false;
    for (auto &qt : m_allTargets) {
      if (qt.manifest == dep_manifest && qt.target.name == explicit_target) {
        resolved.push_back(qt.qualifiedName);
        found = true;
        break;
      }
    }
    if (!found)
      m_logger.Error("Package '" + pkg + "' has no target named '" +
                     explicit_target + "'");
    return resolved;
  }
  std::string pPkg, pName;
  if (SplitProfile(raw_dep, pPkg, pName)) {
    auto dep_manifest = FindResolvedDependency(
        from_target.manifest->resolved_dependencies, pPkg);
    if (!dep_manifest) {
      m_logger.Error("Target '" + from_target.qualifiedName +
                     "' depends on unknown package '" + pPkg + "'");
      return resolved;
    }
    if (!dep_manifest->exports || !dep_manifest->exports->profiles.count(pName))
      return resolved;
    for (auto &target_name :
         dep_manifest->exports->profiles.at(pName).targets) {
      for (auto &qt : m_allTargets) {
        if (qt.manifest == dep_manifest && qt.target.name == target_name)
          resolved.push_back(qt.qualifiedName);
      }
    }
    return resolved;
  }
  if (auto *sibling = FindSiblingByName(m_allTargets, from_target, raw_dep)) {
    resolved.push_back(sibling->qualifiedName);
    return resolved;
  }
  for (const auto &[dep_key, resolved_dep] :
       from_target.manifest->resolved_dependencies) {
    if (resolved_dep.manifest) {
      for (auto &qt : m_allTargets) {
        if (qt.manifest == resolved_dep.manifest && qt.target.name == raw_dep)
          resolved.push_back(qt.qualifiedName);
      }
    }
  }
  if (!resolved.empty())
    return resolved;
  auto dep_manifest = FindResolvedDependency(
      from_target.manifest->resolved_dependencies, raw_dep);
  if (!dep_manifest || !dep_manifest->exports)
    return resolved;
  for (auto &target_name : dep_manifest->exports->default_targets) {
    for (auto &qt : m_allTargets) {
      if (qt.manifest == dep_manifest && qt.target.name == target_name)
        resolved.push_back(qt.qualifiedName);
    }
  }
  return resolved;
}

std::vector<GraphEdge> Graph::buildEdges() {
  std::vector<GraphEdge> edges;
  for (auto &qt : m_allTargets) {
    for (auto &raw_dep : qt.target.depends_on) {
      auto resolved_targets = resolveDependsOnEntry(raw_dep, qt);
      for (auto &to_name : resolved_targets)
        edges.push_back({qt.qualifiedName, to_name});
    }
  }
  return edges;
}

std::vector<std::string>
Graph::computeBuildOrder(const std::vector<GraphEdge> &edges) {
  std::vector<std::string> build_order;
  std::unordered_map<std::string, std::vector<std::string>> adj;
  for (const auto &edge : edges)
    adj[edge.from].push_back(edge.to);
  std::unordered_map<std::string, NodeState> states;
  for (const auto &qt : m_allTargets)
    states[qt.qualifiedName] = NodeState::Unvisited;
  std::vector<std::string> active_path;
  auto dfs = [&](auto &self, const std::string &node) -> bool {
    states[node] = NodeState::Visiting;
    active_path.push_back(node);
    if (adj.count(node)) {
      for (const auto &dependency : adj[node]) {
        if (!states.count(dependency))
          continue;
        if (states[dependency] == NodeState::Visiting) {
          m_logger.Error("Dependency Cycle Detected!");
          return false;
        }
        if (states[dependency] == NodeState::Unvisited &&
            !self(self, dependency))
          return false;
      }
    }
    states[node] = NodeState::Done;
    active_path.pop_back();
    build_order.push_back(node);
    return true;
  };
  for (const auto &qt : m_allTargets) {
    if (states[qt.qualifiedName] == NodeState::Unvisited &&
        !dfs(dfs, qt.qualifiedName))
      return {};
  }
  return build_order;
}

std::vector<std::string>
Graph::expandBraceNotation(const std::string &pattern) {
  std::vector<std::string> results;
  size_t start = pattern.find('{');
  size_t end = pattern.find('}', start);
  if (start == std::string::npos || end == std::string::npos) {
    results.push_back(pattern);
    return results;
  }
  std::string prefix = pattern.substr(0, start),
              suffix = pattern.substr(end + 1),
              options_raw = pattern.substr(start + 1, end - start - 1);
  std::stringstream ss(options_raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    std::string expanded = prefix + token + suffix;
    auto sub_expanded = expandBraceNotation(expanded);
    results.insert(results.end(), sub_expanded.begin(), sub_expanded.end());
  }
  return results;
}

void Graph::matchGlobPattern(const std::string &pattern,
                             const std::string &base_dir,
                             std::vector<std::string> &matches) {
  auto expanded_patterns = expandBraceNotation(pattern);
  if (expanded_patterns.size() > 1) {
    for (const auto &p : expanded_patterns)
      matchGlobPattern(p, base_dir, matches);
    return;
  }
  std::string target_pattern = pattern;
  if (target_pattern.rfind("./", 0) == 0)
    target_pattern = target_pattern.substr(2);
  std::replace(target_pattern.begin(), target_pattern.end(), '\\', '/');
  std::string regex_str = "^";
  for (size_t i = 0; i < target_pattern.length(); ++i) {
    char c = target_pattern[i];
    if (c == '*' && i + 1 < target_pattern.length() &&
        target_pattern[i + 1] == '*') {
      regex_str += ".*";
      i++;
      if (i + 1 < target_pattern.length() && target_pattern[i + 1] == '/')
        i++;
    } else if (c == '*')
      regex_str += "[^/]*";
    else if (c == '?')
      regex_str += "[^/]";
    else if (strchr(".+^$()[]|", c)) {
      regex_str += '\\';
      regex_str += c;
    } else
      regex_str += c;
  }
  regex_str += "$";
  std::regex filter(regex_str);
  std::string search_root = base_dir.empty() ? "." : base_dir;
  auto normalize_and_add = [&](const fs::path &path) {
    std::string rel_path = fs::relative(path, search_root).string();
    std::replace(rel_path.begin(), rel_path.end(), '\\', '/');
    if (std::regex_match(rel_path, filter))
      matches.push_back(fs::path(path).lexically_normal().string());
  };
  if (target_pattern.find("**") != std::string::npos) {
    for (auto it = fs::recursive_directory_iterator(search_root);
         it != fs::recursive_directory_iterator(); ++it) {
      std::string dn = it->path().filename().string();
      if (dn == "build" || dn == ".git" || dn == ".mokai") {
        it.disable_recursion_pending();
        continue;
      }
      if (fs::is_regular_file(*it))
        normalize_and_add(it->path());
    }
  } else {
    for (const auto &entry : fs::directory_iterator(search_root)) {
      if (fs::is_regular_file(entry))
        normalize_and_add(entry.path());
    }
  }
}

bool Graph::evaluateConditionExpression(
    const std::string &condition, const Target &target,
    const std::shared_ptr<ProjectManifest> &manifest) {
  if (condition.empty())
    return true;
  std::regex tt_regex(R"(target_type\s*==\s*([A-Za-z0-9_]+))"),
      comp_regex(R"(compiler\s*==\s*([A-Za-z0-9_]+))"),
      opt_regex(R"(options\.([A-Za-z0-9_]+)\s*==\s*(true|false))");
  std::smatch match;
  if (std::regex_search(condition, match, tt_regex))
    return targetTypeToString(target.type) == match[1].str();
  if (std::regex_search(condition, match, comp_regex)) {
    Toolchain tc = discoverToolchain();
    std::string active = "gcc";
    if (tc.is_msvc)
      active = "msvc";
    else if (tc.cpp_compiler == "clang++")
      active = "clang";
    return active == match[1].str();
  }
  if (std::regex_search(condition, match, opt_regex)) {
    std::string key = match[1].str();
    bool val = (match[2].str() == "true");
    return manifest->options.count(key) ? (manifest->options.at(key) == val)
                                        : !val;
  }
  return this->evaluateCond(condition);
}

std::vector<std::string>
Graph::resolveTargetSources(const Target &target,
                            const std::shared_ptr<ProjectManifest> &manifest) {
  std::vector<std::string> resolved;
  std::string base = manifest->base_dir.empty() ? "." : manifest->base_dir;
  auto eval_cb = [this, &target, &manifest](const std::string &cond) {
    return this->evaluateConditionExpression(cond, target, manifest);
  };
  for (const auto &raw : target.getActiveSources(eval_cb)) {
    if (raw.starts_with("@")) {
      std::string gt = raw.substr(1);
      bool found = false;
      for (const auto &fg : manifest->file_groups) {
        if (fg.name == gt) {
          found = true;
          for (const auto &p : fg.patterns)
            matchGlobPattern(p, base, resolved);
          break;
        }
      }
    } else if (raw.find_first_of("*?{") != std::string::npos)
      matchGlobPattern(raw, base, resolved);
    else {
      std::string clean = raw;
      if (clean.starts_with("./"))
        clean = clean.substr(2);
      fs::path fp = fs::path(base) / clean;
      if (fs::exists(fp) && fs::is_regular_file(fp))
        resolved.push_back(fp.lexically_normal().string());
    }
  }
  std::sort(resolved.begin(), resolved.end());
  resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
  return resolved;
}

void Graph::executeHooks(const std::shared_ptr<ProjectManifest> &manifest,
                         HookTrigger trigger, const std::string &target_name) {
  for (const auto &hook : manifest->hooks) {
    if (hook.trigger != trigger ||
        (hook.target.has_value() && hook.target.value() != target_name))
      continue;
    std::string ts = triggerToString(trigger);
    m_logger.Info("Executing Hook [" + hook.name + "] context '" + ts + "'");
    fs::path cp =
        fs::temp_directory_path() / ("mokai_hook_ctx_" + hook.name + ".json");
    std::ofstream ctx_file(cp);
    ctx_file << "{\n  \"trigger\": \"" << escapeJsonString(ts)
             << "\",\n  \"project\": \""
             << escapeJsonString(manifest->project.name)
             << "\",\n  \"target\": \"" << escapeJsonString(target_name)
             << "\"\n}\n";
    ctx_file.close();
    std::string cmd = (std::string(
#if defined(_WIN32) || defined(_WIN64)
                           "set MOKAI_CONTEXT_FILE="
#else
                           "MOKAI_CONTEXT_FILE="
#endif
                           ) +
                       cp.string() +
                       (
#if defined(_WIN32) || defined(_WIN64)
                           " && "
#else
                           " "
#endif
                           ) +
                       hook.run);
    std::system(cmd.c_str());
    fs::remove(cp);
  }
}

bool Graph::BuildAllTree(const std::vector<std::string> &build_order) {
  if (build_order.empty())
    return true;
  std::unordered_map<std::string, std::vector<std::string>>
      cached_resolved_sources;
  std::unordered_map<std::string, std::string> source_filename_to_target;
  for (const auto &qn : build_order) {
    const auto *qt = FindByQualifiedName(qn);
    if (!qt || (!m_options.target_filter.empty() &&
                qt->target.name != m_options.target_filter))
      continue;
    auto srcs = resolveTargetSources(qt->target, qt->manifest);
    cached_resolved_sources[qn] = srcs;
    for (const auto &s : srcs) {
      std::string fn = fs::path(s).filename().string();
      if (source_filename_to_target.count(fn)) {
        m_logger.Error("Duplicate source file name '" + fn + "' in '" +
                       source_filename_to_target[fn] + "' and '" + qn + "'");
        return false;
      }
      source_filename_to_target[fn] = qn;
    }
  }
  auto start_t = std::chrono::high_resolution_clock::now();
  Toolchain tc = discoverToolchain();
  std::string output_root = "./build",
              sub_p = (m_options.profile == BuildProfile::Release) ? "release"
                                                                   : "debug";
  std::string target_build_dir = output_root + "/" + sub_p,
              obj_cache_dir = target_build_dir + "/obj";
  fs::create_directories(target_build_dir);
  fs::create_directories(obj_cache_dir);
  std::string cache_path = "./.mokai/mokai.cache";
  fs::create_directories("./.mokai");
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      state_cache;
  std::mutex cache_mutex;
  if (fs::exists(cache_path) && !m_options.force_rebuild) {
    std::ifstream cf(cache_path);
    std::string line, f_p, f_t, f_h;
    while (std::getline(cf, line)) {
      std::stringstream ss(line);
      if (ss >> f_p >> f_t >> f_h)
        state_cache[f_p] = {f_t, f_h};
    }
  }
  std::unordered_map<std::string, std::string> built_library_map;
  std::vector<std::string> compile_commands;
  std::mutex db_mutex;
  if (!m_allTargets.empty())
    executeHooks(m_allTargets.front().manifest, HookTrigger::PreBuild, "");
  std::unordered_map<std::string, std::vector<std::string>> dep_graph;
  std::unordered_map<std::string, int> in_degree;
  std::unordered_set<std::string> pipeline_targets(build_order.begin(),
                                                   build_order.end());
  for (const auto &qn : build_order)
    in_degree[qn] = 0;
  for (const auto &qn : build_order) {
    const auto *qt = FindByQualifiedName(qn);
    if (!qt)
      continue;
    for (const auto &rd : qt->target.depends_on) {
      for (const auto &dn : resolveDependsOnEntry(rd, *qt)) {
        if (pipeline_targets.contains(dn)) {
          dep_graph[dn].push_back(qn);
          in_degree[qn]++;
        }
      }
    }
  }
  std::queue<std::string> ready_targets;
  int completed_targets = 0, total_targets = 0;
  std::atomic<bool> failed{false};
  std::atomic<int> completed_tasks{0};
  int processed_tasks = 0;
  for (const auto &qn : build_order) {
    const auto *qt = FindByQualifiedName(qn);
    if (!qt || (!m_options.target_filter.empty() &&
                qt->target.name != m_options.target_filter))
      continue;
    total_targets++;
    if (in_degree[qn] == 0)
      ready_targets.push(qn);
  }
  std::condition_variable sched_cv;
  std::mutex sched_mutex;
  unsigned int workers_n =
      (m_options.job_count > 0)
          ? (unsigned int)m_options.job_count
          : std::max(1u, std::thread::hardware_concurrency());
  struct FileTask {
    std::string src, obj, compiler, std_f, work_dir;
    std::shared_ptr<const std::vector<std::string>> base_args;
    bool is_msvc;
  };
  struct CacheRecord {
    std::string src, time, hash;
  };
  struct ActiveTargetContext {
    const QualifiedTarget *qt;
    std::vector<std::string> objects;
    std::atomic<size_t> remaining{0};
    std::atomic<bool> failed{false}, linkage{false};
    std::vector<CacheRecord> records;
    std::mutex records_mutex;
  };
  std::queue<std::pair<std::shared_ptr<ActiveTargetContext>, FileTask>> task_q;
  std::mutex q_mutex;
  std::condition_variable q_cv;
  std::atomic<bool> stop{false};
  std::atomic<int> global_step{0};
  std::atomic<size_t> hits{0}, misses{0}, total_srcs{0};
  std::vector<std::thread> workers;
  for (unsigned int i = 0; i < workers_n; ++i) {
    workers.emplace_back([&]() {
      while (true) {
        std::pair<std::shared_ptr<ActiveTargetContext>, FileTask> item;
        {
          std::unique_lock<std::mutex> lock(q_mutex);
          q_cv.wait(lock, [&]() { return !task_q.empty() || stop || failed; });
          if (stop || failed)
            break;
          item = std::move(task_q.front());
          task_q.pop();
        }
        auto &ctx = item.first;
        const auto &task = item.second;
        if (failed || ctx->failed) {
          ctx->remaining--;
          completed_tasks++;
          sched_cv.notify_one();
          continue;
        }
        bool compile = true;
        std::string ct;
        try {
          if (fs::exists(task.src)) {
            ct = std::to_string(
                fs::last_write_time(task.src).time_since_epoch().count());
            std::lock_guard<std::mutex> lock(cache_mutex);
            if (!m_options.force_rebuild && state_cache.count(task.src) &&
                state_cache[task.src].first == ct && fs::exists(task.obj)) {
              compile = false;
              hits++;
            }
          }
        } catch (...) {
        }
        if (compile) {
          misses++;
          ctx->linkage = true;
          std::vector<std::string> f_args;
          f_args.reserve(task.base_args->size() + 5);
          f_args.push_back(task.compiler);
          f_args.push_back(task.std_f);
          for (const auto &a : *task.base_args)
            f_args.push_back(a);
          if (task.is_msvc) {
            f_args.push_back("/c");
            f_args.push_back(task.src);
            f_args.push_back("/Fo" + task.obj);
          } else {
            f_args.push_back("-c");
            f_args.push_back(task.src);
            f_args.push_back("-o");
            f_args.push_back(task.obj);
          }
          std::string full_cmd;
          for (size_t a = 0; a < f_args.size(); ++a) {
            full_cmd += f_args[a];
            if (a + 1 < f_args.size())
              full_cmd += " ";
          }
          {
            std::lock_guard<std::mutex> lock(db_mutex);
            compile_commands.push_back(
                "  {\n    \"directory\": \"" + escapeJsonString(task.work_dir) +
                "\",\n    \"command\": \"" + escapeJsonString(full_cmd) +
                "\",\n    \"file\": \"" + escapeJsonString(task.src) +
                "\",\n    \"output\": \"" + escapeJsonString(task.obj) +
                "\"\n  }");
          }
          if (executeCommandFast(f_args) != 0) {
            ctx->failed = true;
            failed = true;
          } else if (!ct.empty()) {
            std::lock_guard<std::mutex> lock(ctx->records_mutex);
            ctx->records.push_back({task.src, ct, "-"});
          }
        }
        ctx->remaining--;
        completed_tasks++;
        sched_cv.notify_one();
      }
    });
  }
  std::vector<std::shared_ptr<ActiveTargetContext>> running;
  std::string work_dir = fs::current_path().string();
  std::unique_lock<std::mutex> s_lock(sched_mutex);
  while (completed_targets < total_targets && !failed) {
    while (!ready_targets.empty()) {
      std::string cr = ready_targets.front();
      ready_targets.pop();
      const auto *qt = FindByQualifiedName(cr);
      std::vector<std::string> srcs = cached_resolved_sources[cr];
      if (srcs.empty()) {
        if (m_options.verbosity != Verbosity::Quiet)
          m_logger.Warn("Skipping build unit '" + cr +
                        "': Source list evaluated to empty.");
        built_library_map[cr] = "";
        completed_targets++;
        for (const auto &dep : dep_graph[cr]) {
          if (--in_degree[dep] == 0)
            ready_targets.push(dep);
        }
        continue;
      }
      total_srcs += srcs.size();
      executeHooks(qt->manifest, HookTrigger::PreTargetBuild, qt->target.name);
      if (m_options.verbosity != Verbosity::Quiet)
        m_logger.Step(++global_step, total_targets,
                      "Compiling unit: " + cr + " [" + qt->target.name + "]");
      auto b_args = std::make_shared<std::vector<std::string>>();
      if (!tc.is_msvc) {
        b_args->push_back("-fPIC");
        if (m_options.profile == BuildProfile::Release) {
          b_args->push_back("-O3");
          b_args->push_back("-DNDEBUG");
        } else {
          b_args->push_back("-g");
          b_args->push_back("-O0");
        }
      } else {
        if (m_options.profile == BuildProfile::Release) {
          b_args->push_back("/O2");
          b_args->push_back("/DNDEBUG");
          b_args->push_back("/EHsc");
        } else {
          b_args->push_back("/Zi");
          b_args->push_back("/Od");
          b_args->push_back("/EHsc");
        }
      }
      auto eval_cb = [this, qt](const std::string &c) {
        return this->evaluateConditionExpression(c, qt->target, qt->manifest);
      };
      for (const auto &f : qt->target.getActiveFlags(eval_cb))
        b_args->push_back(f);
      std::string b = qt->manifest->base_dir.empty() ? "."
                                                     : qt->manifest->base_dir,
                  ip = tc.is_msvc ? "/I" : "-I";
      for (const auto &inc : qt->target.include_dirs) {
        fs::path p(inc);
        if (p.is_relative())
          p = fs::path(b) / p;
        b_args->push_back(ip + p.lexically_normal().string());
      }
      for (const auto &inc : qt->manifest->project.include_dirs) {
        fs::path p(inc);
        if (p.is_relative())
          p = fs::path(b) / p;
        b_args->push_back(ip + p.lexically_normal().string());
      }
      auto trans = getTransitiveDependencies(cr);
      std::reverse(trans.begin(), trans.end());
      std::unordered_set<std::string> s_inc;
      for (const auto &dn : trans) {
        if (const auto *dqt = FindByQualifiedName(dn)) {
          if (dqt->manifest && dqt->manifest->exports) {
            std::string db =
                dqt->manifest->base_dir.empty() ? "." : dqt->manifest->base_dir;
            for (const auto &ei : dqt->manifest->exports->include_dirs) {
              fs::path p(ei);
              if (p.is_relative())
                p = fs::path(db) / p;
              std::string n = p.lexically_normal().string();
              if (s_inc.insert(n).second)
                b_args->push_back(ip + n);
            }
          }
        }
      }
      std::string dp = tc.is_msvc ? "/D" : "-D";
      for (const auto &pr : qt->target.getActiveProperties(eval_cb)) {
        if (pr.starts_with("@")) {
          std::string tn = pr.substr(1);
          for (const auto &pg : qt->manifest->property_groups) {
            if (pg.name == tn &&
                (!pg.condition.has_value() ||
                 evaluateConditionExpression(pg.condition.value(), qt->target,
                                             qt->manifest))) {
              for (const auto &def : pg.defines)
                b_args->push_back(dp + def);
            }
          }
        } else
          b_args->push_back(dp + pr);
      }
      auto ctx = std::make_shared<ActiveTargetContext>();
      ctx->qt = qt;
      ctx->remaining = srcs.size();
      ctx->objects.resize(srcs.size());
      fs::create_directories(fs::path(obj_cache_dir) / qt->target.name);
      {
        std::lock_guard<std::mutex> lock(q_mutex);
        for (size_t idx = 0; idx < srcs.size(); ++idx) {
          const auto &s = srcs[idx];
          std::string ex = fs::path(s).extension().string();
          std::transform(ex.begin(), ex.end(), ex.begin(), ::tolower);
          bool is_c = (ex == ".c");
          std::string o = (fs::path(obj_cache_dir) / qt->target.name /
                           (fs::path(s).filename().string() + "_" +
                            std::to_string(std::hash<std::string>{}(s)) +
                            (tc.is_msvc ? ".obj" : ".o")))
                              .string();
          ctx->objects[idx] = o;
          task_q.push({ctx,
                       {s, o, is_c ? tc.c_compiler : tc.cpp_compiler,
                        tc.is_msvc ? (is_c ? "/std:c11" : "/std:c++20")
                                   : (is_c ? "-std=c11" : "-std=c++23"),
                        work_dir, b_args, tc.is_msvc}});
        }
      }
      q_cv.notify_all();
      running.push_back(ctx);
    }
    if (completed_targets >= total_targets || failed)
      break;
    sched_cv.wait(s_lock, [&]() {
      return failed || (completed_tasks.load() > processed_tasks);
    });
    processed_tasks = completed_tasks.load();
    for (auto it = running.begin(); it != running.end();) {
      auto ctx = *it;
      if (ctx->remaining.load() == 0) {
        if (ctx->failed) {
          failed = true;
          break;
        }
        std::string out_f = target_build_dir + "/" + ctx->qt->target.name;
#if defined(_WIN32) || defined(_WIN64)
        if (ctx->qt->target.type == TargetType::Executable)
          out_f += ".exe";
        else if (ctx->qt->target.type == TargetType::StaticLibrary)
          out_f += ".lib";
        else
          out_f += ".dll";
#else
        if (ctx->qt->target.type == TargetType::StaticLibrary)
          out_f = target_build_dir + "/lib" + ctx->qt->target.name + ".a";
        else if (ctx->qt->target.type == TargetType::SharedLibrary)
          out_f = target_build_dir + "/lib" + ctx->qt->target.name + ".so";
#endif
        if (ctx->linkage || !fs::exists(out_f)) {
          std::string l_cmd;
          auto trans = getTransitiveDependencies(ctx->qt->qualifiedName);
          std::reverse(trans.begin(), trans.end());
          if (ctx->qt->target.type == TargetType::StaticLibrary) {
            l_cmd = tc.is_msvc ? (tc.archiver + " /OUT:" + out_f)
                               : (tc.archiver + " rcs " + out_f);
            for (const auto &o : ctx->objects)
              l_cmd += " " + o;
          } else {
            if (tc.is_msvc) {
              l_cmd = "link /OUT:" + out_f + " ";
              if (ctx->qt->target.type == TargetType::SharedLibrary)
                l_cmd += "/DLL ";
            } else
              l_cmd = tc.cpp_compiler + " " +
                      (ctx->qt->target.type == TargetType::SharedLibrary
                           ? "-shared "
                           : "");
            for (const auto &o : ctx->objects)
              l_cmd += " " + o;
            if (tc.is_msvc) {
              for (const auto &dn : trans)
                if (built_library_map.count(dn) &&
                    !built_library_map[dn].empty())
                  l_cmd += " " +
                           fs::path(built_library_map[dn])
                               .replace_extension(".lib")
                               .string() +
                           " ";
              for (const auto &sl : ctx->qt->target.system_libs)
                l_cmd += " " + sl + ".lib ";
            } else {
              l_cmd += " -o " + out_f + " ";
              for (const auto &dn : trans)
                if (built_library_map.count(dn) &&
                    !built_library_map[dn].empty())
                  l_cmd += " " + built_library_map[dn] + " ";
              for (const auto &sl : ctx->qt->target.system_libs)
                l_cmd += " -l" + sl + " ";
            }
          }
          if (std::system(l_cmd.c_str()) != 0) {
            failed = true;
            break;
          }
        }
        built_library_map[ctx->qt->qualifiedName] = out_f;
        {
          std::lock_guard<std::mutex> lock(cache_mutex);
          for (const auto &rec : ctx->records)
            state_cache[rec.src] = {rec.time, rec.hash};
        }
        executeHooks(ctx->qt->manifest, HookTrigger::PostTargetBuild,
                     ctx->qt->target.name);
        completed_targets++;
        for (const auto &dep : dep_graph[ctx->qt->qualifiedName])
          if (--in_degree[dep] == 0)
            ready_targets.push(dep);
        it = running.erase(it);
      } else
        ++it;
    }
  }
  s_lock.unlock();
  {
    std::lock_guard<std::mutex> lock(q_mutex);
    stop = true;
  }
  q_cv.notify_all();
  for (auto &w : workers)
    if (w.joinable())
      w.join();
  if (failed)
    return false;
  std::ofstream out_c(cache_path);
  if (out_c.is_open()) {
    for (const auto &[p, d] : state_cache)
      out_c << p << " " << d.first << " " << d.second << "\n";
  }
  if (!compile_commands.empty()) {
    std::ofstream db(target_build_dir + "/compile_commands.json");
    if (db.is_open()) {
      db << "[\n";
      for (size_t i = 0; i < compile_commands.size(); ++i)
        db << compile_commands[i]
           << (i + 1 < compile_commands.size() ? ",\n" : "\n");
      db << "]";
    }
  }
  if (!m_allTargets.empty())
    executeHooks(m_allTargets.front().manifest, HookTrigger::PostBuild, "");
  if (m_options.verbosity != Verbosity::Quiet)
    m_logger.Success(
        "Build completed in " +
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::high_resolution_clock::now() - start_t)
                           .count()) +
        "ms.");
  return true;
}

} // namespace mokai
