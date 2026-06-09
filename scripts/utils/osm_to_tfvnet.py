#!/usr/bin/env python3
"""osm_to_tfvnet.py — import a real road network from OpenStreetMap, CLEAN its topology
with OSMnx, and export a `.tfvnet` file for TrafficFlowViz's import generator
(sim.proc.mode=import, sim.import.path=<out>).

This is the offline preprocessing half of the "import + clean real data" pipeline
recommended by the road-generation deep-research report. It implements:
  1. OSM download (place name or bbox) or load a local .graphml / .osm.
  2. project_graph -> meters.
  3. simplify_graph  -> drop interstitial geometry-only nodes (the tiny vertices OSM uses to
     digitize curves), keeping the true intersection/dead-end nodes only.
  4. consolidate_intersections(rebuild_graph=True, tolerance, dead_ends=False) -> merge
     clustered near-duplicate intersection nodes (divided roads, roundabouts) into one node.
  5. OPTIONAL Douglas-Peucker geometry simplification (--simplify-m) -> straighten roads
     (OSMnx cleans *topology* but keeps the original curve geometry; this reduces bendiness).
  6. Map OSM highway tags -> TrafficFlowViz RoadClass, derive per-direction lanes + oneway.
  7. Re-index nodes to 0..N-1, centre on origin, FLIP Y (OSM/projected is y-up; the sim is
     y-down/SDL), and write NODE/EDGE lines.

Requires: osmnx (pip install osmnx). Needs internet for --place/--bbox.

Examples:
  python3 osm_to_tfvnet.py --place "Binjiang, Hangzhou, China" --out hangzhou.tfvnet
  python3 osm_to_tfvnet.py --bbox 37.79 37.81 -122.42 -122.40 --simplify-m 8 --out sf.tfvnet
  python3 osm_to_tfvnet.py --graphml mycity.graphml --tolerance 12 --out city.tfvnet
Then in the sim:
  ./trafficviz --sim.proc.mode=import --sim.import.path=hangzhou.tfvnet
"""
import argparse
import sys

# OSM 'highway' tag -> TrafficFlowViz RoadClass + default per-direction lane count.
_CLASS = [
    (("motorway", "motorway_link", "trunk", "trunk_link"), "HIGHWAY", 3),
    (("primary", "primary_link"), "ARTERIAL", 2),
    (("secondary", "secondary_link", "tertiary", "tertiary_link"), "COLLECTOR", 1),
]
_DEFAULT = ("LOCAL", 1)


def classify(highway):
    """Map an OSM highway tag (str or list) to (RoadClass, default_lanes_per_dir)."""
    if isinstance(highway, list):
        highway = highway[0] if highway else ""
    for tags, cls, lanes in _CLASS:
        if highway in tags:
            return cls, lanes
    return _DEFAULT


def first_int(value, default):
    """OSM attrs are sometimes lists/strings; pull the first parseable int."""
    if isinstance(value, list):
        value = value[0] if value else None
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def main():
    ap = argparse.ArgumentParser(description="OSM -> cleaned .tfvnet for TrafficFlowViz")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--place", help='e.g. "Binjiang, Hangzhou, China"')
    src.add_argument("--bbox", nargs=4, type=float, metavar=("N", "S", "E", "W"),
                     help="north south east west (lat/lon)")
    src.add_argument("--graphml", help="load a local .graphml instead of downloading")
    ap.add_argument("--out", required=True, help="output .tfvnet path")
    ap.add_argument("--tolerance", type=float, default=12.0,
                    help="intersection-consolidation tolerance in meters (default 12)")
    ap.add_argument("--simplify-m", type=float, default=0.0,
                    help="Douglas-Peucker tolerance (m) to straighten edge geometry; 0=off")
    ap.add_argument("--network-type", default="drive", help="OSMnx network_type (default drive)")
    args = ap.parse_args()

    try:
        import osmnx as ox
    except ImportError:
        sys.exit("error: osmnx not installed. Run:  pip install osmnx")

    # 1. Acquire graph.
    if args.graphml:
        G = ox.load_graphml(args.graphml)
    elif args.place:
        G = ox.graph_from_place(args.place, network_type=args.network_type)
    else:
        n, s, e, w = args.bbox
        try:                                            # OSMnx >= 2.0 takes a bbox tuple
            G = ox.graph_from_bbox(bbox=(w, s, e, n), network_type=args.network_type)
        except TypeError:                               # OSMnx < 2.0 took north,south,east,west
            G = ox.graph_from_bbox(n, s, e, w, network_type=args.network_type)

    # 2-4. Project to meters, simplify topology, consolidate clustered intersections.
    G = ox.project_graph(G)
    try:
        G = ox.simplify_graph(G)                        # may already be simplified -> ignore
    except Exception:
        pass
    consolidate = getattr(ox, "consolidate_intersections", None) \
        or ox.simplification.consolidate_intersections
    G = consolidate(G, tolerance=args.tolerance, rebuild_graph=True, dead_ends=False)

    # 6. Node coords (projected meters), centre + flip Y (OSM y-up -> sim y-down).
    nodes = list(G.nodes(data=True))
    xs = [d["x"] for _, d in nodes]
    ys = [d["y"] for _, d in nodes]
    cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
    idx = {osmid: i for i, (osmid, _) in enumerate(nodes)}

    lines = ["# generated by osm_to_tfvnet.py (meters, y-down)"]
    for osmid, d in nodes:
        lines.append(f"NODE {idx[osmid]} {d['x'] - cx:.2f} {-(d['y'] - cy):.2f}")

    # 7. Edges: dedup undirected pairs, derive class/lanes/oneway/geometry.
    seen = set()
    n_edges = 0
    for u, v, data in G.edges(data=True):
        if u == v:
            continue
        key = (min(idx[u], idx[v]), max(idx[u], idx[v]))
        if key in seen:
            continue
        seen.add(key)

        cls, default_lanes = classify(data.get("highway"))
        twoway = G.has_edge(v, u) and not data.get("oneway", False)
        oneway = 0 if twoway else 1
        total_lanes = first_int(data.get("lanes"), default_lanes * (1 if oneway else 2))
        per_dir = max(1, total_lanes // (1 if oneway else 2))

        geom = data.get("geometry")
        if args.simplify_m > 0 and geom is not None:
            geom = geom.simplify(args.simplify_m)       # Douglas-Peucker -> straighter roads
        pts = ""
        if geom is not None and geom.geom_type == "LineString":
            coords = list(geom.coords)
            if len(coords) > 2:                         # only emit genuinely-curved geometry
                pts = " " + " ".join(f"{x - cx:.2f},{-(y - cy):.2f}" for x, y in coords)

        lines.append(f"EDGE {idx[u]} {idx[v]} {cls} {per_dir} {oneway}{pts}")
        n_edges += 1

    with open(args.out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {args.out}: {len(nodes)} nodes, {n_edges} edges "
          f"(tolerance={args.tolerance} m, simplify={args.simplify_m} m)")


if __name__ == "__main__":
    main()
