"""End-to-end contracts: actual executable, formats, persistence and errors."""
import csv
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe, root = sys.argv[1], Path(sys.argv[2])
checks = 0

def run(*args, fail=False):
    global checks
    result = subprocess.run([exe, *map(str, args)], text=True, capture_output=True)
    checks += 1
    if fail:
        assert result.returncode != 0 and result.stderr.startswith("pareto-paths:"), result
    else:
        assert result.returncode == 0, result.stderr
    return result.stdout

result = json.loads(run("demo", "--output", "json"))
assert [r["cost"] for r in result["targets"][0]["routes"]] == [[4, 16], [8, 6], [12, 2]]
assert not result["approximate"]
assert result["statistics"]["expanded"] > 0
for algorithm in ["mls", "t-mls", "kpc-mls", "t-kpc-mls", "ima", "blset", "t-kpc-ima"]:
    actual = json.loads(run("query", "--input", root / "examples/routes.csv", "--algorithm", algorithm, "--output", "json"))
    assert actual["algorithm"] == algorithm
    assert [r["cost"] for r in actual["targets"][0]["routes"]] == [[4, 16], [8, 6], [12, 2]]
ev = json.loads(run("query", "--input", root / "examples/ev.csv", "--model", "ev", "--capacity", "10", "--output", "json"))
assert [r["cost"] for r in ev["targets"][0]["routes"]] == [[8, 3, 1, 9], [10, 0, 0, 10]]
for preset in ["independent2", "independent3", "correlated2", "correlated3", "reciprocal", "ev"]:
    rows = list(csv.DictReader(io.StringIO(run("benchmark", "--preset", preset, "--nodes-count", "12", "--k", "4", "--queries", "2", "--repeats", "1"))))
    assert len(rows) == 8
    assert all(int(row["expanded"]) > 0 and float(row["query_seconds"]) > 0 for row in rows)
rows = list(csv.DictReader(io.StringIO(run("benchmark", "--preset", "independent2", "--nodes-count", "12", "--algorithms", "t-kpc-ima", "--k", "4", "--queries", "2", "--repeats", "1"))))
assert len(rows) == 2 and all(row["algorithm"] == "t-kpc-ima" for row in rows)
for removed in ["legacy-edges", "legacy-random", "legacy-full"]:
    run("query", "--input", root / "examples/routes.csv", "--format", removed, fail=True)
for retired in ["kpc-ima", "kpc-ima-straight"]:
    run("demo", "--algorithm", retired, fail=True)
    run("benchmark", "--algorithms", retired, fail=True)
run("query", "--input", root / "examples/routes.csv", "--target", "1,5", "--algorithm", "t-kpc-ima", fail=True)
run("query", "--input", root / "examples/ev.csv", "--model", "ev", "--capacity", "10", "--algorithm", "t-kpc-ima", fail=True)
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    combined = folder / "combined.gr"
    combined.write_text("p sp 4 2\na 1 2 2\na 2 3 3\np sp 4 2\na 1 2 8\na 2 3 4\n")
    paired = json.loads(run("query", "--input", combined, "--format", "dimacs", "--target", "2", "--algorithm", "ima", "--output", "json"))
    assert paired["targets"][0]["routes"][0]["cost"] == [5, 12]
    isolated = json.loads(run("query", "--input", combined, "--format", "dimacs", "--target", "3", "--algorithm", "ima", "--output", "json"))
    assert isolated["targets"][0]["routes"] == []
    prefixed = folder / "prefixed.gr"
    prefixed.write_text("1 3\n" + combined.read_text())
    run("query", "--input", prefixed, "--format", "dimacs", fail=True)
    normalized = folder / "osm.csv"
    normalized.write_text("from,to,time,consumption\n0,1,2,8\n1,2,3,-4\n")
    fixed = json.loads(run("query", "--input", normalized, "--model", "ev", "--capacity", "10", "--output", "json"))
    assert fixed["targets"][0]["routes"][0]["cost"] == [5, 8, 4, 6]
    coordinates = folder / "nodes.csv"
    coordinates.write_text("id,x,y\n0,0,0\n1,1,0\n2,2,0\n")
    located = json.loads(run("query", "--input", normalized, "--nodes", coordinates, "--model", "ev", "--capacity", "10", "--ellipse", "1.1", "--output", "json"))
    assert located["targets"][0]["routes"][0]["cost"] == [5, 8, 4, 6]
    cache = Path(folder) / "routes.cover"
    run("preprocess", "--input", root / "examples/routes.csv", "--output", cache, "--triangle", "true")
    cached = json.loads(run("query", "--input", root / "examples/routes.csv", "--algorithm", "t-kpc-mls", "--cover", cache, "--output", "json"))
    assert [r["cost"] for r in cached["targets"][0]["routes"]] == [[4, 16], [8, 6], [12, 2]]
    cache.write_text("broken")
    run("query", "--input", root / "examples/routes.csv", "--algorithm", "t-kpc-mls", "--cover", cache, fail=True)
for args in [
    ["query"], ["unknown"], ["demo", "--surprise", "1"], ["demo", "--source", "-1"],
    ["demo", "--source", "99"], ["demo", "--target", "1,"], ["demo", "--output", "yaml"],
    ["demo", "--algorithm", "mls", "--k", "4"], ["demo", "--algorithm", "mls", "--max-labels", "1"],
    ["demo", "--ellipse", "1.1"], ["query", "--input", "missing.csv"],
    ["query", "--preset", "independent2", "--input", "missing.csv"],
    ["query", "--preset", "ev", "--algorithm", "ima"], ["demo", "--epsilons", "nan,1"],
    ["benchmark", "--repeats", "0"], ["demo", "--source", "0", "--source", "1"], ["demo", "--triangle", "invalid"],
    ["benchmark", "--algorithms", "mls", "--triangle", "invalid"],
    ["benchmark", "--algorithms", "mls", "--triangle", "true"],
]:
    run(*args, fail=True)
help_text = run("--help")
assert "multicriteria" in help_text
assert "--format csv|dimacs" in help_text
assert "legacy-" not in help_text
assert help_text.split("--algorithm ", 1)[1].splitlines()[0].split("|") == ["mls", "t-mls", "kpc-mls", "t-kpc-mls", "ima", "blset", "t-kpc-ima"]
assert "0.1.0" in run("--version")
print(f"PASS {checks} CLI invocations")
