# TrafficFlowViz

High‑performance C++/SDL engine for visualising live and historical traffic flows.

## Quick Start
```bash
# Clone repository with submodules
git clone --recursive https://github.com/yourusername/trafficflowviz.git
cd trafficflowviz

# If you already cloned without --recursive, initialize submodules:
git submodule update --init --recursive

# Build and run
./build.sh             # build debug
./build/bin/trafficviz # run sample
export PYTHONPATH=build/python:$PYTHONPATH
python scripts/rush_hour.py  # drive from Python
```

## Controls
See [KEYBINDINGS.md](KEYBINDINGS.md) for a complete list of keyboard shortcuts and controls.

## ImGui Interface
The application features a fully-dockable ImGui interface with:
- Resizable and dockable windows
- Control panel for adjusting simulation parameters
- Real-time alerts panel
- Statistics display
- Press `I` to toggle the UI interface

## Sample Data Sets
| file | description |
|------|-------------|
| `roads_complex.csv` | octagonal ring + two diagonal connectors |
| `vehicles_complex.csv` | 40 vehicles pre‑positioned on all segments |

## Feature Checklist

- [x] Road CSV loader
- [x] Vehicle simulation loop
- [x] Real‑time pan & zoom
- [x] Python scripting bridge
- [x] Dockable UI with ImGui
- [ ] Web‑socket live feed adapter
- [x] Heat‑map overlay
- [ ] Export video / PNG

## Development Schedule
Import `schedule.ics` into Apple Calendar to see sprint milestones.
