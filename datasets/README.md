# Road-network datasets

This directory bundles the supplied Bavaria, Germany, and six DIMACS instances as gzip archives. Extracted text files go into the ignored `data/` working directory. No Git LFS or third-party Python package is required.

```sh
# Extract only the graphs needed for a run.
python3 tools/datasets.py extract bayern NY
# Or extract the complete collection.
python3 tools/datasets.py extract bayern germany NY BAY COL FLA NE NW
```

The extraction command verifies compressed and uncompressed SHA-256 hashes against [manifest.json](manifest.json). Existing matching files are reused; modified extracted files are not overwritten. The original files in `../data` are not modified.

| Dataset | Vertices | Directed edges | Criteria | Compressed files |
| :--- | ---: | ---: | :--- | ---: |
| bayern | 294,727 | 587,781 | time, consumption | 6.8 MiB |
| germany | 1,521,776 | 2,947,009 | time, consumption | 35.8 MiB |
| NY | 264,346 | 733,846 | distance, time | 7.1 MiB |
| BAY | 321,270 | 800,172 | distance, time | 8.0 MiB |
| COL | 435,666 | 1,057,066 | distance, time | 11.1 MiB |
| FLA | 1,070,376 | 2,712,798 | distance, time | 27.9 MiB |
| NE | 1,524,453 | 3,897,636 | distance, time | 42.0 MiB |
| NW | 1,207,945 | 2,840,208 | distance, time | 30.8 MiB |

Total compressed size: **169.5 MiB**. The Bavaria and Germany totals include their node-coordinate files. Each DIMACS archive contains both weight blocks; its edge count is the number of arcs in each block, not their sum.

## Bavaria and Germany

These are **OpenStreetMap-derived research graphs with residential roads removed**. They retain the supplied research topology and weights; they are not current, unmodified OSM downloads. See [OpenStreetMap attribution and ODbL terms](NOTICE-OSM.md).

The canonical edge files use this order:

```csv
from,to,time,consumption
0,1,3,7680
```

IDs are zero-based. The edge schema is `from,to,time,consumption`; every arc and criterion value is retained. Node files use `id,x,y`, containing only the IDs and projected coordinates used by the library.

The supplied Bavaria edge file contains **587,781 arcs**, whereas the paper reports 587,782. This package preserves the supplied file and does not insert an edge to match the published count.

```sh
./build/pareto-paths query --input data/osm/bayern_edges.csv \
  --nodes data/osm/bayern_nodes.csv --model ev --source 0 --target 1
```

Time and energy values keep their supplied units; no conversion is performed. Consumption may be negative due to regeneration, so these energy graphs require `--model ev` rather than the nonnegative additive model. Battery capacity must use the same unit as consumption.

## DIMACS instances

The U.S. graphs are from the **9th DIMACS Implementation Challenge**, based on TIGER/Line road networks. See [DIMACS provenance and terms](NOTICE-DIMACS.md).

Each extracted `data/dimacs/REGION.gr` contains two complete DIMACS `p sp` graph blocks in this order: **distance, then traversal time**. The importer matches each arc's endpoints across the blocks and produces two additive criteria. The supplied 100-query test prefixes have been removed, and no query pairs are bundled. DIMACS IDs remain one-based in the files and are converted to zero-based IDs when loaded.

```sh
./build/pareto-paths query --input data/dimacs/NY.gr --format dimacs \
  --source 0 --target 1 --algorithm t-kpc-ima
```

The reader also supports the standard separate distance/time files using `--second-weights`. A combined file does not need that option. The C++ executable reads uncompressed text, so run the extraction command first.

## Rebuild from the original files

```sh
python3 tools/datasets.py prepare --source ../data --bundle datasets
```

The preparation step validates node IDs, graph sizes, arc counts, and DIMACS pairing; normalizes OSM edge and coordinate columns; removes only the leading personal test-query pairs from DIMACS sources; and writes deterministic gzip archives with a manifest of original and distributed hashes. `--only bayern NY` can prepare a subset in a separate bundle directory. No files are downloaded. Duplicate combined Bavaria input, experimental random-criterion files, and unrelated coordinate files are not bundled.
