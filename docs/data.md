# Datasets and formats

All examples are synthetic and committed in `examples/`. External datasets stay outside Git. No downloader or dataset redistribution license is implied.

## Native CSV

Additive edges have a header beginning `from,to`, followed by two or more criterion names:

```csv
from,to,time,distance
0,1,2,8
1,2,3,4
```

Every edge must have the same number of finite, nonnegative costs. IDs are zero-based nonnegative integers. Isolated vertices can be declared through the optional node file. Graph size is inferred from the largest edge or node ID; IDs with gaps consume storage up to that maximum.

EV CSV requires the exact header `from,to,time,consumption`. Values are signed 64-bit integers, with positive time and possibly negative consumption. Use `--model ev --capacity INTEGER` (default capacity 40,000,000). The loader does not round fractional energy values.

Optional coordinates (`--nodes FILE`) use:

```csv
id,x,y
0,0,0
1,1,2
2,2,0
```

Coordinates should share a planar unit when ellipse pruning is used. `x/y` are not implicitly latitude/longitude. Nodes files can also use the original six-column header `elevation,id,latitude,latitudeProjected,longitude,longitudeProjected`; the projected columns are used.

CSV is deliberately simple: comma separators, optional whitespace, blank lines, and `#` comment lines; quoted fields are not supported. Invalid values and inconsistent row dimensions fail instead of silently defaulting. Structural edge-row errors include a file and row number; deferred numeric-cost and model validation reports the value or violated constraint.

## Original project formats

| `--format` | Contents |
| --- | --- |
| `legacy-edges` | Header then `consumption,fromId,time,toId` rows; accepts the original `comsumption` header typo |
| `legacy-random` | Headerless `from,to,time,cost1,cost2`; additive model only |
| `legacy-full` | Headerless six-column node rows, a blank separator, then headerless consumption/from/time/to edges |

Legacy edge formats map to `[time, consumption]` in additive mode, or the EV profile in EV mode. **Real energy datasets contain negative consumption**, so use EV mode for those files. The additive reader rejects negative weights; it does not silently take absolute values. Legacy-random imports the weights actually in the file; it no longer discards them and generates new random numbers. Use synthetic presets when generated criteria are desired.

For data stored beside this project:

```sh
./build/pareto-paths query --input ../data/bayern_edges.csv \
  --format legacy-edges --model ev --nodes ../data/bayern_nodes.csv \
  --source 0 --target 1 --max-labels 100000
```

Large instances may exceed budgets. The example illustrates import arguments, not a guaranteed fast country-scale query. Avoid enabling EV cover preprocessing on large cyclic networks without checking the documented compression limitations.

## DIMACS graphs

Use paired distance/time graph files from the 9th DIMACS shortest-path challenge:

```sh
./build/pareto-paths query --input USA-road-t.NY.gr \
  --second-weights USA-road-d.NY.gr --format dimacs \
  --source 0 --target 100 --algorithm ima
```

Each file must contain `p sp NODE_COUNT ARC_COUNT`, `a FROM TO WEIGHT`, and optional `c` comments. DIMACS IDs are one-based and converted to zero-based internal IDs. The two files must have identical dimensions and arc ordering. The primary file supplies the first cost dimension.

Files named `USA-road-f.*.gr` in the original collection contain **origin-destination pairs**, not graph arcs. They cannot be used as graph input. Incomplete `.cyberducksegment` files are also not datasets. No claim is made that the collection includes complete paired DIMACS graphs.

## Seeded synthetic presets

`--preset NAME --nodes-count N --seed S` creates a forward directed graph with edges advancing 1, 2, or 5 vertices where possible. Presets are `independent2`, `independent3`, `correlated2`, `correlated3`, `reciprocal`, and `ev`. Costs use a seeded `mt19937` with explicit integer mapping, avoiding implementation-specific distribution sampling. EV examples use capacity 100 and both positive consumption and regeneration. Reciprocal criteria use time and `1/time`; generated times are always positive.

These small DAGs are teaching and smoke-test fixtures, not substitutes for road-network evaluation. In particular, a reciprocal-cost experiment on a forward DAG does not characterize the potentially much larger frontiers of cyclic road networks.
