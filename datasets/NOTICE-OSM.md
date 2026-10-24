# OpenStreetMap-derived datasets

The Bavaria (`bayern`) and Germany graph and coordinate files in `osm/` are derived from OpenStreetMap, with residential roads removed during the original research preprocessing.

**© OpenStreetMap contributors.** OpenStreetMap data is available under the **Open Data Commons Open Database License (ODbL) 1.0**:

- [OpenStreetMap copyright and attribution](https://www.openstreetmap.org/copyright)
- [ODbL 1.0 license](https://opendatacommons.org/licenses/odbl/1-0/)

The supplied research datasets are distributed as ODbL-derived databases. The repository's MIT software license does not replace the dataset license. The preparation tool normalizes edge and coordinate columns and packaging; it preserves the supplied topology, node IDs, projected coordinates, and criterion values. It does not regenerate a current OSM extract or repeat the residential-road filtering.
