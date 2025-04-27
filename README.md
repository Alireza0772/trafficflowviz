# TrafficFlowViz

High-performance C++/SDL engine for visualizing live and historical traffic flows with a modern ImGui interface and Python scripting support.

---

## Table of Contents
- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Installation & Build](#installation--build)
- [Usage](#usage)
- [Controls](#controls)
- [Python Scripting](#python-scripting)
- [Data Formats & Sample Data](#data-formats--sample-data)
- [Logging System](#logging-system)
- [Development & Contribution](#development--contribution)
- [License & Credits](#license--credits)
- [TODOs](#todos)

---

## Overview
TrafficFlowViz is a high-performance, extensible C++ engine for real-time and historical traffic flow visualization. It supports interactive simulation, live data feeds, and a dockable UI for research, prototyping, and demonstration purposes.

## Features
- Road network and vehicle CSV loaders
- Real-time vehicle simulation loop
- Pan & zoom visualization
- Python scripting bridge for automation and custom scenarios
- Dockable, resizable ImGui interface
- Real-time alerts and statistics panels
- Heatmap overlay for traffic density
- Structured, colorized logging system
- (Planned) WebSocket live feed adapter
- (Planned) Export to video/PNG

## Architecture
- **Core Engine:** Manages simulation, road networks, and entities
- **Rendering:** Uses SDL and ImGui for high-performance, interactive graphics
- **Data:** Loads road and vehicle data from CSV files
- **Python Bindings:** Exposes simulation controls to Python
- **Extensible:** Modular design for adding new features and data sources

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design notes.

## Installation & Build
### Prerequisites
- C++17 compiler (tested on macOS, Linux)
- CMake 3.15+
- Python 3.8+
- SDL2, ImGui (included as submodules)

### Build Instructions
```bash
# Clone repository with submodules
git clone --recursive https://github.com/yourusername/trafficflowviz.git
cd trafficflowviz

# If you already cloned without --recursive, initialize submodules:
git submodule update --init --recursive

# Build (Debug)
./build.sh

# Build (Release)
./build.sh release
```

## Usage
### Run the Application
```bash
./build/bin/trafficviz
```

### Python Scripting
Set the Python path and run scripts:
```bash
export PYTHONPATH=build/python:$PYTHONPATH
python scripts/simulations/rush_hour.py
```

## Controls
See [KEYBINDINGS.md](KEYBINDINGS.md) for a complete list of keyboard shortcuts and controls.
- Press `I` to toggle the ImGui interface
- Use mouse to pan/zoom

## Data Formats & Sample Data
Sample datasets are provided in the `data/` directory.

| File                   | Description                                |
| ---------------------- | ------------------------------------------ |
| `roads_complex.csv`    | Octagonal ring + two diagonal connectors   |
| `vehicles_complex.csv` | 40 vehicles pre-positioned on all segments |

See `data/roads/` and `data/vehicles/` for more examples. Data format documentation is in [Product Requirements Document.md](Product%20Requirments%20Document.md).

## Logging System
TrafficFlowViz features a global, structured, and colorized logging system for all C++ code:
- Use macros: `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, and `LOG_DEBUG` (debug-only)
- Parameterized logs: use `PARAM(name, value)` for each named parameter
- Example:
  ```cpp
  LOG_INFO("Loaded {count} vehicles from {file}", PARAM(count, 42), PARAM(file, filename));
  LOG_ERROR("Failed to open file: {file}", PARAM(file, filename));
  LOG_DEBUG("Debug info: {val}", PARAM(val, someValue));
  ```
- Each log includes timestamp, log level, filename:line, and colorized parameters
- LOG_DEBUG only emits logs in debug builds

## Development & Contribution
- Format code with `clang-format` (see VS Code tasks)
- Build and run using provided shell scripts or VS Code tasks
- See [ARCHITECTURE.md](ARCHITECTURE.md) and [Product Requirements Document.md](Product%20Requirments%20Document.md) for design and requirements
- Contributions welcome! Please open issues or pull requests

## License & Credits
- See `LICENSE` (if present) for license details
- ImGui and SDL2 are included under their respective licenses in `external/`
- Developed by [Your Name/Team], 2023–2025

---

## TODOs

- [x] Road CSV loader implemented (FR-02)
- [x] Vehicle simulation loop functional (FR-04)
- [x] Real-time pan & zoom enabled (FR-04)
- [x] Python scripting bridge integrated (FR-07)
- [x] Dockable UI with ImGui (FR-04, FR-09)
- [x] Heatmap overlay for traffic density (FR-05)
- [x] Structured, colorized logging system
- [x] Sample datasets provided
- [x] Build scripts and VS Code tasks configured
- [ ] Live data ingestion via REST/WebSocket (FR-01)
- [ ] Historical log loader for CSV/Parquet (FR-02)
- [ ] Multi-source sensor fusion (FR-03)
- [ ] Congestion heatmaps with tooltips (FR-05)
- [ ] Travel-time isochrone computation and export (FR-06)
- [ ] Plugin system for custom DSP/ML modules (FR-08)
- [ ] Alerts & event rules with rule editor UI (FR-09)
- [ ] Session recording and replay (FR-10)
- [ ] Export to video/PNG functionality
- [ ] Enhanced statistics and analytics panels
- [ ] Improved error handling and user feedback
- [ ] Cross-platform build/test automation
- [ ] User and developer documentation expansion
- [ ] Self-driving traffic entities (simulation and visualization)

For questions or support, open an issue on GitHub.
