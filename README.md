# TrafficFlowViz

**High‑performance C++ platform for live & historical traffic visualisation, analytics and reinforcement‑learning research.**

---

## Table of Contents

- [TrafficFlowViz](#trafficflowviz)
  - [Table of Contents](#tableofcontents)
  - [Overview](#overview)
  - [Key Features](#keyfeatures)
  - [Quick Start](#quickstart)
  - [Detailed Installation](#detailedinstallation)
    - [Prerequisites](#prerequisites)
    - [Build Options](#buildoptions)
  - [Python API](#pythonapi)
  - [Data Formats](#dataformats)
  - [Docker \& Dev Containers](#dockerdevcontainers)
  - [Testing \& CI](#testingci)
  - [Contributing](#contributing)
  - [Roadmap](#roadmap)
  - [License \& Citation](#licensecitation)
  - [Architecture (Short)](#architectureshort)

---

## Overview

TrafficFlowViz (TFV) lets city engineers, researchers and data‑scientists **ingest 100 k+ msgs s⁻¹**, replay months of log data and experiment with RL/GNN control policies from the comfort of a Jupyter notebook – all while maintaining **≥ 60 FPS** interactive visuals on commodity GPUs.

> **Why another traffic viewer?**  Existing tools focus either on microscopic simulation (SUMO, Vissim) or pretty but non‑interactive dashboards. TFV bridges the gap: native‑code performance + modern UI + first‑class Python bridge.

---

## Key Features

| Area                | Highlights                                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------------------- |
| **Visualisation**   | Zoomable tiled map; vector arrows & heatmaps; layer toggle & ordering; movie/GIF snapshot export.          |
| **Simulation Core** | Deterministic fixed‑step loop; live & historical replay; congestion metrics; alert rules.                  |
| **Data Ingestion**  | REST/WebSocket adapter (JSON/Proto); CSV/Parquet loader; pluggable broker (Kafka/NATS).                    |
| **Python Bindings** | Gym‑like `step() / reset()` API; NumPy zero‑copy buffers; wheel builds for macOS universal2, Linux x86‑64. |
| **Plugin System**   | Load C/C++/Rust or ONNX models at runtime via stable C ABI + watchdog thread.                              |
| **Observability**   | Structured colour logs; Prometheus metrics; optional Jaeger tracing.                                       |
| **Portability**     | Single code‑base; supports macOS, Linux, Windows. Continuous builds on all three.                          |

---

## Quick Start

```bash
# clone incl. submodules
$ git clone --recursive https://github.com/yourname/trafficflowviz.git
$ cd trafficflowviz

# build (debug)
$ ./build.sh            # uses CMake + Ninja

# run demo scene
$ ./build/bin/tfv_demo  # loads data/roads_demo.csv by default
```

> **Tip:** add `./scripts` to your PATH – it contains one‑liners for common workflows (profiling, sanitizers, packaging).

---

## Detailed Installation

### Prerequisites

* **Compiler:** Clang 16 or GCC 13 (C++23)
* **CMake:** ≥ 3.25
* **Python:** ≥ 3.8 (optional but recommended)
* **Libraries:** SDL2, ImGui (vendored); zstd; Arrow
* **macOS only:** Command‑Line Tools + (optionally) MoltenVK

### Build Options

| CMake Flag           | Default | Description                                         |
| -------------------- | ------- | --------------------------------------------------- |
| `TFV_RENDERER`       | `SDL`   | `SDL`, `Metal` or `Vulkan`                          |
| `TFV_BUILD_PYTHON`   | `ON`    | Build wheels + install to `${CMAKE_INSTALL_PREFIX}` |
| `TFV_USE_SANITIZERS` | `OFF`   | Address/UBSan for debug builds                      |
| `TFV_ENABLE_DOCS`    | `OFF`   | Build Doxygen + Sphinx docs                         |

```bash
cmake -B build -S . -DTFV_RENDERER=Metal -DTFV_BUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

---

## Python API

After installing the wheel (`pip install dist/trafficflowviz‑*.whl`) use it just like an OpenAI Gym environment:

```python
import trafficflowviz as tfv

env = tfv.Env("data/munich_5min.parquet")
obs = env.reset()
for _ in range(1_000):
    action = my_policy(obs)
    obs, reward, done, info = env.step(action)
    if done:
        obs = env.reset()
```

Key modules:

* `tfv.core` – direct binding of C++ Simulation / Engine
* `tfv.datasets` – helpers to download or convert public traffic logs
* `tfv.rl` – wrappers for RL lib / Ray RLlib

API reference is auto‑generated in the online docs.

---

## Data Formats

| File                 | Schema                             | Purpose              |
| -------------------- | ---------------------------------- | -------------------- |
| `roads_*.csv`        | id, from, to, length, speed\_limit | Static road topology |
| `vehicles_live.json` | id, ts, lat, lon, speed            | Live feed messages   |
| `*.parquet`          | Arrow schema (see docs)            | Bulk historical logs |

Conversion utilities:

```bash
python tools/convert_csv_to_parquet.py data/raw/*.csv -o data/converted/
```

---

## Docker & Dev Containers

The repo ships a **VS Code dev‑container** and a minimal **Docker runtime** image.

```bash
# one‑liner to start interactive container with hot‑reload
$ ./scripts/devcontainer.sh
```

The Dockerfile can be used to run headless simulations on a server and stream rendered frames via WebRTC.

---

## Testing & CI

* **Unit tests:** Catch2 (`ctest -L unit`)
* **Coverage:** gcov + gcovr → Codecov badge on PRs
* **Static analysis:** clang‑tidy, cppcheck, include‑what‑you‑use
* **CI matrix:** macOS 12, Ubuntu 22.04, Windows 2022
* **Artifact upload:** Signed Python wheels to TestPyPI on tags

Run all checks locally:

```bash
./scripts/run_local_ci.sh
```

---

## Contributing

Pull‑requests are welcome! Please:

1. Create an issue first if you plan a major change.
2. Follow the **Git Conventional Commits** style.
3. Run `./scripts/clang_format_all.sh` before pushing.
4. Ensure `./scripts/run_local_ci.sh` passes.
5. Sign the CLA.

See [CONTRIBUTING.md] for full guidelines.

---

## Roadmap

Planned high‑level milestones (see GitHub Projects board for detail):

| Quarter     | Milestone                                          |
| ----------- | -------------------------------------------------- |
| **Q2 2025** | Live feed ingest (FR‑01); Python 0.1 wheel on PyPI |
| **Q3 2025** | Plugin ABI v1; Isochrone export (FR‑06)            |
| **Q4 2025** | Distributed ingest cluster; Windows DX12 renderer  |
| **Q1 2026** | Self‑driving entity simulation; VR support         |

---

## License & Citation

* Code: **MIT License** (see `LICENSE`)
* Third‑party deps: ImGui (MIT), SDL2 (zlib), etc.
* If you use TFV in academic work please cite:

```
@software{TFV2025,
  author  = {Senobari, Alireza *et al.*},
  title   = {TrafficFlowViz – High‑Performance Traffic Visualisation & Simulation},
  year    = {2025},
  url     = {https://github.com/yourname/trafficflowviz}
}
```

---

## Architecture (Short)

TFV follows a **layered engine** pattern inspired by Walnut:

```
┌──────────┐     messages     ┌───────────────┐
│ LiveFeed │ ───────────────▶ │ Simulation    │
└──────────┘                  │  (threads)    │
                              └─────▲─────────┘
            snapshot VehicleMap     │
                              ┌─────┴─────────┐
                              │ Rendering     │
                              │  SDL / Metal  │
                              └─────▲─────────┘
                                    │ ImGui draw lists
                              ┌─────┴─────────┐
                              │ ImGuiLayer    │
                              └───────────────┘
```

See the full specification below for deeper details.

---
