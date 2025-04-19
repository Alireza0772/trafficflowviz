#!/usr/bin/env python3
"""Launch TrafficFlowViz from Python."""
import pathlib, sys
root = pathlib.Path(__file__).resolve().parents[1]
build_python = root / "build" / "python"
if build_python.exists():
    sys.path.insert(0, str(build_python))
import trafficviz
a = trafficviz.Engine("Rush Hour", 1280, 720)
a.run()
print("Simulation finished")
