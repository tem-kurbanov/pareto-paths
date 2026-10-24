"""Behavior checks for dataset normalization, packaging and extraction."""
import csv
import gzip
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("dataset_tools", ROOT / "tools/datasets.py")
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)

NODE_HEADER = "elevation,id,latitude,latitudeProjected,longitude,longitudeProjected\n"
GRAPH = "c distance\np sp 3 2\na 1 2 2\na 2 1 3\nc time\np sp 3 2\na 1 2 8\na 2 1 4\n"


class Datasets(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="pareto-dataset-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "bayern_nodes.csv").write_text(NODE_HEADER + "1,0,10,100,20,200\n2,1,11,101,21,201\n3,2,12,102,22,202\n")
        (self.source / "bayern_edges.csv").write_text("comsumption,fromId,time,toId\n8,0,2,1\n-4,1,3,0\n")
        (self.source / "USA-road-f.NY.gr").write_text("1 3\n2 1\n" + GRAPH)
        self.bundle = self.root / "bundle"

    def prepare(self):
        return tools.prepare(self.source, self.bundle, ["bayern", "NY"])

    def test_normalize_and_roundtrip(self):
        originals = {p.name: p.read_bytes() for p in self.source.iterdir()}
        manifest = self.prepare()
        bayern, ny = manifest["datasets"]
        self.assertEqual((bayern["nodes"], bayern["edges"]), (3, 2))
        self.assertEqual(bayern["criteria"], ["time", "consumption"])
        self.assertEqual(bayern["license"], "ODbL-1.0")
        self.assertEqual(ny["removed_test_query_pairs"], 2)
        self.assertEqual(ny["nodes"], 3)  # Includes isolated vertex 3 from p sp.
        destination = self.root / "extracted"
        paths = tools.extract(self.bundle, destination, ["bayern", "NY"])
        self.assertEqual(len(paths), 3)
        with (destination / "osm/bayern_edges.csv").open() as stream:
            self.assertEqual(list(csv.reader(stream)), [
                ["from", "to", "time", "consumption"], ["0", "1", "2", "8"], ["1", "0", "3", "-4"]])
        with (destination / "osm/bayern_nodes.csv").open() as stream:
            self.assertEqual(list(csv.reader(stream)), [
                ["id", "x", "y"], ["0", "100", "200"], ["1", "101", "201"], ["2", "102", "202"]])
        self.assertEqual((destination / "dimacs/NY.gr").read_text(), GRAPH)
        self.assertEqual({p.name: p.read_bytes() for p in self.source.iterdir()}, originals)
        self.assertEqual(tools.extract(self.bundle, destination, ["bayern", "NY"]), paths)
        (destination / "osm/bayern_edges.csv").write_text("user edit\n")
        with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
            tools.extract(self.bundle, destination, ["bayern"])

    def test_deterministic_archives(self):
        first = self.prepare()
        second_bundle = self.root / "second"
        second = tools.prepare(self.source, second_bundle, ["bayern", "NY"])
        self.assertEqual(first, second)
        for dataset in first["datasets"]:
            for entry in dataset["files"]:
                self.assertEqual((self.bundle / entry["archive"]).read_bytes(), (second_bundle / entry["archive"]).read_bytes())

    def test_no_query_prefix_and_mismatched_arcs(self):
        source = self.source / "USA-road-f.NY.gr"
        output = self.root / "graph.gr"
        source.write_text(GRAPH)
        self.assertEqual(tools.clean_dimacs(source, output), (3, 2, 0))
        source.write_text(GRAPH.replace("a 2 1 4", "a 2 3 4"))
        with self.assertRaisesRegex(ValueError, "ordering"):
            tools.clean_dimacs(source, output)
        source.write_text(GRAPH.rsplit("a 2 1 4", 1)[0])
        with self.assertRaisesRegex(ValueError, "complete"):
            tools.clean_dimacs(source, output)

    def test_invalid_source_values(self):
        edge_source = self.source / "bayern_edges.csv"
        for row in ["8,9,2,1", "8,0,0,1", "bad,0,2,1"]:
            edge_source.write_text("comsumption,fromId,time,toId\n" + row + "\n")
            with self.assertRaises(ValueError):
                tools.osm_edges(edge_source, self.root / "edges.csv", 3)
        (self.source / "USA-road-f.NY.gr").write_text("1 4\n" + GRAPH)
        with self.assertRaisesRegex(ValueError, "endpoint"):
            tools.clean_dimacs(self.source / "USA-road-f.NY.gr", self.root / "graph.gr")

    def test_corruption_and_unsafe_paths(self):
        manifest = self.prepare()
        entry = manifest["datasets"][0]["files"][0]
        archive = self.bundle / entry["archive"]
        original = archive.read_bytes()
        archive.write_bytes(original + b"corrupt")
        destination = self.root / "extracted"
        with self.assertRaisesRegex(ValueError, "compressed checksum"):
            tools.extract(self.bundle, destination, ["bayern"])
        self.assertFalse((destination / entry["path"]).exists())
        archive.write_bytes(original)
        entry["sha256"] = "0" * 64
        (self.bundle / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "extracted checksum"):
            tools.extract(self.bundle, destination, ["bayern"])
        self.assertFalse((destination / entry["path"]).exists())
        entry["path"] = "../outside.csv"
        (self.bundle / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "unsafe"):
            tools.extract(self.bundle, destination, ["bayern"])
        self.assertFalse((self.root / "outside.csv").exists())


if __name__ == "__main__":
    unittest.main()
