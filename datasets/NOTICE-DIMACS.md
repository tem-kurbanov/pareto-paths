# DIMACS road-network datasets

The NY, BAY, COL, FLA, NE, and NW instances are from the **9th DIMACS Implementation Challenge: Shortest Paths**, specifically its U.S. road-network collection derived from U.S. Census TIGER/Line data.

- [Challenge download page and terms](https://www.diag.uniroma1.it/~challenge9/download.shtml)
- [TIGER/Line collection provenance](https://www.diag.uniroma1.it/~challenge9/data/tiger/)

The challenge download page states that its data/software is in the public domain unless otherwise stated. Original graph comments are retained. These data are not relicensed under the software's MIT license.

The supplied local files contain a distance graph followed by a traversal-time graph, with matching arc order. The NY, BAY, COL, FLA, and NE source files also contained 100 personal test origin-destination pairs before the graphs. Those pairs have been removed from the distributed archives. NW had no such prefix. No query-pair files are included, and the graph blocks themselves retain their original IDs and weights.
