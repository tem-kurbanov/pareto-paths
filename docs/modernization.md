# From five experiments to one project

The original directories are preserved beside this new repository. They are not copied into the build or committed as five redundant implementations. This map records the capabilities carried forward.

| Original | Consolidated capability |
| --- | --- |
| `ev` | Integer battery profiles; composition and feasibility; MLS/t-MLS/kPC/t-kPC; epsilon, ellipse, and capping heuristics; route export |
| `random_criteria_search` | Generic additive criteria; independent/correlated synthetic presets; four main algorithms; cover persistence; benchmark CSV |
| `t-1t` | Time/reciprocal-time preset and the four main algorithms |
| `mcsps_two` | Bicriteria t-set specialization; lower-bound preprocessing; IMA/blSET and two cover-based point-to-point strategies; DIMACS paired input |
| `mcsps_three` | Dimension-independent additive labels and t-sets, tested for 2/3/4 criteria |

## Changes made deliberately

- Hardcoded Bavaria/Germany sizes become inferred graph sizes. Constructors no longer read process-wide stdin, choose random charging stations, print benchmark progress, or terminate the process.
- A shared graph and cost-model interface replaces copied `Graph` classes. Algorithms return owned route/result objects, not dangling parent pointers or timing-only side effects. Statistics do not reset when read.
- The result arena keeps labels alive until path reconstruction finishes. Original edge identity survives parallel edges and shortcuts. Cover objects expose read-only state.
- Exact battery comparisons use `int64_t`, including costs beyond floating-point integer precision. Additions/subtractions throw on overflow. EV edges require strictly positive time.
- Additive weights use `double`, retaining reciprocal costs without the legacy two/three-criterion integer special cases. Negative additive weights are rejected rather than assuming a label-setting property that they violate.
- Cover selection is a deterministic greedy path-hitting test. Nondominated shortcuts are rebuilt into a fresh object each time; stale preprocessing data cannot accumulate. Triangle pruning is retained as an optional sequential pass with a dedicated zero-cycle regression test.
- Regenerative-cycle vertices are retained for EV covers. This changes legacy simple-path behavior intentionally to preserve useful battery-state transitions. It often prevents compression on real road graphs; this limitation is explicit.
- Full and truncated weak-dominance checks use strict comparator ordering and equal-cost deduplication. The legacy concatenated-number hash is removed, eliminating ambiguous keys such as `(1,23)` and `(12,3)`.
- Point-to-point variants share queue/storage code. The two cover variants are exposed as different lower-bound domains, not the old hash-vs-vector container implementations. This is a documented strategy consolidation, not a claim of identical internal execution.
- Legacy-random imports file values instead of overwriting them with fresh randomness. Seeded synthetic generation is a separate operation. Randomly selected dock sets become explicit retained goal lists.
- Ellipse pruning takes a direct detour ratio rather than the old coefficient/minimum-distance formula. Experimental heuristic settings are validated and their outputs labeled approximate.
- Ad-hoc experiment mains, unreachable benchmark blocks, user-specific paths, cluster scripts, raw log dumps, and historical result files are replaced by one CLI and reproducible presets. Historical paper measurements are cited, not relabeled as new benchmarks.

## Reading path

1. `include/pareto/pareto.hpp` — graph, costs, covers, search options and owned results.
2. `src/graph.cpp` — additive and battery algebra, dominance and fingerprints.
3. `src/cover.cpp` and `src/search.cpp` — path hitting, shortcuts, label lifecycle, t-sets and bounds.
4. `tests/test_main.cpp` — exhaustive additive oracle, independent EV charge simulation, persistence and errors.

The CSV loader and CLI are separate from algorithms. No global `using namespace std`, manual `new/delete`, implicit global input, or external C++ package dependency is required.

## Verification approach

An independent exhaustive-path oracle filters complete candidate sets on small additive graphs. Seeded cyclic fixtures exercise two, three, and four objectives across every exact algorithm and several cover sizes. Hand-calculated route costs, disconnected goals, zero-cost cycles, multiple targets, and source-equals-target cases establish observable behavior. Fractional fixtures use an explicit comparison tolerance without relaxing Pareto-set size assertions.

EV results are independently simulated for every integer starting charge on small-capacity fixtures. Tests include useful regenerative self-loops and two parallel costs straddling `2^53`, in both insertion orders. Import, cover round-trip/corruption, input validation, CLI JSON/CSV contracts, and explicit resource failures receive separate checks. Debug, Release, and sanitizer builds complement the independent code review. See `verification.md` for actual local evidence and remaining limits.
