# Published evaluation data

The README's performance tables are transcribed from:

Temirlan Kurbanov, Marek Cuchý, and Jiří Vokřínek. **Fast One-to-Many Multicriteria Shortest Path Search.** *IEEE Transactions on Intelligent Transportation Systems*, 24(10), 10410–10419, 2023. [DOI: 10.1109/TITS.2023.3282069](https://doi.org/10.1109/TITS.2023.3282069).

These are results of the original implementation evaluated in that publication. They are not measurements or a reproduction of the refactored implementation in this repository. The publisher PDF and its figures are not redistributed here.

## One-to-many

[paper-one-to-many.csv](paper-one-to-many.csv) contains:

- Mean runtimes and standard deviations from **Table V**, p. 10418, in seconds. Each algorithm column in the README is mean ± standard deviation.
- The corresponding reported t-kPC-MLS/MLS speedups from **Table VI**, p. 10418. These ratios are transcribed directly, rather than recomputed from rounded Table V values.
- Graph sizes from **Table III**, p. 10416; target-set sizes and `k` from Sections V.A–V.B.

Bavaria has 294,727 vertices and 587,782 edges, with 1,000 randomly selected goals per experiment. Germany has 1,521,776 vertices and 2,947,009 edges, with 6,000 goals. The cover parameter is `k = 32`; only dominance pruning was used during cover construction. Query sources were randomly selected, and all four algorithm combinations were run from each selected source.

Dataset names encode the region (`B` = Bavaria; `G` = Germany), number of additive criteria (`2` or `3`), and correlation (`C` = correlated; `U` = uncorrelated). These experiments use travel time plus generated additive criteria on the road-network topology, not the four-component EV battery model. Table II describes the cost combinations: correlated secondary criteria are of the form `t + ct`; uncorrelated criteria are random values in `{0,...,50}`.

For B-3-U, MLS required over 24 hours and t-MLS exceeded memory limits on some queries. The reported B-3-U timings are restricted to instances completed by the three modified methods. Its MLS mean, standard deviation, and speedup relative to MLS are therefore empty in the CSV and shown as an em dash in the README. Table VI reports 3.14 for t-kPC-MLS relative to t-MLS on that row; it must not be presented as speedup relative to MLS. G-3-U is omitted from the paper's runtime table because it could not be solved consistently.

## One-to-one

[paper-one-to-one.csv](paper-one-to-one.csv) contains the IMA baseline and proposed method's speedups from **Table VIII**, p. 10419. Section V.E describes the proposed one-to-one method as **t-discarding kPC-IMA**; the table itself labels its final column `t-kPC-MLS`. We use the method name from the accompanying text, `t-kPC-IMA`, to distinguish it from the one-to-many algorithm.

The experiments use bicriteria DIMACS road graphs (distance and traversal time), `k = 16`, and 100 origin-destination pairs per graph for IMA and the proposed method. Ratios are relative to IMA. Cover construction time is excluded; per-query bound computation is included. Columns for literature methods LCDPF, BBDijkstra, and MDA are not reproduced here; those algorithms are not implemented in this repository. The paper's caveats about different hardware and MDA's different query samples concern that broader literature comparison.

## Experimental environment

Section V reports C++ compiled with GCC 8.5.0 and `-O3`, running on an AMD EPYC 7543 at 2.8 GHz with 32 GB RAM allocated. Cover preprocessing is evaluated separately in Section V.B. No additional random seeds, raw query samples, or query counts for the one-to-many experiments are supplied in these transcriptions.

## Existing synthetic smoke runs

`results.csv`, `environment.json`, `comparison.svg`, and `comparison.png` record the earlier 64-vertex synthetic runs. They exercise benchmark output, seeded fixtures, and agreement with MLS; they are **not a meaningful performance comparison for large multicriteria shortest-path instances** and are no longer presented as such in the README.

`tools/benchmark.py` reproduces those small smoke runs only. It does not reproduce the published evaluation. A new implementation-level performance evaluation would require road-network-scale datasets, appropriate criterion generation and target sets, and separate accounting for preprocessing and query costs.
