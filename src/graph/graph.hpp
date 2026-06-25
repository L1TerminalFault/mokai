#pragma once

#include "graph/types.hpp"
#include "log/log.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mokai {

struct GlobalOptions;

/**
 * @brief The Graph class manages the entire build topology.
 * It transforms a nested tree of ProjectManifests into a flattened,
 * deduplicated Directed Acyclic Graph (DAG) of buildable targets.
 */
class Graph {
public:
  /**
   * @brief Initializes the Graph and triggers the discovery phase.
   * @param rootManifest The entry point of the project.
   * @param options CLI-driven configurations.
   */
  Graph(std::shared_ptr<ProjectManifest> rootManifest,
        const GlobalOptions &options);

  /**
   * @brief The primary entry point for the build orchestration.
   * Topological sort, dependency validation, and parallel execution.
   */
  bool BuildAllTree(const std::vector<std::string> &build_order);

  /**
   * @brief Sorts the registry into a safe execution sequence.
   * Detects cycles and validates that all dependencies exist.
   */
  std::vector<std::string>
  computeBuildOrder(const std::vector<GraphEdge> &edges);

  /**
   * @brief Access the raw dependency edges for visualization or analysis.
   */
  std::vector<GraphEdge> getEdges() const { return m_edges; }

  /**
   * @brief Propagates include paths and defines from dependencies up to the
   * target.
   */
  std::vector<std::string>
  getTransitiveDependencies(const std::string &qualified_name);

  /**
   * @brief Evaluates TOML condition strings (e.g., "os == linux") against
   * current state.
   */
  bool
  evaluateConditionExpression(const std::string &condition,
                              const Target &target,
                              const std::shared_ptr<ProjectManifest> &manifest);

  /**
   * @brief Triggers lifecycle scripts (scripts defined in [hook.*]).
   */
  void executeHooks(const std::shared_ptr<ProjectManifest> &manifest,
                    HookTrigger trigger, const std::string &target_name);

private:
  /**
   * @brief Helper for recursive transitive dependency collection.
   */
  void collectTransitive(const std::string &node,
                         std::unordered_set<std::string> &visited,
                         std::vector<std::string> &out_libs);
  /**
   * @brief Recursively explores manifests and populates the Global Registry.
   * Implements Diamond Dependency deduplication by tracking processed
   * manifests.
   */
  void populateRegistry(std::shared_ptr<ProjectManifest> manifest,
                        const std::string &path_prefix);

  /**
   * @brief Helper to sanitize and format FQDNs (Fully Qualified Domain Names).
   * e.g., "root.deps.fmt::fmt"
   */
  std::string generateQualifiedName(const std::string &prefix,
                                    const std::string &name) const;

  /**
   * @brief Iterates the Registry and creates formal GraphEdges by resolving
   * 'depends_on' tokens.
   */
  std::vector<GraphEdge> buildEdges();

  /**
   * @brief Logic for mapping a string dependency (like "fmt" or "pkg:tgt") to a
   * registry entry.
   */
  std::vector<std::string>
  resolveDependsOnEntry(const std::string &raw_dep,
                        const QualifiedTarget &from_target);

  /**
   * @brief Resolves globs, groups (@name), and literals into absolute physical
   * paths.
   */
  std::vector<std::string>
  resolveTargetSources(const Target &target,
                       const std::shared_ptr<ProjectManifest> &manifest);

  void matchGlobPattern(const std::string &pattern, const std::string &base_dir,
                        std::vector<std::string> &matches);

  std::vector<std::string> expandBraceNotation(const std::string &pattern);

  bool evaluateCond(const std::string &cond_expr);

  const QualifiedTarget *
  FindByQualifiedName(const std::string &qualified_name) const;

private:
  enum class NodeState { Unvisited, Visiting, Done };

  log::Logger m_logger;
  const GlobalOptions &m_options;

  // The Global Target Registry: Map of FQDN -> Target Data
  // Deduplicates targets automatically across the entire build graph.
  std::unordered_map<std::string, QualifiedTarget> m_targetRegistry;

  // Dependency Tracking: Map of FQDN -> List of FQDNs it depends on.
  std::unordered_map<std::string, std::vector<std::string>> m_adjacencyList;

  // Memoization set: Prevents re-parsing the same manifest in diamond
  // dependencies.
  std::unordered_set<ProjectManifest *> m_processedManifests;

  std::shared_ptr<ProjectManifest> m_root_manifest;
  std::vector<GraphEdge> m_edges;

  // Configuration for the NameResolver
  const std::string m_namespaceSeparator = "::";
  const std::string m_packageSeparator = ".";
  const std::string m_rootPrefix = "root";
};

} // namespace mokai
