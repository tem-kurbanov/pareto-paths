#!/usr/bin/env python3
"""Reproduce the README experiment. Requires Python 3.9+ and matplotlib 3.9+."""
import argparse
import csv
import datetime
import io
import json
from pathlib import Path
import platform
import statistics
import subprocess

PRESETS = ["correlated2", "independent2", "correlated3", "independent3", "reciprocal", "ev"]
ALGORITHMS = ["mls", "t-mls", "kpc-mls", "t-kpc-mls"]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/pareto-paths"))
    parser.add_argument("--output", type=Path, default=Path("benchmarks"))
    parser.add_argument("--nodes", type=int, default=64)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows, commands = [], []
    for preset in PRESETS:
        command = [str(args.binary.resolve()), "benchmark", "--preset", preset,
                   "--nodes-count", str(args.nodes), "--seed", "42", "--k", "8",
                   "--queries", "4", "--repeats", "5"]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        rows.extend(csv.DictReader(io.StringIO(result.stdout)))
        commands.append(command[1:])
        print(f"Measured {preset}")
    with (args.output / "results.csv").open("w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    metadata = {
        "measured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "system": platform.platform(), "machine": platform.machine(),
        "processor": platform.processor(), "python": platform.python_version(),
        "binary": subprocess.check_output([str(args.binary.resolve()), "--version"], text=True).strip(),
        "commands": commands,
        "notes": "Synthetic forward DAGs; sources 0..3, last node as target; preprocessing reused. "
                 "One untimed MLS reference per source; five timed runs per algorithm in fixed order. "
                 "Small-graph timings are illustrative, not evidence of large-network speedups.",
    }
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    plot(rows, args.output / "comparison.svg")


def plot(rows, destination):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    node_counts = {int(row["nodes"]) for row in rows}
    if len(node_counts) != 1:
        raise ValueError("Figure requires one graph size across presets")
    node_count = node_counts.pop()
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "svg.hashsalt": "pareto-paths", "axes.spines.top": False,
                         "axes.spines.right": False, "axes.spines.left": False})
    fig, axes = plt.subplots(1, 2, figsize=(12, 7), sharey=True)
    fig.patch.set_facecolor("#faf9f6")
    colors = ["#24334d", "#7390af", "#e8b46a", "#168578"]
    for ax, metric, title in zip(axes, ["expanded", "query_seconds"],
                                 ["Labels expanded", "Query time"]):
        ax.set_facecolor("#faf9f6")
        y = np.arange(len(PRESETS))
        for i, algorithm in enumerate(ALGORITHMS):
            ratios, low, high = [], [], []
            for preset in PRESETS:
                samples = []
                for source in ["0", "1", "2", "3"]:
                    selected = [r for r in rows if r["preset"] == preset and r["source"] == source]
                    base = statistics.median(float(r[metric]) for r in selected if r["algorithm"] == "mls")
                    actual = statistics.median(float(r[metric]) for r in selected if r["algorithm"] == algorithm)
                    samples.append(actual / base)
                middle = statistics.median(samples)
                ratios.append(middle)
                low.append(middle - min(samples))
                high.append(max(samples) - middle)
            ax.barh(y + (i - 1.5) * .18, ratios, height=.15, color=colors[i], label=algorithm,
                    xerr=[low, high], error_kw={"linewidth": .6, "ecolor": "#596474", "capsize": 1.5})
        ax.axvline(1, color="#999a99", lw=.8, ls="--", zorder=0)
        ax.set_title(title, loc="left", fontsize=15, fontweight="bold", pad=16, color="#24334d")
        ax.set_xlabel("Relative to MLS  ·  lower is better", labelpad=12)
        ax.set_yticks(y, ["Correlated · 2 criteria", "Independent · 2 criteria", "Correlated · 3 criteria",
                          "Independent · 3 criteria", "Time + reciprocal time", "EV battery profiles"])
        ax.tick_params(axis="both", length=0, pad=8)
        ax.grid(axis="x", color="#dfdfd9", linewidth=.5)
        ax.set_axisbelow(True)
        ax.set_xlim(left=0)
    axes[0].invert_yaxis()
    fig.suptitle("Compression changes the work. Runtime depends on the instance.",
                 x=.04, y=.98, ha="left", fontsize=18, fontweight="bold", color="#24334d")
    fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=4, frameon=False,
               bbox_to_anchor=(.58, .07))
    fig.text(.04, .035, f"{node_count} vertices · seed 42 · 4 sources · 5 repeats · k = 8\n"
             "Bars: median of per-source ratios; whiskers: source range. Preprocessing excluded; see raw CSV.",
             fontsize=9, color="#596474")
    fig.subplots_adjust(left=.23, right=.98, top=.85, bottom=.23, wspace=.2)
    fig.savefig(destination, metadata={"Date": None})
    destination.write_text("\n".join(line.rstrip() for line in destination.read_text().splitlines()) + "\n")
    fig.savefig(destination.with_suffix(".png"), dpi=160, metadata={"Software": "matplotlib"})
    plt.close(fig)

if __name__ == "__main__":
    main()
