#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "pareto/pareto.hpp"

namespace pareto {
namespace {
std::int64_t add(std::int64_t a, std::int64_t b) {
  if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
      (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
    throw std::overflow_error("battery cost overflow");
  return a + b;
}
std::int64_t subtract(std::int64_t a, std::int64_t b) {
  if (b == std::numeric_limits<std::int64_t>::min())
    throw std::overflow_error("battery cost overflow");
  return add(a, -b);
}
}  // namespace
Graph::Graph(std::size_t nodes, std::size_t dimensions)
    : dimensions_(dimensions), outgoing_(nodes), points_(nodes) {
  if (!nodes || dimensions < 2) throw std::invalid_argument("need nodes and at least two criteria");
}
Graph Graph::electric(std::size_t nodes, std::int64_t capacity) {
  if (capacity <= 0) throw std::invalid_argument("battery capacity must be positive");
  Graph graph(nodes, 4);
  graph.model_ = Model::ev;
  graph.capacity_ = capacity;
  return graph;
}
EdgeId Graph::add_edge(Node from, Node to, AdditiveCost cost) {
  if (model_ != Model::additive || cost.size() != dimensions_)
    throw std::invalid_argument("cost model or dimension mismatch");
  if (to >= size()) throw std::out_of_range("edge endpoint out of range");
  auto& out = outgoing_.at(from);
  for (double value : cost)
    if (!std::isfinite(value) || value < 0)
      throw std::invalid_argument("additive costs must be finite and nonnegative");
  const auto id = edges_.size();
  edges_.push_back({from, to, std::move(cost)});
  out.push_back(id);
  return id;
}
EdgeId Graph::add_ev_edge(Node from, Node to, std::int64_t time, std::int64_t consumption) {
  if (model_ != Model::ev || time <= 0)
    throw std::invalid_argument("EV edges require the EV model and positive traversal time");
  if (to >= size()) throw std::out_of_range("edge endpoint out of range");
  auto& out = outgoing_.at(from);
  const auto id = edges_.size();
  edges_.push_back({from, to,
                    BatteryCost{time, std::max(std::int64_t{0}, consumption), consumption,
                                std::min(capacity_, subtract(capacity_, consumption))}});
  out.push_back(id);
  return id;
}
void Graph::set_point(Node node, Point point) {
  if (!std::isfinite(point.x) || !std::isfinite(point.y))
    throw std::invalid_argument("coordinates must be finite");
  points_.at(node) = point;
}
const std::optional<Point>& Graph::point(Node node) const { return points_.at(node); }
const std::vector<EdgeId>& Graph::outgoing(Node node) const { return outgoing_.at(node); }
Cost Graph::identity() const {
  if (model_ == Model::ev) return BatteryCost{0, 0, 0, capacity_};
  return AdditiveCost(dimensions_, 0);
}
Cost compose(const Cost& prefix, const Cost& suffix) {
  if (prefix.index() != suffix.index()) throw std::invalid_argument("cost model mismatch");
  if (auto a = std::get_if<AdditiveCost>(&prefix)) {
    const auto& b = std::get<AdditiveCost>(suffix);
    if (a->size() != b.size()) throw std::invalid_argument("cost dimension mismatch");
    auto result = *a;
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] += b[i];
      if (!std::isfinite(result[i])) throw std::overflow_error("additive cost overflow");
    }
    return result;
  }
  const auto& a = std::get<BatteryCost>(prefix);
  const auto& b = std::get<BatteryCost>(suffix);
  return BatteryCost{add(a[0], b[0]), std::max(a[1], add(a[2], b[1])),
                     std::max(add(a[2], b[2]), subtract(a[1], b[3])),
                     std::min(subtract(a[3], b[2]), b[3])};
}
bool dominates(const Cost& lhs, const Cost& rhs) {
  if (lhs.index() != rhs.index()) throw std::invalid_argument("cost model mismatch");
  if (auto a = std::get_if<AdditiveCost>(&lhs)) {
    const auto& b = std::get<AdditiveCost>(rhs);
    if (a->size() != b.size()) throw std::invalid_argument("cost dimension mismatch");
    for (std::size_t i = 0; i < a->size(); ++i)
      if ((*a)[i] > b[i]) return false;
    return true;
  }
  const auto& a = std::get<BatteryCost>(lhs);
  const auto& b = std::get<BatteryCost>(rhs);
  return a[0] <= b[0] && a[1] <= b[1] && a[2] <= b[2] && a[3] >= b[3];
}
bool lex_less(const Cost& lhs, const Cost& rhs) {
  if (lhs.index() != rhs.index()) throw std::invalid_argument("cost model mismatch");
  if (auto a = std::get_if<AdditiveCost>(&lhs)) return *a < std::get<AdditiveCost>(rhs);
  const auto& a = std::get<BatteryCost>(lhs);
  const auto& b = std::get<BatteryCost>(rhs);
  for (std::size_t i = 0; i < 3; ++i)
    if (a[i] != b[i]) return a[i] < b[i];
  return a[3] > b[3];
}
std::vector<double> values(const Cost& cost) {
  if (auto a = std::get_if<AdditiveCost>(&cost)) return *a;
  const auto& a = std::get<BatteryCost>(cost);
  return {static_cast<double>(a[0]), static_cast<double>(a[1]), static_cast<double>(a[2]),
          static_cast<double>(a[3])};
}
std::string Graph::fingerprint() const {
  std::uint64_t hash = 14695981039346656037ULL;
  auto feed = [&](const std::string& str) {
    for (unsigned char c : str) {
      hash ^= c;
      hash *= 1099511628211ULL;
    }
  };
  feed(std::to_string(size()) + ":" + std::to_string(dimensions_) + ":" +
       std::to_string(capacity_) + ":");
  for (const auto& e : edges_) {
    std::ostringstream s;
    s << std::hexfloat << e.from << ',' << e.to << ':';
    if (auto a = std::get_if<AdditiveCost>(&e.cost))
      for (auto x : *a) s << x << ';';
    else
      for (auto x : std::get<BatteryCost>(e.cost)) s << x << ';';
    feed(s.str());
  }
  for (const auto& p : points_) {
    std::ostringstream s;
    if (p) s << std::hexfloat << p->x << ',' << p->y;
    feed(s.str() + ";");
  }
  std::ostringstream result;
  result << std::hex << hash;
  return result.str();
}
}  // namespace pareto
