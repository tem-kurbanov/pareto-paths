# Algorithms and contracts

## One graph, two cost algebras

`Graph` owns directed edges, adjacency lists, and optional planar coordinates. Vertices are zero-based; parallel edges have distinct stable IDs. Additive graphs use a vector of two or more nonnegative finite `double` weights. A path adds its component weights and minimizes every component.

EV graphs instead use four `int64_t` profile components:

```text
[time, minimum_initial_charge, net_consumption, maximum_final_charge]
```

For any starting charge `s >= minimum_initial_charge`, final charge is `min(s - net_consumption, maximum_final_charge)`. Lower is better for the first three components; higher is better for the fourth. Composing prefix `a` and suffix `b` gives:

```text
time                 = a.time + b.time
minimum_initial      = max(a.minimum_initial, a.consumption + b.minimum_initial)
consumption          = max(a.consumption + b.consumption, a.minimum_initial - b.maximum_final)
maximum_final        = min(a.maximum_final - b.consumption, b.maximum_final)
```

This represents regeneration and capacity saturation without assuming independent additive battery objectives. A route is discarded if required initial charge exceeds capacity or maximum final charge is negative. Capacity and consumption share an integer unit chosen by the caller. EV traversal time must be strictly positive. Overflow throws; there is no silent wraparound. Exact EV comparisons stay in integer arithmetic, including values beyond `2^53`.

The EV model describes travel feasibility at a fixed capacity, not charging-station scheduling or a complete multi-stop charging planner.

## Search strategies

| CLI name | Strategy | Supported models/goals |
| --- | --- | --- |
| `mls` | Lexicographically ordered multicriteria labels; full dominance checks | Additive or EV; one/many/all goals |
| `t-mls` | MLS with a nondominated truncated set for closed labels | Additive or EV; one/many/all goals |
| `kpc-mls` | Full dominance on the compressed graph and source connectors | Additive or EV; explicit retained goals |
| `t-kpc-mls` | Cover search with truncated closed-label dominance | Additive or EV; explicit retained goals |
| `ima` | MLS with componentwise reverse-Dijkstra lower bounds and incumbent-goal pruning | Additive; one goal |
| `blset` | Goal-frontier pruning using nonnegative partial costs, without lower-bound preprocessing | Additive; one goal |
| `kpc-ima` | t-cover search; lower bounds on the cover augmented with non-cover connectors | Additive; one goal |
| `kpc-ima-straight` | t-cover search; lower bounds on the original graph | Additive; one goal |

A t-set drops the first component of nondominated closed-label vectors. A comparison can use this smaller set only when the candidate's first component is no better than all relevant closed first components. Otherwise the implementation uses full dominance. For two additive criteria, a scalar minimum suffices for the truncated set. Open labels always receive full dominance checks. Both bound variants use the same owned label storage; the old concatenated-vector hash representation is removed.

`SearchResult` owns Pareto routes and immutable-by-convention statistics. Routes retain original edge IDs, including parallel edges and every edge represented by a shortcut. Empty target lists mean all vertices for non-cover methods. Requested but unreachable goals are present with empty route lists. Equal cost vectors yield one representative route, not every equal-cost path. Queries do not mutate the graph or cover.

## Cover construction

Given `k`, every simple path of **k vertices** must meet the retained set. Goal vertices are always retained. The builder greedily considers removing vertices, rejecting a removal if it would create an uncovered simple k-vertex path. This is a valid cover, not a minimum-cover solver and not a claim to reproduce the original heuristic's exact vertex choices.

From each retained vertex, the builder enumerates paths through non-cover vertices until reaching another retained vertex. It keeps nondominated parallel shortcuts with their original edge sequences. A query beginning outside the cover follows original edges until reaching it. A subset of retained goals can be queried without rebuilding. Asking for an eliminated goal fails explicitly.

Additive cycles cannot improve nonnegative path costs and can be removed. **EV cycles can improve charge:** all vertices in cyclic strongly connected components are retained for EV preprocessing. Eliminated EV regions are therefore acyclic. This protects useful regenerative loops, at the cost of little or no compression on strongly connected road networks. The supplied synthetic EV benchmark is acyclic and does not demonstrate road-network compression.

Optional `--triangle true` pruning removes a shortcut when a two-shortcut route through another retained vertex dominates it. Removals are sequential and use only still-present shortcuts, preserving equal-cost alternatives on zero-cost cycles. It is disabled by default, including in the bundled benchmark. Dominance and triangle pruning are exact within the numeric cost model. Cover construction and path enumeration can be exponential; `max_states` is an expansion budget, not a promise that every large graph can be preprocessed quickly.

Cover objects expose read-only accessors. A cache includes a version, graph/cost/coordinate fingerprint, `k`, the triangle-pruning setting, retained vertices, goals, shortcut edge sequences, and a checksum. Loading reconstructs shortcut costs and validates connectivity, endpoints, counts, and retained boundaries. FNV-1a detects accidental incompatibility/corruption; it is not cryptographic authentication. Caches are local trusted artifacts: do not edit their contents or recompute checksums to bypass validation. Cache loading time is not recorded as construction time; a loaded cover reports zero construction seconds.

## Optional heuristics

Exact search is the default. These switches enable experimental approximate pruning and set `approximate: true` in JSON:

- **Epsilon dominance:** a per-component factor in `(0,1]`, matching the legacy convention. Smaller factors relax dominance; `1` is exact. Exact dominance is always also considered, including negative EV consumption. This is not a claimed global epsilon-error bound.
- **Capping:** zero disables a component; a ratio `>=1` discards candidates too far from the best closed component at that vertex. Component minima/maxima can come from different routes, so useful tradeoffs can be lost.
- **Ellipse:** one target, complete planar coordinates, and ratio `>=1`. A vertex is retained by the heuristic when its source-distance plus goal-distance is at most the ratio times direct source-goal distance. Shortcut interior vertices are checked too. This ratio convention replaces the legacy coefficient-plus-minimum-distance convention.

Heuristics may omit nondominated solutions or every route. Returned paths remain physically feasible; no exact-frontier or approximation-quality guarantee is made.

## Numerics and resource limits

Floating-point addition is not associative. Shortcut grouping can change costs near ties **and can change Pareto-set cardinality**, especially with widely different magnitudes. “Exact” means heuristic pruning is disabled, not exact real arithmetic or guaranteed equivalence between every floating-point evaluation order. Use moderately sized integer-valued additive weights for strict reproducibility; tests use exact equality for such fixtures and relative tolerance for reciprocal-time costs. EV profiles use checked integer arithmetic throughout exact comparisons.

Search has a generated-label budget (default one million) and preprocessing a visited-state budget (default two million). Exceeding either throws and returns no partial `SearchResult`. These budgets bound major work counters, not total allocated bytes. Labels are owned by a per-query arena with parent indices; retired labels remain available for route reconstruction until the query ends. This prioritizes safe lifetimes over the legacy memory estimates. No production-scale performance claim is made for this implementation.
