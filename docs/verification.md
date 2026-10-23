# Verification record

Local assessment: **CONFORMS to the agreed local preparation scope.** At the time of that assessment, publication and software licensing were deferred. The author subsequently selected MIT and authorized public GitHub publication; see `release.md` for the current release procedure. This is evidence of tested behavior, not a claim that every graph or platform is proven correct.

Algorithm/CLI source revision: `394cb75` (the optional triangle-pruning implementation). This report and refreshed benchmark artifacts are committed afterward without changing algorithm code.

## Requirement matrix

| Requirement | Evidence executed | Status |
| --- | --- | --- |
| One modern project, distinct original capabilities retained | Shared C++20 library/CLI; eight algorithm modes; additive/EV models; three optional heuristics; optional triangle pruning; migration map reviewed against originals | PASS |
| Correct additive Pareto costs and original-edge paths | Exhaustive independent simple-path oracle on seeded directed graphs with 2/3/4 criteria, three cover sizes, triangle off/on; parallel edges, zero cycles, unreachable/multiple goals and source-equals-target | PASS |
| Correct battery profiles and exact integer comparisons | Independent stepwise charge simulation, saturation, regeneration, useful loops, infeasible routes, overflow, and adversarial parallel costs beyond `2^53` | PASS |
| Valid imports, owned results and reusable covers | Native/legacy/paired-DIMACS fixtures; schema-2 round trips; corrupt/incorrect graph/cache endpoint rejection; public cover mutation prevented by const accessors | PASS |
| Runnable examples and CLI contracts | 40 real executable invocations for JSON, CSV, persistence, presets and error handling; external C++ consumer compiled and linked from a clean archive | PASS |
| Reproducible, honest portfolio evidence | 480 fresh Release measurements across six presets; baseline checks in every benchmark; charts regenerated with matplotlib; raw CSV/environment/commands committed | PASS |
| Independent review | Read-only reviewer rechecked fixes and final triangle option handling; no remaining confirmed findings. Additional 1,200 cyclic EV query comparisons matched MLS | PASS |
| Original work preserved and clean Git delivery | All 15 original C++/header SHA-1 hashes match initial inspection; clean committed-source archive builds and runs independently; no remote configured | PASS |

## Executed checks

All four local build configurations completed both CTest suites successfully:

| Configuration | Compiler | Result |
| --- | --- | --- |
| Debug, warnings as errors | Apple Clang 21 | 2/2 passed |
| Release, warnings as errors | Apple Clang 21 | 2/2 passed |
| Debug, AddressSanitizer + UndefinedBehaviorSanitizer | Apple Clang 21 | 2/2 passed; no reported sanitizer errors |
| Release, warnings as errors | GCC 16.2 | 2/2 passed |

The C++ executable reports **34,616 checks passed** across six named groups. Python integration tests report **40 CLI invocations**. These are assertion/invocation counts, not thousands of distinct independent test scenarios.

Commands used for each configured build directory:

```sh
cmake --build BUILD_DIRECTORY --parallel 4
ctest --test-dir BUILD_DIRECTORY --output-on-failure
```

The final committed tree was exported with `git archive`, configured and built from scratch in a temporary directory. Its CTest suites, demo, multi-target triangle-pruned cover save/load, JSON query, EV example, and all point-to-point benchmark variants succeeded. A separate C++ program included the public header, linked the static library, and returned the hand-calculated `[5,12]` route cost.

Formatting passed `clang-format --dry-run --Werror` over public headers, C++ sources and tests. Python scripts compile. Local documentation link targets exist, and `git diff --check` passes.

### Negative control

A disposable source copy deliberately reversed additive dominance. The independent oracle rejected it with a frontier-cardinality mismatch (`kpc-mls`, `k=3`, exit 1). The original working tree was not modified by that experiment. This demonstrates meaningful detection of an algorithmic regression; it is not a full mutation-testing campaign.

### External data smoke checks

The complete supplied Bavaria three-criterion file loaded successfully. IMA queried vertex 0 to 1 and returned original edge 0 with cost `[3, 2.2442, 1.5673]` (subject to ordinary double formatting). The Bavaria energy file also loaded under the EV model; a deliberately tiny one-label budget produced the expected explicit failure rather than a partial result.

These checks do not establish country-scale search performance, complete dataset correctness, or reproduction of the paper's experiments. External datasets are not committed.

### Visual and benchmark evidence

The original SVG hero and matplotlib chart were rendered and visually inspected. An independent reviewer checked that chart ratios/whiskers matched all 480 measurement rows, and found no clipping or incorrect route labels. The chart was subsequently refreshed from the final executable and inspected again. A separate caption regression verified that a configurable 128-vertex input yields a 128-vertex label, rather than the old fixed 64 label.

The README was converted to local HTML and its local links checked. A live browser/GitHub rendering check was unavailable in this session; the static renderer does not validate GitHub's Mermaid rendering. No remote Actions run is claimed.

## Limits and deferred work

- Linux CI is configured, but was not executed locally or on GitHub. Windows/MSVC was not tested.
- Additive floating-point regrouping can affect near-tie costs and frontier cardinality. Integer battery comparisons remain exact until a checked overflow exception.
- EV cycles are retained during preprocessing; strongly connected road graphs may receive little or no compression.
- Search and cover enumeration remain potentially exponential, with explicit budgets. The benchmarks use small forward graphs and exclude preprocessing from query-time ratios.
- Software licensing has since been set to MIT at the author's direction. Publication is authorized; local test results above do not imply that remote CI has passed.
