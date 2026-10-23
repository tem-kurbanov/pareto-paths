#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>

#include "pareto/pareto.hpp"

namespace pareto {
std::string algorithm_name(Algorithm a) {
  switch (a) {
    case Algorithm::mls:
      return "mls";
    case Algorithm::t_mls:
      return "t-mls";
    case Algorithm::kpc_mls:
      return "kpc-mls";
    case Algorithm::t_kpc_mls:
      return "t-kpc-mls";
    case Algorithm::ima:
      return "ima";
    case Algorithm::blset:
      return "blset";
    case Algorithm::kpc_ima:
      return "kpc-ima";
    case Algorithm::kpc_ima_straight:
      return "kpc-ima-straight";
  }
  throw std::invalid_argument("unknown algorithm enum");
}
Algorithm parse_algorithm(const std::string& name) {
  for (auto a : {Algorithm::mls, Algorithm::t_mls, Algorithm::kpc_mls, Algorithm::t_kpc_mls,
                 Algorithm::ima, Algorithm::blset, Algorithm::kpc_ima, Algorithm::kpc_ima_straight})
    if (algorithm_name(a) == name) return a;
  throw std::invalid_argument("unknown algorithm: " + name);
}
bool uses_cover(Algorithm a) {
  return a == Algorithm::kpc_mls || a == Algorithm::t_kpc_mls || a == Algorithm::kpc_ima ||
         a == Algorithm::kpc_ima_straight;
}
namespace {
constexpr auto no_parent = std::numeric_limits<std::size_t>::max();
long double component(const Cost& cost, std::size_t i) {
  if (auto a = std::get_if<AdditiveCost>(&cost)) return a->at(i);
  return std::get<BatteryCost>(cost).at(i);
}
bool weak(const Cost& a, const Cost& b, const std::vector<double>& eps, std::size_t begin = 0) {
  if (eps.empty() || std::all_of(eps.begin(), eps.end(), [](double value) { return value == 1; })) {
    if (auto x = std::get_if<AdditiveCost>(&a)) {
      const auto& y = std::get<AdditiveCost>(b);
      for (std::size_t i = begin; i < x->size(); ++i)
        if ((*x)[i] > y[i]) return false;
    } else {
      const auto& battery_a = std::get<BatteryCost>(a);
      const auto& y = std::get<BatteryCost>(b);
      for (std::size_t i = begin; i < 4; ++i)
        if (i == 3 ? battery_a[i] < y[i] : battery_a[i] > y[i]) return false;
    }
    return true;
  }
  // Approximation augments exact dominance. In particular, multiplying negative
  // consumption by epsilon must not stop equal EV profiles from being discarded.
  if (!eps.empty() && weak(a, b, {}, begin)) return true;
  const auto n = a.index() == 0 ? std::get<AdditiveCost>(a).size() : 4;
  for (std::size_t i = begin; i < n; ++i) {
    const long double factor = eps.empty() ? 1.0L : eps[i];
    if (a.index() == 1 && i == 3) {
      if (component(a, i) < component(b, i) * factor) return false;
    } else if (component(a, i) * factor > component(b, i))
      return false;
  }
  return true;
}
struct Step {
  bool shortcut{};
  std::size_t id{};
};
struct Label {
  Node node;
  Cost cost;
  std::size_t parent;
  Step step;
  bool active{true};
  bool closed{};
};
struct Arc {
  Node from, to;
  const Cost* cost;
  Step step;
};
bool first_at_least(const Cost& a, const Cost& b) {
  if (auto x = std::get_if<AdditiveCost>(&a)) return (*x)[0] >= std::get<AdditiveCost>(b)[0];
  return std::get<BatteryCost>(a)[0] >= std::get<BatteryCost>(b)[0];
}
}  // namespace
SearchResult search(const Graph& graph, Node source, std::vector<Node> targets,
                    SearchOptions options, const Cover* cover) {
  if (source >= graph.size()) throw std::out_of_range("source out of range");
  const auto name = algorithm_name(options.algorithm);
  const bool overlay = uses_cover(options.algorithm);
  const bool bounded =
      options.algorithm == Algorithm::ima || options.algorithm == Algorithm::blset ||
      options.algorithm == Algorithm::kpc_ima || options.algorithm == Algorithm::kpc_ima_straight;
  const bool reduced =
      options.algorithm == Algorithm::t_mls || options.algorithm == Algorithm::t_kpc_mls ||
      options.algorithm == Algorithm::kpc_ima || options.algorithm == Algorithm::kpc_ima_straight;
  if (overlay && targets.empty())
    throw std::invalid_argument("cover search requires explicit targets");
  if (bounded && (targets.size() != 1 || graph.model() != Model::additive))
    throw std::invalid_argument(name + " requires one target and additive costs");
  if (!options.max_labels) throw std::invalid_argument("max_labels must be positive");
  if (targets.empty()) {
    targets.resize(graph.size());
    std::iota(targets.begin(), targets.end(), Node{0});
  }
  for (auto target : targets)
    if (target >= graph.size()) throw std::out_of_range("target out of range");
  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  if (overlay) {
    if (!cover || cover->graph_fingerprint() != graph.fingerprint() ||
        cover->retained().size() != graph.size() || cover->outgoing().size() != graph.size())
      throw std::invalid_argument("missing or incompatible cover");
    for (auto t : targets)
      if (!cover->retained()[t]) throw std::invalid_argument("query target not retained in cover");
  }
  const auto dimensions = graph.dimensions();
  auto validate_vector = [&](const std::vector<double>& v, bool epsilon) {
    if (v.empty()) return;
    if (v.size() != dimensions) throw std::invalid_argument("pruning vector dimension mismatch");
    for (auto x : v)
      if (!std::isfinite(x) || (epsilon ? (x <= 0 || x > 1) : (x != 0 && x < 1)))
        throw std::invalid_argument("epsilons must be in (0,1]; caps must be zero or >=1");
  };
  validate_vector(options.epsilons, true);
  validate_vector(options.capping, false);
  if (options.ellipse) {
    if (targets.size() != 1 || !std::isfinite(*options.ellipse) || *options.ellipse < 1)
      throw std::invalid_argument("ellipse requires one target and a finite ratio >=1");
    for (Node n = 0; n < graph.size(); ++n)
      if (!graph.point(n))
        throw std::invalid_argument("ellipse requires coordinates for all nodes");
  }
  SearchResult result;
  result.approximate =
      std::any_of(options.epsilons.begin(), options.epsilons.end(),
                  [](auto x) { return x != 1; }) ||
      std::any_of(options.capping.begin(), options.capping.end(), [](auto x) { return x != 0; }) ||
      options.ellipse.has_value();
  const auto started = std::chrono::steady_clock::now();
  std::vector<std::vector<Arc>> adjacency(graph.size());
  for (const auto& edge : graph.edges()) {
    if (overlay && cover->retained()[edge.from]) continue;
    adjacency[edge.from].push_back(
        {edge.from,
         edge.to,
         &edge.cost,
         {false, static_cast<std::size_t>(&edge - graph.edges().data())}});
  }
  if (overlay)
    for (std::size_t i = 0; i < cover->shortcuts().size(); ++i) {
      const auto& edge = cover->shortcuts()[i];
      adjacency.at(edge.from).push_back({edge.from, edge.to, &edge.cost, {true, i}});
    }
  std::vector<AdditiveCost> lower;
  if (bounded && options.algorithm != Algorithm::blset) {
    const auto infinity = std::numeric_limits<double>::infinity();
    lower.assign(graph.size(), AdditiveCost(dimensions, infinity));
    std::vector<std::vector<std::pair<Node, const Cost*>>> reverse(graph.size());
    // The direct variant bounds the original graph; kpc-ima bounds the overlay
    // augmented with connectors from every non-cover source.
    if (options.algorithm == Algorithm::kpc_ima) {
      for (const auto& arcs : adjacency)
        for (const auto& arc : arcs) reverse[arc.to].emplace_back(arc.from, arc.cost);
    } else
      for (const auto& e : graph.edges()) reverse[e.to].emplace_back(e.from, &e.cost);
    for (std::size_t d = 0; d < dimensions; ++d) {
      using Item = std::pair<double, Node>;
      std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
      lower[targets[0]][d] = 0;
      queue.emplace(0, targets[0]);
      while (!queue.empty()) {
        auto [distance, v] = queue.top();
        queue.pop();
        if (distance != lower[v][d]) continue;
        for (const auto& [prev, cost] : reverse[v]) {
          const auto candidate = distance + std::get<AdditiveCost>(*cost)[d];
          if (!std::isfinite(candidate)) throw std::overflow_error("lower bound overflow");
          if (candidate < lower[prev][d]) {
            lower[prev][d] = candidate;
            queue.emplace(candidate, prev);
          }
        }
      }
    }
  }
  std::vector<Label> arena;
  std::vector<std::vector<std::size_t>> labels(graph.size()), t_sets(graph.size());
  std::vector<double> scalar_t(graph.size(), std::numeric_limits<double>::infinity());
  std::vector<std::optional<std::size_t>> closed_first(graph.size());
  auto compare = [&](std::size_t a, std::size_t b) {
    if (lex_less(arena[a].cost, arena[b].cost)) return true;
    if (lex_less(arena[b].cost, arena[a].cost)) return false;
    return a < b;
  };
  std::set<std::size_t, decltype(compare)> queue(compare);
  auto enqueue = [&](Node node, Cost cost, std::size_t parent, Step step) {
    if (arena.size() >= options.max_labels)
      throw std::runtime_error("search label budget exceeded; no partial result returned");
    auto id = arena.size();
    arena.push_back({node, std::move(cost), parent, step});
    labels[node].push_back(id);
    queue.insert(id);
    ++result.statistics.generated;
    result.statistics.peak_queue = std::max(result.statistics.peak_queue, queue.size());
  };
  enqueue(source, graph.identity(), no_parent, {});
  auto distance = [&](Node a, Node b) {
    const auto p = *graph.point(a), q = *graph.point(b);
    return std::hypot(p.x - q.x, p.y - q.y);
  };
  auto outside = [&](Node node) {
    return options.ellipse && distance(source, node) + distance(node, targets[0]) >
                                  distance(source, targets[0]) * *options.ellipse;
  };
  while (!queue.empty()) {
    const auto id = *queue.begin();
    queue.erase(queue.begin());
    if (!arena[id].active) continue;
    arena[id].closed = true;
    const auto node = arena[id].node;
    const auto current_cost = arena[id].cost;  // arena may reallocate on enqueue
    ++result.statistics.expanded;
    if (reduced) {
      if (!closed_first[node] || first_at_least(current_cost, arena[*closed_first[node]].cost))
        closed_first[node] = id;
      if (graph.model() == Model::additive && dimensions == 2 && options.epsilons.empty())
        scalar_t[node] = std::min(scalar_t[node], std::get<AdditiveCost>(current_cost)[1]);
      else {
        auto& ts = t_sets[node];
        if (!std::any_of(ts.begin(), ts.end(),
                         [&](auto old) { return weak(arena[old].cost, current_cost, {}, 1); })) {
          std::erase_if(ts, [&](auto old) { return weak(current_cost, arena[old].cost, {}, 1); });
          ts.push_back(id);
        }
      }
    }
    if (bounded && node == targets[0]) continue;
    for (const auto& arc : adjacency[node]) {
      if (outside(arc.to)) continue;
      if (options.ellipse && arc.step.shortcut) {
        const auto& path = cover->shortcuts()[arc.step.id].edges;
        if (std::any_of(path.begin(), path.end(),
                        [&](auto e) { return outside(graph.edges()[e].to); }))
          continue;
      }
      auto candidate = compose(current_cost, *arc.cost);
      if (auto c = std::get_if<BatteryCost>(&candidate);
          c && ((*c)[1] > graph.capacity() || (*c)[3] < 0))
        continue;
      if (bounded) {
        Cost estimate = candidate;
        bool unreachable = false;
        if (!lower.empty()) {
          unreachable = !std::isfinite(lower[arc.to][0]);
          if (!unreachable) estimate = compose(candidate, Cost{lower[arc.to]});
        }
        bool prune = unreachable;
        for (auto goal : labels[targets[0]])
          if (arena[goal].active && dominates(arena[goal].cost, estimate)) {
            prune = true;
            break;
          }
        if (prune) {
          ++result.statistics.bound_pruned;
          continue;
        }
      }
      bool discard = false;
      const bool use_t_set = reduced && closed_first[arc.to] &&
                             first_at_least(candidate, arena[*closed_first[arc.to]].cost);
      if (use_t_set) {
        if (graph.model() == Model::additive && dimensions == 2 && options.epsilons.empty())
          discard = scalar_t[arc.to] <= std::get<AdditiveCost>(candidate)[1];
        else
          for (auto old : t_sets[arc.to])
            if (weak(arena[old].cost, candidate, options.epsilons, 1)) {
              discard = true;
              break;
            }
      }
      const auto& bucket = labels[arc.to];
      for (auto old : bucket) {
        if (!arena[old].active) continue;
        if ((!reduced || !arena[old].closed) &&
            weak(arena[old].cost, candidate, options.epsilons)) {
          discard = true;
          break;
        }
        // Outside the t-set's first-component guard, use full dominance.
        if (reduced && !use_t_set && arena[old].closed &&
            weak(arena[old].cost, candidate, options.epsilons)) {
          discard = true;
          break;
        }
      }
      if (!discard && !options.capping.empty()) {
        for (std::size_t d = 0; d < dimensions; ++d) {
          const auto ratio = options.capping[d];
          if (ratio == 0) continue;
          std::optional<long double> best;
          const bool maximize = graph.model() == Model::ev && d == 3;
          for (auto old : bucket)
            if (arena[old].active && arena[old].closed) {
              auto value = component(arena[old].cost, d);
              if (!best || (maximize ? value > *best : value < *best)) best = value;
            }
          if (best && (maximize ? *best > ratio * component(candidate, d)
                                : *best * ratio < component(candidate, d))) {
            discard = true;
            break;
          }
        }
      }
      if (discard) {
        ++result.statistics.dominated;
        continue;
      }
      for (auto old : bucket)
        if (arena[old].active && dominates(candidate, arena[old].cost)) {
          arena[old].active = false;
          queue.erase(old);
        }
      enqueue(arc.to, std::move(candidate), id, arc.step);
    }
  }
  for (auto target : targets) {
    auto& routes = result.routes[target];
    for (auto id : labels[target]) {
      if (!arena[id].active) continue;
      Route route{arena[id].cost, {}, {source}};
      std::vector<Step> steps;
      auto cursor = id;
      while (arena[cursor].parent != no_parent) {
        steps.push_back(arena[cursor].step);
        cursor = arena[cursor].parent;
      }
      for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        if (it->shortcut) {
          const auto& edges = cover->shortcuts()[it->id].edges;
          route.edges.insert(route.edges.end(), edges.begin(), edges.end());
        } else
          route.edges.push_back(it->id);
      }
      for (auto edge : route.edges) route.nodes.push_back(graph.edges()[edge].to);
      routes.push_back(std::move(route));
    }
    std::sort(routes.begin(), routes.end(),
              [](const auto& a, const auto& b) { return lex_less(a.cost, b.cost); });
  }
  result.statistics.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return result;
}
}  // namespace pareto
