#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pareto {
using Node = std::size_t;
using EdgeId = std::size_t;
using AdditiveCost = std::vector<double>;
// time, minimum initial charge, net consumption, maximum final charge
using BatteryCost = std::array<std::int64_t, 4>;
using Cost = std::variant<AdditiveCost, BatteryCost>;

enum class Model { additive, ev };
struct Point {
  double x{}, y{};
};
struct Edge {
  Node from{}, to{};
  Cost cost;
};

class Graph {
 public:
  explicit Graph(std::size_t nodes, std::size_t dimensions = 2);
  static Graph electric(std::size_t nodes, std::int64_t capacity);
  EdgeId add_edge(Node from, Node to, AdditiveCost cost);
  EdgeId add_ev_edge(Node from, Node to, std::int64_t time, std::int64_t consumption);
  void set_point(Node node, Point point);
  [[nodiscard]] std::size_t size() const { return outgoing_.size(); }
  [[nodiscard]] std::size_t dimensions() const { return dimensions_; }
  [[nodiscard]] Model model() const { return model_; }
  [[nodiscard]] std::int64_t capacity() const { return capacity_; }
  [[nodiscard]] const std::vector<Edge>& edges() const { return edges_; }
  [[nodiscard]] const std::vector<EdgeId>& outgoing(Node node) const;
  [[nodiscard]] const std::optional<Point>& point(Node node) const;
  [[nodiscard]] Cost identity() const;
  // FNV-1a fingerprint for cache compatibility, not a cryptographic identifier.
  [[nodiscard]] std::string fingerprint() const;

 private:
  Model model_{Model::additive};
  std::size_t dimensions_{};
  std::int64_t capacity_{};
  std::vector<Edge> edges_;
  std::vector<std::vector<EdgeId>> outgoing_;
  std::vector<std::optional<Point>> points_;
};

[[nodiscard]] Cost compose(const Cost& prefix, const Cost& suffix);
[[nodiscard]] bool dominates(const Cost& lhs, const Cost& rhs);
[[nodiscard]] bool lex_less(const Cost& lhs, const Cost& rhs);
[[nodiscard]] std::vector<double> values(const Cost& cost);

struct CoverOptions {
  std::size_t k{16};  // Every simple path of k vertices meets the cover.
  std::size_t max_states{2'000'000};
  bool triangle_pruning{false};
};
struct Shortcut {
  Node from{}, to{};
  Cost cost;
  std::vector<EdgeId> edges;
};
class Cover {
 public:
  [[nodiscard]] const std::string& graph_fingerprint() const { return graph_fingerprint_; }
  [[nodiscard]] std::size_t k() const { return k_; }
  [[nodiscard]] bool triangle_pruning() const { return triangle_pruning_; }
  [[nodiscard]] const std::vector<bool>& retained() const { return retained_; }
  [[nodiscard]] const std::vector<Node>& targets() const { return targets_; }
  [[nodiscard]] const std::vector<Shortcut>& shortcuts() const { return shortcuts_; }
  [[nodiscard]] const std::vector<std::vector<std::size_t>>& outgoing() const { return outgoing_; }
  [[nodiscard]] double seconds() const { return seconds_; }

 private:
  friend Cover build_cover(const Graph&, std::vector<Node>, CoverOptions);
  friend void save_cover(const Cover&, const std::filesystem::path&);
  friend Cover load_cover(const Graph&, const std::filesystem::path&);
  std::string graph_fingerprint_;
  std::size_t k_{};
  bool triangle_pruning_{};
  std::vector<bool> retained_;
  std::vector<Node> targets_;
  std::vector<Shortcut> shortcuts_;
  std::vector<std::vector<std::size_t>> outgoing_;
  double seconds_{};
};
[[nodiscard]] Cover build_cover(const Graph& graph, std::vector<Node> targets,
                                CoverOptions options = {});
void save_cover(const Cover& cover, const std::filesystem::path& path);
[[nodiscard]] Cover load_cover(const Graph& graph, const std::filesystem::path& path);

enum class Algorithm { mls, t_mls, kpc_mls, t_kpc_mls, ima, blset, kpc_ima, kpc_ima_straight };
[[nodiscard]] std::string algorithm_name(Algorithm algorithm);
[[nodiscard]] Algorithm parse_algorithm(const std::string& name);
[[nodiscard]] bool uses_cover(Algorithm algorithm);
struct SearchOptions {
  Algorithm algorithm{Algorithm::mls};
  // Empty means exact dominance; components in (0,1] enable epsilon dominance.
  std::vector<double> epsilons;
  // Zero disables a component; otherwise a ratio >= 1 (experimental heuristic).
  std::vector<double> capping;
  std::optional<double> ellipse;
  std::size_t max_labels{1'000'000};
};
struct Statistics {
  double seconds{};
  std::size_t generated{}, expanded{}, dominated{}, peak_queue{}, bound_pruned{};
};
struct Route {
  Cost cost;
  std::vector<EdgeId> edges;
  std::vector<Node> nodes;
};
struct SearchResult {
  std::map<Node, std::vector<Route>> routes;
  Statistics statistics;
  bool approximate{};
};
// Empty targets means every vertex for non-cover algorithms. Cover queries require
// an explicit target set retained by preprocessing. Results own all their data.
[[nodiscard]] SearchResult search(const Graph& graph, Node source, std::vector<Node> targets = {},
                                  SearchOptions options = {}, const Cover* cover = nullptr);

struct LoadOptions {
  std::string format{"csv"};  // csv, legacy-edges, legacy-random, legacy-full, dimacs
  Model model{Model::additive};
  std::int64_t capacity{40'000'000};
  std::optional<std::filesystem::path> nodes;
  std::optional<std::filesystem::path> second_weights;
};
[[nodiscard]] Graph load_graph(const std::filesystem::path& path, LoadOptions options = {});
// Synthetic presets: independent2, independent3, correlated2, correlated3,
// reciprocal, ev. Same seed produces the same graph on supported platforms.
[[nodiscard]] Graph generate_graph(const std::string& preset, std::size_t nodes,
                                   std::uint32_t seed);
}  // namespace pareto
