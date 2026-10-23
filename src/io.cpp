#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
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
void read_nodes(std::istream& input, Points& points, bool header, bool stop_at_blank) {
  std::string line;
  bool first = true;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty()) {
      if (stop_at_blank)
        return;
      else
        continue;
    }
    if (line[0] == '#') continue;
    auto f = split(line);
    if (first && header) {
      first = false;
      if (f != std::vector<std::string>{"id", "x", "y"} &&
          f != std::vector<std::string>{"elevation", "id", "latitude", "latitudeProjected",
                                        "longitude", "longitudeProjected"})
        throw std::invalid_argument("nodes header must be id,x,y or the legacy six-column header");
      continue;
    }
    first = false;
    if (f.size() != 3 && f.size() != 6) throw std::invalid_argument("invalid coordinate row");
    const Node node = natural(f[f.size() == 3 ? 0 : 1]);
    const Point p = f.size() == 3 ? Point{real(f[1]), real(f[2])} : Point{real(f[3]), real(f[5])};
    if (!points.emplace(node, p).second) throw std::invalid_argument("duplicate node id");
  }
}
struct Dimacs {
  std::size_t nodes{};
  std::vector<RawEdge> edges;
};
Dimacs read_dimacs(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open DIMACS file: " + path.string());
  Dimacs result;
  std::size_t expected{};
  std::string line;
  bool header = false;
  while (std::getline(in, line)) {
    std::istringstream row(line);
    std::string kind, extra;
    if (!(row >> kind) || kind == "c") continue;
    if (kind == "p") {
      std::string problem, n, m;
      if (header || !(row >> problem >> n >> m) || problem != "sp" || row >> extra)
        throw std::invalid_argument("invalid DIMACS p sp header");
      result.nodes = natural(n);
      expected = natural(m);
      header = true;
    } else if (kind == "a") {
      std::string f, t, w;
      if (!header || !(row >> f >> t >> w) || row >> extra)
        throw std::invalid_argument("invalid DIMACS arc");
      auto from = natural(f), to = natural(t);
      if (!from || !to || from > result.nodes || to > result.nodes)
        throw std::invalid_argument("DIMACS vertices must be 1-based and in range");
      result.edges.push_back({from - 1, to - 1, {w}});
    } else
      throw std::invalid_argument(
          "expected DIMACS p/a/c records; origin-destination pair files are not graph files");
  }
  if (!header || !result.nodes || result.edges.size() != expected)
    throw std::invalid_argument("DIMACS header or arc count mismatch");
  return result;
}
}  // namespace
Graph load_graph(const std::filesystem::path& path, LoadOptions options) {
  std::vector<RawEdge> edges;
  Points points;
  std::size_t nodes{}, dimensions{};
  if (options.second_weights && options.format != "dimacs")
    throw std::invalid_argument("--second-weights requires dimacs format");
  if (options.format == "dimacs") {
    if (options.model != Model::additive || !options.second_weights)
      throw std::invalid_argument("DIMACS needs additive model and paired --second-weights");
    auto first = read_dimacs(path), second = read_dimacs(*options.second_weights);
    if (first.nodes != second.nodes || first.edges.size() != second.edges.size())
      throw std::invalid_argument("DIMACS graph sizes differ");
    edges = std::move(first.edges);
    nodes = first.nodes;
    dimensions = 2;
    for (std::size_t i = 0; i < edges.size(); ++i) {
      if (edges[i].from != second.edges[i].from || edges[i].to != second.edges[i].to)
        throw std::invalid_argument("paired DIMACS arc ordering differs");
      edges[i].costs.push_back(second.edges[i].costs[0]);
    }
  } else {
    if (options.format != "csv" && options.format != "legacy-edges" &&
        options.format != "legacy-random" && options.format != "legacy-full")
      throw std::invalid_argument("unknown graph format: " + options.format);
    if (options.model == Model::ev && options.format == "legacy-random")
      throw std::invalid_argument("legacy-random costs do not define EV consumption");
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open graph: " + path.string());
    if (options.format == "legacy-full") read_nodes(in, points, false, true);
    std::string line;
    bool header = options.format == "csv" || options.format == "legacy-edges";
    std::size_t row_number = 0;
    while (std::getline(in, line)) {
      ++row_number;
      line = trim(line);
      if (line.empty() || line[0] == '#') continue;
      try {
        auto f = split(line);
        if (header) {
          header = false;
          if (options.format == "csv") {
            if (f.size() < 4 || f[0] != "from" || f[1] != "to")
              throw std::invalid_argument(
                  "CSV header must start from,to and name at least two criteria");
            dimensions = f.size() - 2;
            if (options.model == Model::ev &&
                f != std::vector<std::string>{"from", "to", "time", "consumption"})
              throw std::invalid_argument("EV CSV header must be from,to,time,consumption");
          } else if (f != std::vector<std::string>{"comsumption", "fromId", "time", "toId"} &&
                     f != std::vector<std::string>{"consumption", "fromId", "time", "toId"})
            throw std::invalid_argument("unrecognized legacy edge header");
          continue;
        }
        if (options.format == "csv") {
          if (f.size() != dimensions + 2)
            throw std::invalid_argument("inconsistent CSV row dimension");
          edges.push_back({natural(f[0]), natural(f[1]), {f.begin() + 2, f.end()}});
        } else if (options.format == "legacy-random") {
          if (f.size() != 5)
            throw std::invalid_argument("legacy-random requires from,to,time,cost1,cost2");
          dimensions = 3;
          edges.push_back({natural(f[0]), natural(f[1]), {f[2], f[3], f[4]}});
        } else {
          if (f.size() != 4)
            throw std::invalid_argument("legacy edge row requires consumption,from,time,to");
          dimensions = 2;
          edges.push_back({natural(f[1]), natural(f[3]), {f[2], f[0]}});
        }
      } catch (const std::exception& e) {
        throw std::invalid_argument(path.string() + ":" + std::to_string(row_number) + ": " +
                                    e.what());
      }
    }
    if (header || dimensions < 2) throw std::invalid_argument("missing graph header or edges");
  }
  if (options.nodes) {
    if (options.format == "legacy-full")
      throw std::invalid_argument("legacy-full already contains nodes");
    std::ifstream in(*options.nodes);
    if (!in) throw std::runtime_error("cannot open nodes: " + options.nodes->string());
    read_nodes(in, points, true, false);
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
