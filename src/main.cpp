#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "pareto/pareto.hpp"

namespace {
using namespace pareto;
using Arguments = std::map<std::string, std::string>;
std::vector<std::string> split(const std::string& text) {
  if (text.empty() || text.back() == ',') throw std::invalid_argument("empty list element");
  std::istringstream in(text);
  std::vector<std::string> result;
  std::string item;
  while (std::getline(in, item, ',')) {
    if (item.empty()) throw std::invalid_argument("empty list element");
    result.push_back(item);
  }
  return result;
}
std::string get(const Arguments& args, const std::string& key, const std::string& fallback = {}) {
  const auto it = args.find(key);
  return it == args.end() ? fallback : it->second;
}
std::size_t number(const std::string& text) {
  std::size_t value{};
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid nonnegative integer: " + text);
  return value;
}
std::int64_t signed_number(const std::string& text) {
  std::int64_t value{};
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid integer: " + text);
  return value;
}
double real(const std::string& text) {
  std::size_t pos{};
  const auto result = std::stod(text, &pos);
  if (pos != text.size() || !std::isfinite(result))
    throw std::invalid_argument("invalid finite number: " + text);
  return result;
}
std::vector<double> doubles(const std::string& text) {
  std::vector<double> result;
  for (const auto& item : split(text)) result.push_back(real(item));
  return result;
}
std::uint32_t seed(const Arguments& args) {
  auto value = number(get(args, "seed", "42"));
  if (value > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("seed exceeds uint32 range");
  return static_cast<std::uint32_t>(value);
}
Arguments parse(int argc, char** argv, const std::string& command) {
  const std::set<std::string> common{
      "input", "preset",         "nodes-count", "seed", "format",     "model",   "capacity",
      "nodes", "second-weights", "target",      "k",    "max-states", "triangle"};
  const std::set<std::string> query{"source",   "algorithm", "output",  "cover",
                                    "epsilons", "caps",      "ellipse", "max-labels"};
  const std::set<std::string> benchmark{"algorithms", "repeats", "queries", "max-labels"};
  Arguments args;
  for (int i = 2; i < argc; i += 2) {
    const std::string flag = argv[i];
    if (!flag.starts_with("--") || i + 1 >= argc)
      throw std::invalid_argument("expected --option value; see --help");
    const auto key = flag.substr(2);
    const bool accepted = common.contains(key) ||
                          ((command == "query" || command == "demo") && query.contains(key)) ||
                          (command == "benchmark" && benchmark.contains(key)) ||
                          (command == "preprocess" && key == "output");
    if (!accepted) throw std::invalid_argument("unknown option for " + command + ": " + flag);
    if (!args.emplace(key, argv[i + 1]).second)
      throw std::invalid_argument("duplicate option: " + flag);
  }
  if (args.contains("triangle") && get(args, "triangle") != "true" &&
      get(args, "triangle") != "false")
    throw std::invalid_argument("triangle must be true or false");
  if (args.contains("input") && args.contains("preset"))
    throw std::invalid_argument("choose --input or --preset");
  if (!args.contains("input"))
    for (const auto* key : {"format", "model", "capacity", "nodes", "second-weights"})
      if (args.contains(key))
        throw std::invalid_argument(std::string("--") + key + " requires --input");
  if (args.contains("input"))
    for (const auto* key : {"nodes-count", "seed"})
      if (args.contains(key) && command != "benchmark")
        throw std::invalid_argument(std::string("--") + key + " is for generated graphs");
  if ((command == "query" || command == "demo") && args.contains("cover"))
    for (const auto* key : {"k", "max-states", "triangle"})
      if (args.contains(key))
        throw std::invalid_argument(std::string("--") + key + " conflicts with --cover");
  return args;
}
Graph demo_graph() {
  Graph graph(6);
  graph.add_edge(0, 1, {2, 8});
  graph.add_edge(1, 5, {2, 8});
  graph.add_edge(0, 2, {4, 3});
  graph.add_edge(2, 5, {4, 3});
  graph.add_edge(0, 3, {6, 1});
  graph.add_edge(3, 5, {6, 1});
  graph.add_edge(0, 4, {7, 9});
  graph.add_edge(4, 5, {7, 9});
  return graph;
}
Graph graph_from(const Arguments& args, bool demo) {
  if (args.contains("input")) {
    LoadOptions options;
    options.format = get(args, "format", "csv");
    const auto model = get(args, "model", "additive");
    if (model != "additive" && model != "ev")
      throw std::invalid_argument("model must be additive or ev");
    options.model = model == "ev" ? Model::ev : Model::additive;
    if (args.contains("capacity") && options.model != Model::ev)
      throw std::invalid_argument("capacity requires EV model");
    options.capacity = signed_number(get(args, "capacity", "40000000"));
    if (args.contains("nodes")) options.nodes = get(args, "nodes");
    if (args.contains("second-weights")) options.second_weights = get(args, "second-weights");
    return load_graph(get(args, "input"), options);
  }
  if (demo && !args.contains("preset")) return demo_graph();
  return generate_graph(get(args, "preset", "independent2"), number(get(args, "nodes-count", "64")),
                        seed(args));
}
std::vector<Node> targets_from(const Arguments& args, const Graph& graph) {
  std::vector<Node> result;
  for (const auto& item : split(get(args, "target", std::to_string(graph.size() - 1))))
    result.push_back(number(item));
  return result;
}
Cover cover_from(const Arguments& args, const Graph& graph, const std::vector<Node>& targets) {
  if (args.contains("cover")) return load_cover(graph, get(args, "cover"));
  const auto triangle = get(args, "triangle", "false");
  return build_cover(graph, targets,
                     {number(get(args, "k", "16")), number(get(args, "max-states", "2000000")),
                      triangle == "true"});
}
std::string quote(const std::string& text) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      out << '\\' << c;
    else if (c < 32)
      out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
          << std::dec;
    else
      out << c;
  }
  out << '"';
  return out.str();
}
void print_cost(std::ostream& out, const Cost& cost) {
  out << '[';
  std::visit(
      [&](const auto& vector) {
        bool first = true;
        for (auto x : vector) {
          if (!first) out << ',';
          out << x;
          first = false;
        }
      },
      cost);
  out << ']';
}
template <class T>
void print_list(std::ostream& out, const std::vector<T>& vector) {
  out << '[';
  for (std::size_t i = 0; i < vector.size(); ++i) {
    if (i) out << ',';
    out << vector[i];
  }
  out << ']';
}
void print_json(const Graph& graph, const SearchResult& result, const SearchOptions& options,
                Node source, double preprocessing) {
  const auto& s = result.statistics;
  std::cout << "{\"schema_version\":1,\"algorithm\":" << quote(algorithm_name(options.algorithm))
            << ",\"model\":" << quote(graph.model() == Model::ev ? "ev" : "additive")
            << ",\"graph\":" << quote(graph.fingerprint()) << ",\"source\":" << source
            << ",\"approximate\":" << (result.approximate ? "true" : "false")
            << ",\"preprocessing_seconds\":" << preprocessing
            << ",\"statistics\":{\"seconds\":" << s.seconds << ",\"generated\":" << s.generated
            << ",\"expanded\":" << s.expanded << ",\"dominated\":" << s.dominated
            << ",\"peak_queue\":" << s.peak_queue << ",\"bound_pruned\":" << s.bound_pruned
            << "},\"targets\":[";
  bool first_target = true;
  for (const auto& [target, routes] : result.routes) {
    if (!first_target) std::cout << ',';
    first_target = false;
    std::cout << "{\"node\":" << target << ",\"routes\":[";
    for (std::size_t i = 0; i < routes.size(); ++i) {
      if (i) std::cout << ',';
      const auto& route = routes[i];
      std::cout << "{\"cost\":";
      print_cost(std::cout, route.cost);
      std::cout << ",\"nodes\":";
      print_list(std::cout, route.nodes);
      std::cout << ",\"edges\":";
      print_list(std::cout, route.edges);
      std::cout << '}';
    }
    std::cout << "]}";
  }
  std::cout << "]}\n";
}
std::string csv(const std::string& text) {
  std::string result = "\"";
  for (char c : text) {
    if (c == '"') result += '"';
    result += c;
  }
  return result + '"';
}
std::string platform() {
#if defined(__APPLE__)
  return "macOS";
#elif defined(__linux__)
  return "Linux";
#elif defined(_WIN32)
  return "Windows";
#else
  return "unknown";
#endif
}
std::string build_type() {
#ifdef NDEBUG
  return "Release";
#else
  return "Debug";
#endif
}
bool equivalent(const SearchResult& a, const SearchResult& b) {
  if (a.routes.size() != b.routes.size()) return false;
  for (const auto& [target, routes] : a.routes) {
    const auto it = b.routes.find(target);
    if (it == b.routes.end() || routes.size() != it->second.size()) return false;
    for (std::size_t r = 0; r < routes.size(); ++r) {
      if (routes[r].cost.index() == 1) {
        if (routes[r].cost != it->second[r].cost) return false;
      } else {
        const auto x = values(routes[r].cost), y = values(it->second[r].cost);
        for (std::size_t d = 0; d < x.size(); ++d)
          if (std::abs(x[d] - y[d]) > 1e-10 * std::max({1.0, std::abs(x[d]), std::abs(y[d])}))
            return false;
      }
    }
  }
  return true;
}
void benchmark(const Arguments& args) {
  const auto graph = graph_from(args, false);
  const auto targets = targets_from(args, graph);
  const auto repeats = number(get(args, "repeats", "3")),
             queries = number(get(args, "queries", "4"));
  if (!repeats || !queries || repeats > 10000 || queries > graph.size())
    throw std::invalid_argument("need repeats in 1..10000 and queries in 1..node count");
  const auto max_labels = number(get(args, "max-labels", "1000000"));
  std::vector<Algorithm> algorithms;
  for (const auto& name : split(get(args, "algorithms", "mls,t-mls,kpc-mls,t-kpc-mls")))
    algorithms.push_back(parse_algorithm(name));
  std::optional<Cover> cover;
  if (!std::any_of(algorithms.begin(), algorithms.end(), uses_cover))
    for (const auto* key : {"k", "max-states", "triangle"})
      if (args.contains(key))
        throw std::invalid_argument(std::string("--") + key + " requires a cover algorithm");
  if (std::any_of(algorithms.begin(), algorithms.end(), uses_cover))
    cover = cover_from(args, graph, targets);
  const auto fingerprint = graph.fingerprint();
  std::cout << "preset,model,graph,seed,nodes,edges,source,targets,algorithm,repeat,preprocessing_"
               "seconds,query_seconds,generated,expanded,peak_queue,bound_pruned,compiler,build,"
               "platform\n";
  for (std::size_t q = 0; q < queries; ++q) {
    const Node source = q;  // Same deterministic source sequence for every algorithm.
    SearchOptions baseline_options;
    baseline_options.max_labels = max_labels;
    const auto baseline = search(graph, source, targets, baseline_options);
    for (auto algorithm : algorithms)
      for (std::size_t r = 0; r < repeats; ++r) {
        SearchOptions options;
        options.algorithm = algorithm;
        options.max_labels = max_labels;
        const auto result = search(graph, source, targets, options, cover ? &*cover : nullptr);
        if (!equivalent(baseline, result))
          throw std::runtime_error("benchmark Pareto-set mismatch for " +
                                   algorithm_name(algorithm));
        const auto& s = result.statistics;
        std::cout << csv(get(args, "preset", args.contains("input") ? "external" : "independent2"))
                  << ',' << (graph.model() == Model::ev ? "ev" : "additive") << ',' << fingerprint
                  << ',' << seed(args) << ',' << graph.size() << ',' << graph.edges().size() << ','
                  << source << ',' << csv(get(args, "target", std::to_string(graph.size() - 1)))
                  << ',' << algorithm_name(algorithm) << ',' << r << ','
                  << (uses_cover(algorithm) ? cover->seconds() : 0) << ',' << s.seconds << ','
                  << s.generated << ',' << s.expanded << ',' << s.peak_queue << ','
                  << s.bound_pruned << ',' << csv(__VERSION__) << ',' << build_type() << ','
                  << platform() << '\n';
      }
  }
}
void help() {
  std::cout << R"(pareto-paths 0.1.0 - multicriteria routing with k-path-cover acceleration

Usage:
  pareto-paths demo [--output text|json]
  pareto-paths query (--input FILE | --preset NAME) [options]
  pareto-paths preprocess (--input FILE | --preset NAME) --output FILE [options]
  pareto-paths benchmark [--preset NAME | --input FILE] [options]

Graph options:
  --format csv|legacy-edges|legacy-random|legacy-full|dimacs
  --model additive|ev       --capacity INT (EV only)
  --nodes FILE              --second-weights FILE (paired DIMACS)
  --preset independent2|independent3|correlated2|correlated3|reciprocal|ev
  --nodes-count 64          --seed 42

Search options:
  --source 0               --target ID[,ID...] (default: last vertex)
  --algorithm mls|t-mls|kpc-mls|t-kpc-mls|ima|blset|kpc-ima|kpc-ima-straight
  --output text|json        --max-labels 1000000
  --cover FILE              --k 16              --max-states 2000000
  --triangle true|false     Optional exact triangle shortcut pruning (default false)
  --epsilons E[,E...]       --caps R[,R...]       --ellipse R
  Approximate pruning is opt-in; ellipse requires node coordinates.

Benchmark options:
  --algorithms mls,t-mls,kpc-mls,t-kpc-mls
  --repeats 3              --queries 4 (sources 0..queries-1)
  Writes CSV to stdout; preprocessing is separate from query time.

See README.md and docs/algorithms.md for assumptions and limitations.
)";
}
}  // namespace
int main(int argc, char** argv) {
  using namespace pareto;
  try {
    std::cout << std::setprecision(17);
    if (argc == 1 || std::string(argv[1]) == "--help") {
      help();
      return 0;
    }
    const std::string command = argv[1];
    if (command == "--version") {
      std::cout << "pareto-paths 0.1.0 | " << __VERSION__ << " | " << platform() << " | "
                << build_type() << '\n';
      return 0;
    }
    if (command != "demo" && command != "query" && command != "preprocess" &&
        command != "benchmark")
      throw std::invalid_argument("unknown command: " + command);
    const auto args = parse(argc, argv, command);
    if ((command == "query" || command == "preprocess") && !args.contains("input") &&
        !args.contains("preset"))
      throw std::invalid_argument("provide --input or --preset");
    if (command == "benchmark") {
      benchmark(args);
      return 0;
    }
    const auto graph = graph_from(args, command == "demo");
    const auto targets = targets_from(args, graph);
    if (command == "preprocess") {
      if (!args.contains("output"))
        throw std::invalid_argument("preprocess requires --output FILE");
      const auto cover = cover_from(args, graph, targets);
      save_cover(cover, get(args, "output"));
      std::cout << "Saved cover: "
                << std::count(cover.retained().begin(), cover.retained().end(), true) << '/'
                << graph.size() << " vertices, " << cover.shortcuts().size() << " shortcuts, "
                << cover.seconds() << " seconds\n";
      return 0;
    }
    SearchOptions options;
    options.algorithm =
        parse_algorithm(get(args, "algorithm", command == "demo" ? "t-kpc-mls" : "mls"));
    options.max_labels = number(get(args, "max-labels", "1000000"));
    if (args.contains("epsilons")) options.epsilons = doubles(get(args, "epsilons"));
    if (args.contains("caps")) options.capping = doubles(get(args, "caps"));
    if (args.contains("ellipse")) options.ellipse = real(get(args, "ellipse"));
    const auto output = get(args, "output", "text");
    if (output != "text" && output != "json")
      throw std::invalid_argument("output must be text or json");
    if (!uses_cover(options.algorithm))
      for (const auto* key : {"cover", "k", "max-states", "triangle"})
        if (args.contains(key))
          throw std::invalid_argument(std::string("--") + key + " requires a cover algorithm");
    std::optional<Cover> cover;
    if (uses_cover(options.algorithm)) cover = cover_from(args, graph, targets);
    const auto source = number(get(args, "source", "0"));
    const auto result = search(graph, source, targets, options, cover ? &*cover : nullptr);
    if (output == "json")
      print_json(graph, result, options, source, cover ? cover->seconds() : 0);
    else {
      std::cout << "PARETO / PATHS\n"
                << algorithm_name(options.algorithm) << " | " << graph.size() << " vertices | "
                << graph.edges().size() << " edges\n"
                << (result.approximate
                        ? "Approximate search (pruning enabled)"
                        : "Exact search (floating-point arithmetic for additive costs)")
                << "\n\n";
      for (const auto& [target, routes] : result.routes) {
        std::cout << source << " -> " << target << "  |  " << routes.size() << " Pareto route(s)\n";
        for (const auto& route : routes) {
          std::cout << "  ";
          print_cost(std::cout, route.cost);
          std::cout << "  ";
          for (std::size_t i = 0; i < route.nodes.size(); ++i) {
            if (i) std::cout << " -> ";
            std::cout << route.nodes[i];
          }
          std::cout << '\n';
        }
      }
      std::cout << "\nExpanded " << result.statistics.expanded << " labels in "
                << result.statistics.seconds << " s";
      if (cover) std::cout << " | preprocessing " << cover->seconds() << " s";
      std::cout << '\n';
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "pareto-paths: " << e.what() << '\n';
    return 1;
  }
}
