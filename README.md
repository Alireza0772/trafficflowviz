# TrafficFlowViz

High‑performance C++/SDL engine for visualising live and historical traffic flows.

## Quick Start
```bash
./build.sh             # build debug
./build/bin/trafficviz # run sample
export PYTHONPATH=build/python:$PYTHONPATH
python scripts/rush_hour.py  # drive from Python
```

## Controls
See [KEYBINDINGS.md](KEYBINDINGS.md) for a complete list of keyboard shortcuts and controls.

## Sample Data Sets
| file | description |
|------|-------------|
| `roads_complex.csv` | octagonal ring + two diagonal connectors |
| `vehicles_complex.csv` | 40 vehicles pre‑positioned on all segments |

## Feature Checklist

- [x]  Road CSV loader
- [x] Vehicle simulation loop
- [x] Real‑time pan & zoom
- [x] Python scripting bridge
- [ ] Web‑socket live feed adapter
- [ ] Heat‑map overlay
- [ ] Export video / PNG

## Development Schedule
Import `schedule.ics` into Apple Calendar to see sprint milestones.
