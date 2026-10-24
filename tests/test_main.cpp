#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "pareto/pareto.hpp"

namespace {
using namespace pareto;
std::size_t checks = 0;
void check(bool value, const std::string& message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class F>
void throws(F&& action, const std::string& message) {
  bool caught = false;
  try {
    action();
  } catch (const std::exception&) {
    caught = true;
  }
  check(caught, message);
}
const std::vector<Algorithm> exact{Algorithm::mls,       Algorithm::t_mls, Algorithm::kpc_mls,
                                   Algorithm::t_kpc_mls, Algorithm::ima,   Algorithm::blset,
                                   Algorithm::t_kpc_ima};
static_assert(!std::is_aggregate_v<Cover>);
static_assert(
    std::is_same_v<decltype(std::declval<Cover&>().shortcuts()), const std::vector<Shortcut>&>);
// Independent reference: enumerate simple paths, then filter the complete list.
// Nonnegative additive costs mean removing a cycle can never worsen a route.
std::vector<AdditiveCost> oracle(const Graph& graph, Node source, Node target) {
  std::vector<AdditiveCost> candidates;
  std::vector<bool> visited(graph.size());
  std::function<void(Node, AdditiveCost)> enumerate = [&](Node node, AdditiveCost sum) {
    if (node == target) {
      candidates.push_back(sum);
      return;
    }
    visited[node] = true;
    for (auto id : graph.outgoing(node)) {
      const auto& e = graph.edges()[id];
      if (visited[e.to]) continue;
      auto next = sum;
      const auto& cost = std::get<AdditiveCost>(e.cost);
      for (std::size_t i = 0; i < next.size(); ++i) next[i] += cost[i];
      enumerate(e.to, std::move(next));
    }
    visited[node] = false;
  };
  enumerate(source, AdditiveCost(graph.dimensions(), 0));
  std::set<AdditiveCost> frontier;
  for (const auto& c : candidates) {
    bool discarded = false;
    for (const auto& other : candidates) {
      if (c == other) continue;
      bool no_worse = true;
      for (std::size_t d = 0; d < c.size(); ++d) no_worse = no_worse && other[d] <= c[d];
      if (no_worse) {
        discarded = true;
        break;
      }
    }
    if (!discarded) frontier.insert(c);
  }
  return {frontier.begin(), frontier.end()};
}
void validate_paths(const Graph& graph, Node source, const SearchResult& result) {
  for (const auto& [target, routes] : result.routes)
    for (const auto& route : routes) {
      check(route.nodes.size() == route.edges.size() + 1, "path node count");
      check(route.nodes.front() == source && route.nodes.back() == target, "route endpoints");
      if (graph.model() == Model::additive) {
        AdditiveCost sum(graph.dimensions());
        for (std::size_t i = 0; i < route.edges.size(); ++i) {
          const auto& e = graph.edges().at(route.edges[i]);
          check(e.from == route.nodes[i] && e.to == route.nodes[i + 1],
                "edge identity and connectivity");
          for (std::size_t d = 0; d < sum.size(); ++d) sum[d] += std::get<AdditiveCost>(e.cost)[d];
        }
        const auto& cost = std::get<AdditiveCost>(route.cost);
        for (std::size_t d = 0; d < sum.size(); ++d)
          check(std::abs(sum[d] - cost[d]) < 1e-9, "reconstructed additive cost");
      } else {
        const auto profile = std::get<BatteryCost>(route.cost);
        for (std::int64_t initial = 0; initial <= graph.capacity(); ++initial) {
          auto charge = initial;
          std::int64_t time = 0;
          bool feasible = true;
          for (std::size_t i = 0; i < route.edges.size(); ++i) {
            const auto& e = graph.edges().at(route.edges[i]);
            check(e.from == route.nodes[i] && e.to == route.nodes[i + 1], "EV edge identity");
            const auto c = std::get<BatteryCost>(e.cost);
            charge = std::min(graph.capacity(), charge - c[2]);
            time += c[0];
            if (charge < 0) {
              feasible = false;
              break;
            }
          }
          check(feasible == (initial >= profile[1]), "EV initial-charge feasibility");
          if (feasible) {
            check(charge == std::min(initial - profile[2], profile[3]),
                  "EV physical simulation vs profile");
            check(time == profile[0], "EV traversal time");
          }
        }
      }
    }
}
std::vector<AdditiveCost> additive_results(const SearchResult& result, Node target) {
  std::vector<AdditiveCost> costs;
  for (const auto& route : result.routes.at(target))
    costs.push_back(std::get<AdditiveCost>(route.cost));
  return costs;
}
void compare_algorithms(const Graph& graph, Node source, Node target) {
  auto expected = oracle(graph, source, target);
  for (std::size_t k : {2, 3, 5})
    for (bool triangle : {false, true}) {
      const auto cover = build_cover(graph, {target}, {k, 2'000'000, triangle});
      for (auto a : exact) {
        SearchOptions options;
        options.algorithm = a;
        const auto result =
            search(graph, source, {target}, options, uses_cover(a) ? &cover : nullptr);
        check(!result.approximate, "exact search flag");
        const auto actual = additive_results(result, target);
        check(actual.size() == expected.size(),
              "oracle set-size mismatch: " + algorithm_name(a) + " k=" + std::to_string(k));
        for (std::size_t r = 0; r < actual.size(); ++r)
          for (std::size_t d = 0; d < graph.dimensions(); ++d)
            check(std::abs(actual[r][d] - expected[r][d]) <=
                      1e-10 * std::max(1.0, std::abs(expected[r][d])),
                  "oracle cost mismatch: " + algorithm_name(a) + " k=" + std::to_string(k));
        validate_paths(graph, source, result);
      }
    }
}
void triangle_pruning() {
  Graph graph(3);
  graph.add_edge(0, 1, {1, 1});
  graph.add_edge(1, 2, {1, 1});
  graph.add_edge(0, 2, {3, 3});
  auto original = build_cover(graph, {0, 1, 2}, {3, 10000});
  auto pruned = build_cover(graph, {0, 1, 2}, {3, 10000, true});
  check(original.shortcuts().size() == 3 && pruned.shortcuts().size() == 2,
        "triangle removes redundant direct edge");
  SearchOptions options;
  options.algorithm = Algorithm::t_kpc_mls;
  auto result = search(graph, 0, {2}, options, &pruned);
  check(additive_results(result, 2) == std::vector<AdditiveCost>{{2, 2}},
        "triangle preserves additive route");
  validate_paths(graph, 0, result);
  Graph zeros(4);
  for (Node a = 0; a < 4; ++a)
    for (Node b = 0; b < 4; ++b)
      if (a != b) zeros.add_edge(a, b, {0, 0});
  auto zero_cover = build_cover(zeros, {0, 1, 2, 3}, {3, 100000, true});
  for (Node source = 0; source < 4; ++source) {
    const auto routes = search(zeros, source, {0, 1, 2, 3}, options, &zero_cover);
    for (Node target = 0; target < 4; ++target)
      check(additive_results(routes, target) == std::vector<AdditiveCost>{{0, 0}},
            "sequential zero-cycle pruning preserves reachability");
    validate_paths(zeros, source, routes);
  }
  auto battery = Graph::electric(3, 10);
  battery.add_ev_edge(0, 1, 1, 2);
  battery.add_ev_edge(1, 2, 1, 2);
  battery.add_ev_edge(0, 2, 3, 5);
  auto ev_cover = build_cover(battery, {0, 1, 2}, {3, 10000, true});
  check(ev_cover.shortcuts().size() == 2, "EV triangle pruning");
  auto ev_result = search(battery, 0, {2}, options, &ev_cover);
  check(std::get<BatteryCost>(ev_result.routes.at(2)[0].cost) == BatteryCost{2, 4, 4, 6},
        "EV triangle profile");
  validate_paths(battery, 0, ev_result);
}
void hand_calculated() {
  Graph graph(7);
  graph.add_edge(0, 1, {2, 8});
  graph.add_edge(1, 5, {2, 8});
  graph.add_edge(0, 2, {4, 3});
  graph.add_edge(2, 5, {4, 3});
  graph.add_edge(0, 3, {6, 1});
  graph.add_edge(3, 5, {6, 1});
  graph.add_edge(0, 4, {7, 9});
  graph.add_edge(4, 5, {7, 9});
  graph.add_edge(0, 1, {2, 8});  // duplicate parallel edge
  graph.add_edge(0, 1, {1, 9});  // distinct parallel-edge tradeoff
  graph.add_edge(1, 1, {0, 0});
  graph.add_edge(2, 0, {0, 0});
  check(oracle(graph, 0, 5) == std::vector<AdditiveCost>{{3, 17}, {4, 16}, {8, 6}, {12, 2}},
        "independent fixture expectation");
  compare_algorithms(graph, 0, 5);
  compare_algorithms(graph, 0, 6);
  compare_algorithms(graph, 0, 0);
  const auto all = search(graph, 0);
  check(all.routes.size() == 7 && all.routes.at(6).empty(), "all-target semantics");
  const auto cover = build_cover(graph, {5}, {3, 100000});
  check(!cover.retained()[0], "fixture exercises non-cover source");
  auto multi = build_cover(graph, {2, 5}, {3, 100000});
  SearchOptions options;
  options.algorithm = Algorithm::t_kpc_mls;
  const auto result = search(graph, 0, {2, 5}, options, &multi);
  check(additive_results(result, 2) == oracle(graph, 0, 2), "multiple retained goals");
  check(additive_results(result, 5) == oracle(graph, 0, 5), "multiple goal continuation");
}
void randomized() {
  std::mt19937 rng(4821);
  for (std::size_t dimensions : {2, 3, 4})
    for (int instance = 0; instance < 12; ++instance) {
      Graph graph(6, dimensions);
      for (Node from = 0; from < graph.size(); ++from)
        for (Node to = 0; to < graph.size(); ++to) {
          if (rng() % 4 != 0) continue;
          AdditiveCost cost(dimensions);
          for (auto& x : cost) x = rng() % 6;
          graph.add_edge(from, to, std::move(cost));
        }
      compare_algorithms(graph, 0, 5);
      compare_algorithms(graph, 3, 1);
    }
}
void ev() {
  auto graph = Graph::electric(4, 10);
  graph.add_ev_edge(0, 1, 2, 6);
  graph.add_ev_edge(1, 3, 2, 6);
  graph.add_ev_edge(0, 2, 4, 3);
  graph.add_ev_edge(2, 3, 4, -2);
  graph.add_ev_edge(0, 3, 10, 0);
  for (auto a : {Algorithm::mls, Algorithm::t_mls, Algorithm::kpc_mls, Algorithm::t_kpc_mls}) {
    const auto cover = build_cover(graph, {3}, {3, 10000});
    SearchOptions options;
    options.algorithm = a;
    const auto result = search(graph, 0, {3}, options, uses_cover(a) ? &cover : nullptr);
    const auto& routes = result.routes.at(3);
    check(routes.size() == 2, "EV infeasible path removed");
    check(std::get<BatteryCost>(routes[0].cost) == BatteryCost{8, 3, 1, 9}, "EV regenerative path");
    check(std::get<BatteryCost>(routes[1].cost) == BatteryCost{10, 0, 0, 10},
          "EV zero-energy path");
    validate_paths(graph, 0, result);
  }
  auto cyclic = Graph::electric(3, 6);
  cyclic.add_ev_edge(0, 1, 1, 5);
  cyclic.add_ev_edge(1, 1, 1, -2);
  cyclic.add_ev_edge(1, 2, 1, 5);
  const auto cover = build_cover(cyclic, {2}, {3, 10000});
  check(cover.retained()[1], "regenerative cycle retained");
  for (auto a : {Algorithm::mls, Algorithm::t_mls, Algorithm::kpc_mls, Algorithm::t_kpc_mls}) {
    SearchOptions options;
    options.algorithm = a;
    const auto result = search(cyclic, 0, {2}, options, uses_cover(a) ? &cover : nullptr);
    const auto& routes = result.routes.at(2);
    check(routes.size() == 2, "regenerative loop Pareto set");
    check(std::get<BatteryCost>(routes[0].cost) == BatteryCost{4, 6, 6, 0},
          "two regenerative loops");
    check(std::get<BatteryCost>(routes[1].cost) == BatteryCost{5, 5, 4, 1},
          "three regenerative loops");
    validate_paths(cyclic, 0, result);
  }
  SearchOptions approximate;
  approximate.epsilons = {0.9, 0.9, 0.9, 0.9};
  approximate.max_labels = 100;
  const auto approximate_result = search(cyclic, 0, {2}, approximate);
  check(approximate_result.approximate, "EV epsilon enabled");
  validate_paths(cyclic, 0, approximate_result);
  const auto big = std::numeric_limits<std::int64_t>::max();
  for (bool reverse : {false, true}) {
    auto precision = Graph::electric(2, big - 1);
    precision.add_ev_edge(0, 1, 1, reverse ? 9007199254740992LL : 9007199254740993LL);
    precision.add_ev_edge(0, 1, 1, reverse ? 9007199254740993LL : 9007199254740992LL);
    auto precise_cover = build_cover(precision, {1}, {2, 100});
    for (auto algorithm :
         {Algorithm::mls, Algorithm::t_mls, Algorithm::kpc_mls, Algorithm::t_kpc_mls}) {
      for (bool explicit_exact : {false, true}) {
        SearchOptions options;
        options.algorithm = algorithm;
        if (explicit_exact) options.epsilons = {1, 1, 1, 1};
        const auto result =
            search(precision, 0, {1}, options, uses_cover(algorithm) ? &precise_cover : nullptr);
        check(result.routes.at(1).size() == 1, "large EV parallel-cost frontier");
        check(std::get<BatteryCost>(result.routes.at(1)[0].cost)[2] == 9007199254740992LL,
              "large EV integer dominance");
      }
    }
  }
  auto large = Graph::electric(2, big - 1);
  large.add_ev_edge(0, 1, 1, 9007199254740993LL);
  check(std::get<BatteryCost>(search(large, 0, {1}).routes.at(1)[0].cost)[2] == 9007199254740993LL,
        "EV integer precision beyond double");
  throws([&] { large.add_ev_edge(0, 1, 1, -big); }, "EV edge overflow");
  throws([&] { (void)compose(Cost{BatteryCost{big, 0, 0, 1}}, Cost{BatteryCost{1, 0, 0, 1}}); },
         "EV compose overflow");
}
void boundaries() {
  check(parse_algorithm("t-kpc-ima") == Algorithm::t_kpc_ima, "renamed t-kPC-IMA parser");
  check(algorithm_name(Algorithm::t_kpc_ima) == "t-kpc-ima", "renamed t-kPC-IMA output");
  check(uses_cover(Algorithm::t_kpc_ima), "t-kPC-IMA uses cover");
  throws([] { (void)parse_algorithm("kpc-ima"); }, "removed kPC-IMA name rejected");
  throws([] { (void)parse_algorithm("kpc-ima-straight"); }, "old straight name rejected");
  throws([] { Graph(0); }, "empty graph rejected");
  throws([] { Graph(2, 1); }, "single criterion rejected");
  Graph g(3);
  throws([&] { g.add_edge(0, 1, {-1, 2}); }, "negative additive cost");
  throws([&] { g.add_edge(0, 1, {1, std::numeric_limits<double>::infinity()}); },
         "nonfinite additive cost");
  throws([&] { g.add_edge(0, 1, {1}); }, "dimension mismatch");
  throws([&] { g.add_edge(0, 3, {1, 2}); }, "endpoint validation");
  throws([&] { (void)compose(Cost{AdditiveCost{1e308, 1}}, Cost{AdditiveCost{1e308, 1}}); },
         "additive overflow");
  g.add_edge(0, 1, {1, 2});
  g.add_edge(1, 2, {2, 1});
  throws([&] { (void)search(g, 3); }, "source validation");
  throws([&] { (void)search(g, 0, {3}); }, "target validation");
  throws([&] { (void)build_cover(g, {}); }, "cover requires goals");
  throws([&] { (void)build_cover(g, {2}, {1, 10}); }, "cover size validation");
  throws([&] { (void)build_cover(g, {2}, {3, 1}); }, "cover budget");
  SearchOptions options;
  options.algorithm = Algorithm::kpc_mls;
  throws([&] { (void)search(g, 0, {2}, options); }, "missing cover");
  Cover empty;
  throws([&] { (void)search(g, 0, {2}, options, &empty); }, "default-constructed cover rejected");
  const auto cover = build_cover(g, {2}, {3, 100});
  throws([&] { (void)search(g, 0, {1}, options, &cover); }, "non-retained goal");
  throws([&] { (void)search(g, 0, {}, options, &cover); }, "implicit cover goals rejected");
  Graph different(3);
  different.add_edge(0, 2, {1, 1});
  throws([&] { (void)search(different, 0, {2}, options, &cover); }, "wrong-graph cover");
  options = {};
  options.max_labels = 1;
  throws([&] { (void)search(g, 0, {2}, options); }, "label budget");
  options = {};
  options.epsilons = {0.8};
  throws([&] { (void)search(g, 0, {2}, options); }, "epsilon dimensions");
  options.epsilons = {0, 1};
  throws([&] { (void)search(g, 0, {2}, options); }, "epsilon bounds");
  options.epsilons = {0.9, 0.9};
  check(search(g, 0, {2}, options).approximate, "epsilon result labeling");
  options = {};
  options.capping = {1.1, 1.1};
  check(search(g, 0, {2}, options).approximate, "capping labeling");
  options = {};
  options.ellipse = 1.1;
  throws([&] { (void)search(g, 0, {2}, options); }, "ellipse requires coordinates");
  for (Node n = 0; n < 3; ++n) g.set_point(n, {static_cast<double>(n), 0});
  const auto ellipse = search(g, 0, {2}, options);
  check(ellipse.approximate && ellipse.routes.at(2).size() == 1, "ellipse route");
  validate_paths(g, 0, ellipse);
  options = {};
  options.algorithm = Algorithm::ima;
  throws([&] { (void)search(g, 0, {1, 2}, options); }, "IMA single goal");
  auto battery = Graph::electric(2, 10);
  throws([&] { (void)search(battery, 0, {1}, options); }, "IMA model validation");
  throws([&] { battery.add_ev_edge(0, 1, 0, -2); }, "zero-time EV edge rejected");
}
void io(const std::filesystem::path& dir) {
  auto write = [&](const std::string& name, const std::string& content) {
    std::ofstream out(dir / name);
    out << content;
    return dir / name;
  };
  auto csv = write("graph.csv", "from,to,time,distance\n0,1,2,8\n1,2,3,4\n");
  auto graph = load_graph(csv);
  check(graph.size() == 3 && graph.edges().size() == 2, "CSV import");
  throws([&] { (void)load_graph(dir / "missing"); }, "missing graph");
  auto bad = write("bad.csv", "from,to,time,distance\n0,1,2,3oops\n");
  throws([&] { (void)load_graph(bad); }, "numeric trailing garbage");
  auto short_row = write("short.csv", "from,to,time,distance\n0,1,2,\n");
  throws([&] { (void)load_graph(short_row); }, "empty field");
  for (const auto* removed : {"legacy-edges", "legacy-random", "legacy-full"}) {
    LoadOptions unsupported;
    unsupported.format = removed;
    throws([&] { (void)load_graph(csv, unsupported); }, "unsupported import format rejected");
  }
  auto old_order = write("wrong-order.csv", "consumption,fromId,time,toId\n8,0,2,1\n");
  throws([&] { (void)load_graph(old_order); }, "edge endpoints must precede criteria");
  auto fixed_order = write("normalized.csv", "from,to,time,consumption\n0,1,2,8\n1,2,3,-4\n");
  LoadOptions fixed_options;
  fixed_options.model = Model::ev;
  fixed_options.capacity = 10;
  auto normalized = load_graph(fixed_order, fixed_options);
  check(normalized.edges()[0].from == 0 && normalized.edges()[0].to == 1,
        "normalized OSM endpoints are the first two columns");
  check(std::get<BatteryCost>(search(normalized, 0, {2}).routes.at(2)[0].cost) ==
            BatteryCost{5, 8, 4, 6},
        "normalized OSM time/consumption order and regeneration");
  auto multidimensional = write("criteria.csv", "from,to,time,cost1,cost2\n0,1,2,3,4\n");
  auto three = load_graph(multidimensional);
  check(three.dimensions() == 3 &&
            std::get<AdditiveCost>(three.edges()[0].cost) == AdditiveCost{2, 3, 4},
        "CSV loads every named criterion in column order");
  auto duplicate_names = write("duplicate-names.csv", "from,to,time,time\n0,1,2,3\n");
  throws([&] { (void)load_graph(duplicate_names); }, "duplicate criterion names rejected");
  auto empty_name = write("empty-name.csv", "from,to,time,\n0,1,2,3\n");
  throws([&] { (void)load_graph(empty_name); }, "empty criterion name rejected");
  auto coordinates = write("nodes.csv", "id,x,y\n0,10,20\n1,11,21\n2,12,22\n3,13,23\n");
  LoadOptions coordinate_options;
  coordinate_options.nodes = coordinates;
  auto positioned = load_graph(csv, coordinate_options);
  check(positioned.size() == 4 && positioned.point(3)->x == 13 && positioned.point(3)->y == 23,
        "canonical coordinates preserve isolated nodes and x/y order");
  auto bad_nodes = write(
      "bad-nodes.csv",
      "elevation,id,latitude,latitudeProjected,longitude,longitudeProjected\n0,0,0,10,0,20\n");
  coordinate_options.nodes = bad_nodes;
  throws([&] { (void)load_graph(csv, coordinate_options); },
         "noncanonical coordinate header rejected");
  auto duplicate_nodes = write("duplicate-nodes.csv", "id,x,y\n0,10,20\n0,11,21\n");
  coordinate_options.nodes = duplicate_nodes;
  throws([&] { (void)load_graph(csv, coordinate_options); }, "duplicate coordinate ID rejected");
  auto no_nodes = write("empty-nodes.csv", "id,x,y\n");
  coordinate_options.nodes = no_nodes;
  throws([&] { (void)load_graph(csv, coordinate_options); }, "empty coordinate file rejected");
  LoadOptions opt;
  auto first = write("first.gr", "c sample\np sp 3 2\na 1 2 2\na 2 3 3\n");
  auto second = write("second.gr", "p sp 3 2\na 1 2 8\na 2 3 4\n");
  opt = {};
  opt.format = "dimacs";
  opt.second_weights = second;
  check(load_graph(first, opt).fingerprint() == graph.fingerprint(), "paired DIMACS import");
  auto invalid = write("invalid.gr", "p sp 3 2\na 1 3 8\na 2 3 4\n");
  opt.second_weights = invalid;
  throws([&] { (void)load_graph(first, opt); }, "paired arc order validation");
  const std::string combined_text =
      "c first criterion\np sp 4 2\na 1 2 2\na 2 3 3\n"
      "c second criterion\np sp 4 2\na 1 2 8\na 2 3 4\n";
  auto combined = write("combined.gr", combined_text);
  opt.second_weights.reset();
  auto combined_graph = load_graph(combined, opt);
  check(combined_graph.size() == 4, "combined DIMACS retains declared isolated vertices");
  check(additive_results(search(combined_graph, 0, {2}), 2) == std::vector<AdditiveCost>{{5, 12}},
        "combined DIMACS criteria pairing");
  throws([&] { (void)load_graph(first, opt); }, "single DIMACS graph needs second criterion");
  auto wrong_pair = write("wrong-pair.gr", "p sp 3 1\na 1 2 2\np sp 3 1\na 1 3 8\n");
  throws([&] { (void)load_graph(wrong_pair, opt); }, "combined DIMACS ordering mismatch");
  auto wrong_count = write("wrong-count.gr", "p sp 3 2\na 1 2 2\np sp 3 1\na 1 2 8\n");
  throws([&] { (void)load_graph(wrong_count, opt); }, "combined DIMACS count mismatch");
  auto third_block = write("third-block.gr", combined_text + "p sp 4 0\n");
  throws([&] { (void)load_graph(third_block, opt); }, "extra DIMACS graph block rejected");
  auto with_queries = write("test-prefix.gr", "1 3\n" + combined_text);
  throws([&] { (void)load_graph(with_queries, opt); },
         "private test-query prefix is not graph input");
  opt.second_weights = second;
  throws([&] { (void)load_graph(combined, opt); }, "ambiguous combined/separate DIMACS inputs");
  const auto cover = build_cover(graph, {2}, {3, 1000, true});
  const auto cache = dir / "graph.cover";
  save_cover(cover, cache);
  auto loaded = load_cover(graph, cache);
  check(loaded.triangle_pruning(), "triangle option cache metadata");
  SearchOptions search_opt;
  search_opt.algorithm = Algorithm::t_kpc_mls;
  check(additive_results(search(graph, 0, {2}, search_opt, &loaded), 2) == oracle(graph, 0, 2),
        "cover roundtrip");
  auto repeated = build_cover(graph, {2}, {3, 1000});
  check(repeated.shortcuts().size() == cover.shortcuts().size(),
        "repeated preprocessing has no accumulated state");
  Graph wrong(3);
  wrong.add_edge(0, 2, {1, 1});
  throws([&] { (void)load_cover(wrong, cache); }, "cache graph validation");
  // Even a recomputed checksum must not make an invalid endpoint usable.
  const auto malformed =
      "PARETO_COVER 2\n" + graph.fingerprint() + " 3 3 0\n101\n1 2\n1\n0 999 1 0\n";
  std::uint64_t digest = 14695981039346656037ULL;
  for (unsigned char c : malformed) {
    digest ^= c;
    digest *= 1099511628211ULL;
  }
  write("graph.cover", std::to_string(digest) + "\n" + malformed);
  throws([&] { (void)load_cover(graph, cache); }, "invalid cached shortcut endpoint");
  write("graph.cover", "broken");
  throws([&] { (void)load_cover(graph, cache); }, "cache checksum");
  for (const auto& preset :
       {"independent2", "independent3", "correlated2", "correlated3", "reciprocal", "ev"}) {
    const auto a = generate_graph(preset, 12, 42), b = generate_graph(preset, 12, 42);
    check(a.fingerprint() == b.fingerprint(), "seeded graph reproducibility");
    if (a.model() == Model::additive) compare_algorithms(a, 0, 11);
  }
}
}  // namespace
int main() {
  auto dir = std::filesystem::temp_directory_path() /
             ("pareto-tests-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directories(dir);
    for (const auto& [name, test] : std::vector<std::pair<std::string, std::function<void()>>>{
             {"hand-calculated paths", hand_calculated},
             {"randomized exhaustive oracle", randomized},
             {"triangle shortcut pruning", triangle_pruning},
             {"battery profiles and regenerative cycles", ev},
             {"invalid inputs and budgets", boundaries},
             {"imports, persistence, reproducibility", [&] { io(dir); }}}) {
      test();
      std::cout << "PASS " << name << '\n';
    }
    std::filesystem::remove_all(dir);
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\nfixtures: " << dir
              << '\n';
    return 1;
  }
}
