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
for algorithm in ["mls", "t-mls", "kpc-mls", "t-kpc-mls", "ima", "blset", "kpc-ima", "kpc-ima-straight"]:
    actual = json.loads(run("query", "--input", root / "examples/routes.csv", "--algorithm", algorithm, "--output", "json"))
    assert [r["cost"] for r in actual["targets"][0]["routes"]] == [[4, 16], [8, 6], [12, 2]]
ev = json.loads(run("query", "--input", root / "examples/ev.csv", "--model", "ev", "--capacity", "10", "--output", "json"))
assert [r["cost"] for r in ev["targets"][0]["routes"]] == [[8, 3, 1, 9], [10, 0, 0, 10]]
for preset in ["independent2", "independent3", "correlated2", "correlated3", "reciprocal", "ev"]:
    rows = list(csv.DictReader(io.StringIO(run("benchmark", "--preset", preset, "--nodes-count", "12", "--k", "4", "--queries", "2", "--repeats", "1"))))
    assert len(rows) == 8
    assert all(int(row["expanded"]) > 0 and float(row["query_seconds"]) > 0 for row in rows)
with tempfile.TemporaryDirectory() as folder:
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
assert "multicriteria" in run("--help")
assert "0.1.0" in run("--version")
print(f"PASS {checks} CLI invocations")
