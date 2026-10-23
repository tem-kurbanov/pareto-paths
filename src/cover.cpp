#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "pareto/pareto.hpp"

namespace pareto {
namespace {
std::string checksum(const std::string& data) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : data) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return std::to_string(hash);
}
// Iterative Kosaraju: cycles can improve charge, so EV overlays must keep them.
std::vector<bool> cyclic_vertices(const Graph& graph) {
  const auto n = graph.size();
  std::vector<std::vector<Node>> reverse(n);
  for (const auto& e : graph.edges()) reverse[e.to].push_back(e.from);
  std::vector<bool> seen(n), cyclic(n);
  std::vector<Node> order;
  for (Node root = 0; root < n; ++root) {
    if (seen[root]) continue;
    std::vector<std::pair<Node, std::size_t>> stack{{root, 0}};
    seen[root] = true;
    while (!stack.empty()) {
      auto& [v, pos] = stack.back();
      if (pos == graph.outgoing(v).size()) {
        order.push_back(v);
        stack.pop_back();
        continue;
      }
      auto next = graph.edges()[graph.outgoing(v)[pos++]].to;
      if (!seen[next]) {
        seen[next] = true;
        stack.emplace_back(next, 0);
      }
    }
  }
  std::fill(seen.begin(), seen.end(), false);
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (seen[*it]) continue;
    std::vector<Node> component, stack{*it};
    seen[*it] = true;
    while (!stack.empty()) {
      auto v = stack.back();
      stack.pop_back();
      component.push_back(v);
      for (auto prev : reverse[v])
        if (!seen[prev]) {
          seen[prev] = true;
          stack.push_back(prev);
        }
    }
    if (component.size() > 1)
      for (auto v : component) cyclic[v] = true;
  }
  for (const auto& e : graph.edges())
    if (e.from == e.to) cyclic[e.from] = true;
  return cyclic;
}
}  // namespace
Cover build_cover(const Graph& graph, std::vector<Node> targets, CoverOptions options) {
  if (options.k < 2 || options.k > 64 || !options.max_states)
    throw std::invalid_argument("cover k must be 2..64 and max_states positive");
  if (targets.empty()) throw std::invalid_argument("cover requires explicit targets");
  for (auto target : targets)
    if (target >= graph.size()) throw std::out_of_range("cover target out of range");
  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  const auto start = std::chrono::steady_clock::now();
  Cover cover;
  cover.graph_fingerprint_ = graph.fingerprint();
  cover.k_ = options.k;
  cover.triangle_pruning_ = options.triangle_pruning;
  cover.targets_ = std::move(targets);
  cover.retained_.assign(graph.size(), true);
  cover.outgoing_.resize(graph.size());
  std::vector<bool> pinned(graph.size());
  for (auto t : cover.targets_) pinned[t] = true;
  if (graph.model() == Model::ev) {
    const auto cyclic = cyclic_vertices(graph);
    for (Node v = 0; v < graph.size(); ++v) pinned[v] = pinned[v] || cyclic[v];
  }
  std::vector<std::vector<Node>> incoming(graph.size());
  for (const auto& e : graph.edges()) incoming[e.to].push_back(e.from);
  std::size_t states = 0;
  auto tick = [&] {
    if (++states > options.max_states)
      throw std::runtime_error(
          "cover state budget exceeded; use smaller k or a larger --max-states");
  };
  // Remove a vertex only if this cannot create an uncovered simple k-vertex path.
  // Growing both ends enumerates paths through the candidate without recursion on
  // graph size; depth is bounded by k. Retained vertices are boundary barriers.
  for (Node candidate = 0; candidate < graph.size(); ++candidate) {
    if (pinned[candidate]) continue;
    std::deque<Node> path{candidate};
    std::function<bool()> has_path = [&]() {
      tick();
      if (path.size() == options.k) return true;
      auto allowed = [&](Node v) {
        return (!cover.retained_[v] || v == candidate) &&
               std::find(path.begin(), path.end(), v) == path.end();
      };
      for (auto id : graph.outgoing(path.back())) {
        const auto v = graph.edges()[id].to;
        if (!allowed(v)) continue;
        path.push_back(v);
        if (has_path()) return true;
        path.pop_back();
      }
      for (auto v : incoming[path.front()]) {
        if (!allowed(v)) continue;
        path.push_front(v);
        if (has_path()) return true;
        path.pop_front();
      }
      return false;
    };
    if (!has_path()) cover.retained_[candidate] = false;
  }
  for (Node root = 0; root < graph.size(); ++root) {
    if (!cover.retained_[root]) continue;
    std::map<Node, std::vector<Shortcut>> shortcuts;
    std::vector<EdgeId> path;
    std::vector<Node> vertices{root};
    std::function<void(Node, const Cost&)> visit = [&](Node node, const Cost& cost) {
      tick();
      for (auto id : graph.outgoing(node)) {
        const auto& edge = graph.edges()[id];
        // Additive cycles are dominated; EV cycle vertices are all retained.
        if (std::find(vertices.begin(), vertices.end(), edge.to) != vertices.end() &&
            !cover.retained_[edge.to])
          continue;
        if (graph.model() == Model::additive && edge.to == root) continue;
        auto next_cost = compose(cost, edge.cost);
        if (auto c = std::get_if<BatteryCost>(&next_cost);
            c && ((*c)[1] > graph.capacity() || (*c)[3] < 0))
          continue;
        path.push_back(id);
        if (cover.retained_[edge.to]) {
          auto& bucket = shortcuts[edge.to];
          bool discard = std::any_of(bucket.begin(), bucket.end(), [&](const auto& old) {
            return dominates(old.cost, next_cost);
          });
          if (!discard) {
            std::erase_if(bucket, [&](const auto& old) { return dominates(next_cost, old.cost); });
            bucket.push_back({root, edge.to, std::move(next_cost), path});
          }
        } else {
          vertices.push_back(edge.to);
          visit(edge.to, next_cost);
          vertices.pop_back();
        }
        path.pop_back();
      }
    };
    visit(root, graph.identity());
    for (auto& [_, bucket] : shortcuts)
      for (auto& shortcut : bucket) {
        cover.outgoing_[root].push_back(cover.shortcuts_.size());
        cover.shortcuts_.push_back(std::move(shortcut));
      }
  }
  if (options.triangle_pruning) {
    // Remove sequentially: every replacement must use shortcuts still present.
    // Simultaneous removal can lose all equal-cost alternatives on zero cycles.
    std::vector<bool> active(cover.shortcuts_.size(), true);
    for (std::size_t candidate = 0; candidate < cover.shortcuts_.size(); ++candidate) {
      const auto& direct = cover.shortcuts_[candidate];
      for (auto first_id : cover.outgoing_[direct.from]) {
        if (!active[first_id]) continue;
        const auto& first = cover.shortcuts_[first_id];
        if (first.to == direct.from || first.to == direct.to) continue;
        for (auto second_id : cover.outgoing_[first.to]) {
          tick();
          if (!active[second_id]) continue;
          const auto& second = cover.shortcuts_[second_id];
          if (second.to == direct.to && dominates(compose(first.cost, second.cost), direct.cost)) {
            active[candidate] = false;
            break;
          }
        }
        if (!active[candidate]) break;
      }
    }
    std::vector<Shortcut> remaining;
    cover.outgoing_.assign(graph.size(), {});
    for (std::size_t i = 0; i < active.size(); ++i)
      if (active[i]) {
        cover.outgoing_[cover.shortcuts_[i].from].push_back(remaining.size());
        remaining.push_back(std::move(cover.shortcuts_[i]));
      }
    cover.shortcuts_ = std::move(remaining);
  }
  cover.seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return cover;
}
void save_cover(const Cover& cover, const std::filesystem::path& path) {
  std::ostringstream body;
  body << "PARETO_COVER 2\n"
       << cover.graph_fingerprint_ << ' ' << cover.k_ << ' ' << cover.retained_.size() << ' '
       << cover.triangle_pruning_ << '\n';
  for (bool kept : cover.retained_) body << (kept ? '1' : '0');
  body << '\n' << cover.targets_.size();
  for (auto target : cover.targets_) body << ' ' << target;
  body << '\n' << cover.shortcuts_.size() << '\n';
  for (const auto& shortcut : cover.shortcuts_) {
    body << shortcut.from << ' ' << shortcut.to << ' ' << shortcut.edges.size();
    for (auto id : shortcut.edges) body << ' ' << id;
    body << '\n';
  }
  const auto payload = body.str();
  std::ofstream out(path, std::ios::binary);
  if (!out || !(out << checksum(payload) << '\n' << payload))
    throw std::runtime_error("cannot write cover: " + path.string());
}
Cover load_cover(const Graph& graph, const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open cover: " + path.string());
  std::string digest;
  std::getline(in, digest);
  std::string payload{std::istreambuf_iterator<char>(in), {}};
  if (digest != checksum(payload)) throw std::runtime_error("corrupt cover checksum");
  std::istringstream data(payload);
  std::string magic, bits;
  std::size_t version{}, count{}, nodes{};
  Cover cover;
  if (!(data >> magic >> version >> cover.graph_fingerprint_ >> cover.k_ >> nodes >>
        cover.triangle_pruning_) ||
      magic != "PARETO_COVER" || version != 2 || nodes != graph.size() ||
      cover.graph_fingerprint_ != graph.fingerprint() || cover.k_ < 2 || cover.k_ > 64)
    throw std::runtime_error("incompatible cover metadata");
  if (!(data >> bits) || bits.size() != nodes) throw std::runtime_error("invalid cover vertices");
  for (char bit : bits) {
    if (bit != '0' && bit != '1') throw std::runtime_error("invalid cover bit");
    cover.retained_.push_back(bit == '1');
  }
  if (!(data >> count) || !count || count > nodes)
    throw std::runtime_error("invalid cover target count");
  cover.targets_.resize(count);
  for (auto& target : cover.targets_)
    if (!(data >> target) || target >= nodes || !cover.retained_[target])
      throw std::runtime_error("invalid cover target");
  if (!(data >> count) || count > payload.size())
    throw std::runtime_error("invalid shortcut count");
  cover.outgoing_.resize(nodes);
  for (std::size_t i = 0; i < count; ++i) {
    Shortcut shortcut;
    std::size_t length{};
    if (!(data >> shortcut.from >> shortcut.to >> length) || !length || length > nodes + 1 ||
        shortcut.from >= nodes || shortcut.to >= nodes || !cover.retained_[shortcut.from] ||
        !cover.retained_[shortcut.to])
      throw std::runtime_error("invalid shortcut");
    shortcut.cost = graph.identity();
    auto current = shortcut.from;
    for (std::size_t j = 0; j < length; ++j) {
      EdgeId id{};
      if (!(data >> id) || id >= graph.edges().size() || graph.edges()[id].from != current)
        throw std::runtime_error("invalid shortcut path");
      current = graph.edges()[id].to;
      if (j + 1 < length && cover.retained_[current])
        throw std::runtime_error("shortcut crosses retained vertex");
      shortcut.edges.push_back(id);
      shortcut.cost = compose(shortcut.cost, graph.edges()[id].cost);
    }
    if (current != shortcut.to) throw std::runtime_error("shortcut endpoint mismatch");
    cover.outgoing_[shortcut.from].push_back(cover.shortcuts_.size());
    cover.shortcuts_.push_back(std::move(shortcut));
  }
  std::string extra;
  if (data >> extra) throw std::runtime_error("unexpected cover trailing data");
  return cover;
}
}  // namespace pareto
