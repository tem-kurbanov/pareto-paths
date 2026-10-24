#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

#include "pareto/pareto.hpp"

namespace pareto {
namespace {
std::string trim(std::string text) {
  const auto start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return {};
  return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}
std::vector<std::string> split(const std::string& text) {
  std::vector<std::string> fields;
  std::istringstream stream(text);
  std::string item;
  while (std::getline(stream, item, ',')) fields.push_back(trim(item));
  if (!text.empty() && text.back() == ',') fields.emplace_back();
  return fields;
}
std::size_t natural(const std::string& text) {
  std::size_t value{};
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid nonnegative integer: " + text);
  return value;
}
double real(const std::string& text) {
  std::size_t pos{};
  const auto value = std::stod(text, &pos);
  if (pos != text.size() || !std::isfinite(value))
    throw std::invalid_argument("invalid finite number: " + text);
  return value;
}
std::int64_t integer(const std::string& text) {
  std::int64_t value{};
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid int64: " + text);
  return value;
}
struct RawEdge {
  Node from, to;
  std::vector<std::string> costs;
};
using Points = std::map<Node, Point>;
void read_nodes(std::istream& input, Points& points) {
  std::string line;
  bool header = false;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto fields = split(line);
    if (!header) {
      if (fields != std::vector<std::string>{"id", "x", "y"})
        throw std::invalid_argument("nodes header must be id,x,y");
      header = true;
      continue;
    }
    if (fields.size() != 3) throw std::invalid_argument("coordinate rows require id,x,y");
    const Node node = natural(fields[0]);
    if (!points.emplace(node, Point{real(fields[1]), real(fields[2])}).second)
      throw std::invalid_argument("duplicate node id");
  }
  if (!header || points.empty())
    throw std::invalid_argument("node CSV requires a header and coordinates");
}
struct Dimacs {
  std::size_t nodes{};
  std::size_t blocks{};
  std::vector<RawEdge> edges;
};
Dimacs read_dimacs(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open DIMACS file: " + path.string());
  Dimacs result;
  std::size_t expected{}, count{};
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream row(line);
    std::string kind, extra;
    if (!(row >> kind) || kind == "c") continue;
    if (kind == "p") {
      std::string problem, n, m;
      if (result.blocks >= 2 || !(row >> problem >> n >> m) || problem != "sp" || row >> extra)
        throw std::invalid_argument("invalid DIMACS p sp header");
      const auto nodes = natural(n), arcs = natural(m);
      if (!nodes) throw std::invalid_argument("DIMACS graph must contain vertices");
      if (result.blocks && (count != expected || nodes != result.nodes || arcs != expected))
        throw std::invalid_argument("paired DIMACS graph counts differ");
      result.nodes = nodes;
      expected = arcs;
      count = 0;
      ++result.blocks;
    } else if (kind == "a") {
      std::string f, t, w;
      if (!result.blocks || !(row >> f >> t >> w) || row >> extra || count >= expected)
        throw std::invalid_argument("invalid DIMACS arc");
      auto from = natural(f), to = natural(t);
      if (!from || !to || from > result.nodes || to > result.nodes)
        throw std::invalid_argument("DIMACS vertices must be 1-based and in range");
      if (result.blocks == 1) {
        result.edges.push_back({from - 1, to - 1, {w}});
      } else {
        auto& edge = result.edges[count];
        if (edge.from != from - 1 || edge.to != to - 1)
          throw std::invalid_argument("paired DIMACS arc ordering differs");
        edge.costs.push_back(w);
      }
      ++count;
    } else
      throw std::invalid_argument("expected DIMACS p/a/c records; remove test-query prefixes");
  }
  if (!result.blocks || count != expected)
    throw std::invalid_argument("DIMACS header or arc count mismatch");
  return result;
}
}  // namespace
Graph load_graph(const std::filesystem::path& path, LoadOptions options) {
  if (options.format != "csv" && options.format != "dimacs")
    throw std::invalid_argument("format must be csv or dimacs");
  std::vector<RawEdge> edges;
  Points points;
  std::size_t nodes{}, dimensions{};
  if (options.second_weights && options.format != "dimacs")
    throw std::invalid_argument("--second-weights requires dimacs format");
  if (options.format == "dimacs") {
    if (options.model != Model::additive)
      throw std::invalid_argument("DIMACS requires the additive model");
    auto first = read_dimacs(path);
    if (options.second_weights) {
      auto second = read_dimacs(*options.second_weights);
      if (first.blocks != 1 || second.blocks != 1)
        throw std::invalid_argument("--second-weights requires one graph block in each file");
      if (first.nodes != second.nodes || first.edges.size() != second.edges.size())
        throw std::invalid_argument("DIMACS graph sizes differ");
      for (std::size_t i = 0; i < first.edges.size(); ++i) {
        if (first.edges[i].from != second.edges[i].from || first.edges[i].to != second.edges[i].to)
          throw std::invalid_argument("paired DIMACS arc ordering differs");
        first.edges[i].costs.push_back(second.edges[i].costs[0]);
      }
    } else if (first.blocks != 2) {
      throw std::invalid_argument("DIMACS requires two graph blocks or --second-weights");
    }
    nodes = first.nodes;
    dimensions = 2;
    edges = std::move(first.edges);
  } else {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open graph: " + path.string());
    std::string line;
    bool header = true;
    std::size_t row_number = 0;
    while (std::getline(in, line)) {
      ++row_number;
      line = trim(line);
      if (line.empty() || line[0] == '#') continue;
      try {
        auto f = split(line);
        if (header) {
          header = false;
          if (f.size() < 4 || f[0] != "from" || f[1] != "to")
            throw std::invalid_argument(
                "CSV header must start from,to and name at least two criteria");
          if (std::any_of(f.begin(), f.end(), [](const auto& name) { return name.empty(); }) ||
              std::set<std::string>(f.begin(), f.end()).size() != f.size())
            throw std::invalid_argument("CSV column names must be nonempty and unique");
          dimensions = f.size() - 2;
          if (options.model == Model::ev &&
              f != std::vector<std::string>{"from", "to", "time", "consumption"})
            throw std::invalid_argument("EV CSV header must be from,to,time,consumption");
          continue;
        }
        if (f.size() != dimensions + 2)
          throw std::invalid_argument("inconsistent CSV row dimension");
        edges.push_back({natural(f[0]), natural(f[1]), {f.begin() + 2, f.end()}});
      } catch (const std::exception& e) {
        throw std::invalid_argument(path.string() + ":" + std::to_string(row_number) + ": " +
                                    e.what());
      }
    }
    if (header || dimensions < 2) throw std::invalid_argument("missing graph header or edges");
  }
  if (options.nodes) {
    std::ifstream in(*options.nodes);
    if (!in) throw std::runtime_error("cannot open nodes: " + options.nodes->string());
    read_nodes(in, points);
  }
  auto include_node = [&](Node v) {
    if (v == std::numeric_limits<Node>::max()) throw std::overflow_error("node id overflow");
    nodes = std::max(nodes, v + 1);
  };
  for (const auto& edge : edges) {
    include_node(edge.from);
    include_node(edge.to);
  }
  for (const auto& [node, _] : points) include_node(node);
  if (!nodes) throw std::invalid_argument("graph has no vertices");
  auto graph = options.model == Model::ev ? Graph::electric(nodes, options.capacity)
                                          : Graph(nodes, dimensions);
  for (const auto& [node, point] : points) graph.set_point(node, point);
  for (const auto& edge : edges) {
    if (options.model == Model::ev) {
      if (edge.costs.size() != 2)
        throw std::invalid_argument("EV edges require time and consumption only");
      graph.add_ev_edge(edge.from, edge.to, integer(edge.costs[0]), integer(edge.costs[1]));
    } else {
      AdditiveCost cost;
      for (const auto& text : edge.costs) cost.push_back(real(text));
      graph.add_edge(edge.from, edge.to, std::move(cost));
    }
  }
  return graph;
}
Graph generate_graph(const std::string& preset, std::size_t nodes, std::uint32_t seed) {
  const bool ev = preset == "ev";
  const bool reciprocal = preset == "reciprocal";
  const bool correlated = preset == "correlated2" || preset == "correlated3";
  if (!ev && !reciprocal && !correlated && preset != "independent2" && preset != "independent3")
    throw std::invalid_argument("unknown preset: " + preset);
  if (nodes < 2 || nodes > 100'000)
    throw std::invalid_argument("synthetic node count must be 2..100000");
  const std::size_t dimensions = preset.ends_with('3') ? 3 : 2;
  auto graph = ev ? Graph::electric(nodes, 100) : Graph(nodes, dimensions);
  std::mt19937 rng(seed);
  // Explicit modulo mapping avoids implementation-specific distribution behavior.
  auto draw = [&]() { return 1 + static_cast<int>(rng() % 20); };
  for (Node node = 0; node < nodes; ++node)
    graph.set_point(node, {static_cast<double>(node), static_cast<double>(node % 5)});
  for (Node from = 0; from < nodes; ++from) {
    for (Node delta : {Node{1}, Node{2}, Node{5}}) {
      if (from + delta >= nodes) continue;
      const auto time = draw();
      if (ev)
        graph.add_ev_edge(from, from + delta, time, draw() - 5);
      else {
        AdditiveCost cost{static_cast<double>(time)};
        for (std::size_t d = 1; d < dimensions; ++d)
          cost.push_back(reciprocal   ? 1.0 / time
                         : correlated ? time + static_cast<double>(rng() % 3)
                                      : draw());
        graph.add_edge(from, from + delta, std::move(cost));
      }
    }
  }
  return graph;
}
}  // namespace pareto
