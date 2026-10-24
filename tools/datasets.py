#!/usr/bin/env python3
"""Prepare or extract the bundled road-network datasets (Python standard library only)."""
import argparse
from array import array
import csv
import gzip
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tempfile

REGIONS = ("NY", "BAY", "COL", "FLA", "NE", "NW")
DATASETS = ("bayern", "germany", *REGIONS)
OSM_URL = "https://www.openstreetmap.org/copyright"
ODBL_URL = "https://opendatacommons.org/licenses/odbl/1-0/"
DIMACS_URL = "https://www.diag.uniroma1.it/~challenge9/download.shtml"


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pack(raw, bundle, relative, source):
    """Deterministic gzip plus hashes for both compressed and extracted bytes."""
    archive = Path(relative + ".gz")
    target = bundle / archive
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".tmp")
    try:
        with temporary.open("wb") as output:
            with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0, compresslevel=6) as zipped:
                with raw.open("rb") as stream:
                    shutil.copyfileobj(stream, zipped)
        result = {
            "archive": archive.as_posix(), "path": relative,
            "sha256": sha256(raw), "bytes": raw.stat().st_size,
            "gzip_sha256": sha256(temporary), "compressed_bytes": temporary.stat().st_size,
            "source_name": source.name, "source_sha256": sha256(source),
        }
        temporary.replace(target)
        return result
    finally:
        temporary.unlink(missing_ok=True)


def osm_nodes(source, target):
    header = ["elevation", "id", "latitude", "latitudeProjected", "longitude", "longitudeProjected"]
    count = 0
    with source.open(newline="") as stream, target.open("w", newline="") as output:
        reader = csv.reader(stream)
        writer = csv.writer(output, lineterminator="\n")
        if next(reader, None) != header:
            raise ValueError(f"{source}: unexpected node header")
        writer.writerow(["id", "x", "y"])
        for row in reader:
            if len(row) != 6 or int(row[1]) != count:
                raise ValueError(f"{source}: expected consecutive zero-based node IDs")
            writer.writerow([row[1], row[3], row[5]])
            count += 1
    if not count:
        raise ValueError(f"{source}: no nodes")
    return count


def osm_edges(source, target, nodes):
    count = 0
    with source.open(newline="") as stream, target.open("w", newline="") as output:
        reader = csv.reader(stream)
        header = next(reader, None)
        if header not in (["comsumption", "fromId", "time", "toId"],
                          ["consumption", "fromId", "time", "toId"]):
            raise ValueError(f"{source}: unexpected edge header")
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(["from", "to", "time", "consumption"])
        for line, row in enumerate(reader, 2):
            if len(row) != 4:
                raise ValueError(f"{source}:{line}: expected four fields")
            consumption, start, time, end = map(int, row)
            if not (0 <= start < nodes and 0 <= end < nodes) or time <= 0:
                raise ValueError(f"{source}:{line}: invalid endpoint or nonpositive traversal time")
            writer.writerow([row[1], row[3], row[2], row[0]])
            count += 1
    if not count:
        raise ValueError(f"{source}: no edges")
    return count


def clean_dimacs(source, target):
    """Keep both graph blocks, remove the leading private test-query pairs."""
    blocks = 0
    nodes = expected = count = removed = 0
    prefix_pairs = []
    endpoints = array("Q")
    with source.open() as stream, target.open("w") as output:
        for line_number, original in enumerate(stream, 1):
            fields = original.split()
            if not fields:
                continue
            kind = fields[0]
            if kind == "c":
                output.write(original.rstrip("\r\n") + "\n")
                continue
            if kind == "p":
                if len(fields) != 4 or fields[1] != "sp" or blocks >= 2:
                    raise ValueError(f"{source}:{line_number}: invalid graph header")
                n, m = map(int, fields[2:])
                if n <= 0 or m < 0:
                    raise ValueError(f"{source}:{line_number}: invalid graph size")
                if blocks and (count != expected or n != nodes or m != expected):
                    raise ValueError(f"{source}:{line_number}: paired graph counts differ")
                if not blocks:
                    nodes, expected = n, m
                    if any(not (1 <= a <= nodes and 1 <= b <= nodes) for a, b in prefix_pairs):
                        raise ValueError(f"{source}: test-query endpoint out of range")
                    prefix_pairs.clear()
                blocks += 1
                count = 0
            elif kind == "a":
                if not blocks or len(fields) != 4:
                    raise ValueError(f"{source}:{line_number}: invalid arc")
                a, b, weight = map(int, fields[1:])
                if not (1 <= a <= nodes and 1 <= b <= nodes) or weight < 0 or count >= expected:
                    raise ValueError(f"{source}:{line_number}: invalid arc values/count")
                if blocks == 1:
                    endpoints.extend([a, b])
                elif (a, b) != (endpoints[2 * count], endpoints[2 * count + 1]):
                    raise ValueError(f"{source}:{line_number}: paired arc ordering differs")
                count += 1
            elif not blocks and len(fields) == 2:
                try:
                    prefix_pairs.append(tuple(map(int, fields)))
                except ValueError as error:
                    raise ValueError(f"{source}:{line_number}: invalid test-query prefix") from error
                removed += 1
                continue
            else:
                raise ValueError(f"{source}:{line_number}: expected DIMACS p/a/c record")
            output.write(original.rstrip("\r\n") + "\n")
    if blocks != 2 or count != expected:
        raise ValueError(f"{source}: expected two complete distance/time graph blocks")
    return nodes, expected, removed


def prepare(source, bundle, selected=DATASETS):
    source, bundle = Path(source), Path(bundle)
    bundle.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(prefix="pareto-datasets-") as temporary:
        scratch = Path(temporary)
        for name in selected:
            if name in ("bayern", "germany"):
                node_source = source / f"{name}_nodes.csv"
                edge_source = source / f"{name}_edges.csv"
                node_raw, edge_raw = scratch / "nodes.csv", scratch / "edges.csv"
                nodes = osm_nodes(node_source, node_raw)
                edges = osm_edges(edge_source, edge_raw, nodes)
                files = [pack(edge_raw, bundle, f"osm/{name}_edges.csv", edge_source),
                         pack(node_raw, bundle, f"osm/{name}_nodes.csv", node_source)]
                record = {
                    "id": name, "format": "csv", "model": "ev", "criteria": ["time", "consumption"],
                    "nodes": nodes, "edges": edges, "source": OSM_URL,
                    "attribution": "© OpenStreetMap contributors", "license": "ODbL-1.0",
                    "license_url": ODBL_URL,
                    "modifications": ["Residential roads removed in the supplied research graph",
                                      "Edge columns reordered to from,to,time,consumption; weights and IDs preserved",
                                      "Node columns normalized to id,x,y using the supplied projected coordinates"],
                    "files": files,
                }
            elif name in REGIONS:
                original = source / f"USA-road-f.{name}.gr"
                raw = scratch / "dimacs.gr"
                nodes, edges, removed = clean_dimacs(original, raw)
                record = {
                    "id": name, "format": "dimacs", "model": "additive", "criteria": ["distance", "time"],
                    "nodes": nodes, "edges": edges, "source": DIMACS_URL,
                    "attribution": "9th DIMACS Implementation Challenge; U.S. Census TIGER/Line road networks",
                    "license": "Public-domain collection unless otherwise noted by the challenge",
                    "removed_test_query_pairs": removed,
                    "modifications": ["Leading private test-query pairs removed; both graph blocks and arc order preserved"],
                    "files": [pack(raw, bundle, f"dimacs/{name}.gr", original)],
                }
            else:
                raise ValueError(f"unknown dataset: {name}")
            records.append(record)
            print(f"Prepared {name}: {nodes:,} nodes, {edges:,} edges", flush=True)
    manifest = {"schema_version": 1, "datasets": records}
    temporary = bundle / "manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(bundle / "manifest.json")
    return manifest


def relative_path(text):
    path = PurePosixPath(text)
    if path.is_absolute() or not path.parts or any(part in (".", "..") for part in path.parts) or "\\" in text:
        raise ValueError(f"unsafe dataset path: {text}")
    return Path(*path.parts)


def extract(bundle, destination, selected):
    bundle, destination = Path(bundle).resolve(), Path(destination).resolve()
    manifest = json.loads((bundle / "manifest.json").read_text())
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported dataset manifest version")
    catalog = {row["id"]: row for row in manifest["datasets"]}
    for name in selected:
        if name not in catalog:
            raise ValueError(f"unknown dataset: {name}")
    extracted = []
    for name in selected:
        for entry in catalog[name]["files"]:
            archive = (bundle / relative_path(entry["archive"])).resolve()
            target = (destination / relative_path(entry["path"])).resolve()
            if not archive.is_relative_to(bundle) or not target.is_relative_to(destination):
                raise ValueError("dataset path escapes its directory")
            if sha256(archive) != entry["gzip_sha256"]:
                raise ValueError(f"compressed checksum mismatch: {archive}")
            if target.exists():
                if sha256(target) != entry["sha256"]:
                    raise ValueError(f"refusing to overwrite modified dataset: {target}")
                extracted.append(target)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".extract-", delete=False) as out:
                temporary = Path(out.name)
                try:
                    with gzip.open(archive, "rb") as stream:
                        shutil.copyfileobj(stream, out)
                except BaseException:
                    temporary.unlink(missing_ok=True)
                    raise
            try:
                if temporary.stat().st_size != entry["bytes"] or sha256(temporary) != entry["sha256"]:
                    raise ValueError(f"extracted checksum/size mismatch: {archive}")
                temporary.replace(target)
            finally:
                temporary.unlink(missing_ok=True)
            extracted.append(target)
    return extracted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="normalize original local sources and build deterministic archives")
    prep.add_argument("--source", type=Path, default=Path("../data"))
    prep.add_argument("--bundle", type=Path, default=Path("datasets"))
    prep.add_argument("--only", nargs="+", choices=DATASETS, default=list(DATASETS))
    unpack = sub.add_parser("extract", help="verify checksums and extract selected bundled graphs")
    unpack.add_argument("datasets", nargs="+", choices=DATASETS)
    unpack.add_argument("--bundle", type=Path, default=Path("datasets"))
    unpack.add_argument("--destination", type=Path, default=Path("data"))
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.source, args.bundle, args.only)
        else:
            for path in extract(args.bundle, args.destination, args.datasets):
                print(path)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"datasets: {error}\n")


if __name__ == "__main__":
    main()
