# Datasets and formats

The complete supplied Bavaria, Germany, and six U.S. DIMACS road graphs are bundled as compressed text under [datasets/](../datasets/README.md). Small synthetic examples remain in `examples/`. Dataset licensing is separate from the MIT software license.

## Bundled road networks

Extract selected graphs before running the C++ executable:

```sh
python3 tools/datasets.py extract bayern germany NY BAY COL FLA NE NW
```

This writes verified, uncompressed copies into `data/osm/` and `data/dimacs/`. The `data/` directory is ignored by Git; the compressed archives, source/derived hashes, counts, and notices are in `datasets/`. Extraction does not overwrite modified existing files. See the [dataset catalog](../datasets/README.md) for graph sizes and `tools/datasets.py --help` for preparation/extraction commands.

### Bavaria and Germany: OpenStreetMap derivatives

The supplied research graphs originate from **OpenStreetMap and were altered to remove residential roads**. They are not unmodified OSM extracts. The bundled copies preserve the supplied edges, vertices, coordinates, and weights, with edge columns reordered to:

```csv
from,to,time,consumption
0,1,3,7680
```

Both identifiers are zero-based, followed by the time and consumption criteria. `--format csv` is the default. Coordinate files use `id,x,y`, with the node ID followed by its two projected coordinates.

```sh
python3 tools/datasets.py extract bayern
./build/pareto-paths query --input data/osm/bayern_edges.csv \
  --nodes data/osm/bayern_nodes.csv --model ev \
  --source 0 --target 1 --max-labels 100000
```

The same command applies to Germany after substituting `germany_edges.csv` and `germany_nodes.csv`. The supplied time and consumption units are preserved. Consumption can be negative, so select `--model ev`; the capacity must use the consumption unit. The additive model rejects negative costs.

**© OpenStreetMap contributors.** These derived databases are distributed under [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/), with [OpenStreetMap attribution](https://www.openstreetmap.org/copyright). See [the bundled notice](../datasets/NOTICE-OSM.md). The package does not rerun the original residential-road filtering or download a current OSM snapshot. The supplied Bavaria file has 587,781 edges; the publication's count of 587,782 is retained only in the attributed paper results.

### DIMACS: 9th Implementation Challenge

NY, BAY, COL, FLA, NE, and NW come from the **9th DIMACS Implementation Challenge: Shortest Paths**, using U.S. Census TIGER/Line road networks. The [challenge download page](https://www.diag.uniroma1.it/~challenge9/download.shtml) identifies its collection as public domain unless otherwise stated. Original graph comments are retained; see [the bundled notice](../datasets/NOTICE-DIMACS.md).

The bundled files contain a complete distance graph followed by a complete traversal-time graph. Each block has `p sp NODE_COUNT ARC_COUNT`, `a FROM TO WEIGHT`, and optional `c` comments. There are no origin-destination query pairs: the 100 leading personal test pairs in five supplied files were removed; NW had no prefix.

```sh
python3 tools/datasets.py extract NY
./build/pareto-paths query --input data/dimacs/NY.gr --format dimacs \
  --source 0 --target 1 --algorithm t-kpc-ima
```

A combined file is loaded without `--second-weights`. The two criterion dimensions follow block order: distance, then traversal time. Node IDs are one-based in the file and converted to zero-based IDs internally. Graph sizes and arc ordering must match between blocks; the declared node count preserves isolated vertices.

Separate single-criterion DIMACS files remain supported:

```sh
./build/pareto-paths query --input USA-road-t.NY.gr \
  --second-weights USA-road-d.NY.gr --format dimacs \
  --source 0 --target 100 --algorithm ima
```

Here the primary file supplies the first criterion, so that example uses time then distance. `--second-weights` cannot be combined with an input that already contains two graph blocks. More than two blocks, mismatched endpoints/counts, and test-query prefixes are rejected. Use the cleaned bundled instances rather than the original prefixed test containers.

## Native CSV

Additive edge files begin with `from,to`, followed by two or more criterion names:

```csv
from,to,time,distance
0,1,2,8
1,2,3,4
```

Column names must be nonempty and unique. Every edge must have the same number of finite, nonnegative costs. IDs are zero-based nonnegative integers. Isolated vertices can be declared through the optional node file. Graph size is inferred from the largest edge or node ID; IDs with gaps consume storage up to that maximum.

EV CSV requires the exact header `from,to,time,consumption`. Values are signed 64-bit integers, with positive time and possibly negative consumption. Use `--model ev --capacity INTEGER` (default capacity 40,000,000). The loader does not round fractional energy values.

Optional coordinates (`--nodes FILE`) use:

```csv
id,x,y
0,0,0
1,1,2
2,2,0
```

Coordinates must be finite planar values in a common unit when ellipse pruning is used. `x/y` are not implicitly latitude/longitude. The bundled OSM node files contain projected coordinates. Supplying the node file preserves isolated vertices absent from the CSV edge list. Headers and rows must use exactly the three columns `id,x,y`; IDs must be unique.

CSV is deliberately simple: comma separators, optional whitespace, blank lines, and `#` comment lines; quoted fields are not supported. Invalid values and inconsistent row dimensions fail instead of silently defaulting. Structural edge-row errors include a file and row number; deferred numeric-cost and model validation reports the value or violated constraint.

Large instances may exceed search/preprocessing budgets. Dataset availability and successful import do not establish query speed or reproduction of the paper's performance. EV cover preprocessing may provide little compression on cyclic road networks; see [algorithm limits](algorithms.md).

## Seeded synthetic presets

`--preset NAME --nodes-count N --seed S` creates a forward directed graph with edges advancing 1, 2, or 5 vertices where possible. Presets are `independent2`, `independent3`, `correlated2`, `correlated3`, `reciprocal`, and `ev`. Costs use a seeded `mt19937` with explicit integer mapping, avoiding implementation-specific distribution sampling. EV examples use capacity 100 and both positive consumption and regeneration. Reciprocal criteria use time and `1/time`; generated times are always positive.

These small DAGs are teaching and smoke-test fixtures, not substitutes for road-network evaluation. In particular, a reciprocal-cost experiment on a forward DAG does not characterize the potentially much larger frontiers of cyclic road networks.
