# TrafficFlowViz

**An agent-based traffic microsimulation in C++.** Every vehicle runs a pluggable *brain* — a built-in rule model, a configurable neural net, or your own trained model (C/DLL, Python, or ONNX) — in a closed **sense → decide → act** loop, with expressive real-time visualization and reproducible data export. Built for traffic / reinforcement-learning research.

> **Status:** research prototype. It is a single-process SDL2 application with a deterministic, *same-build* reproducible core (see [Determinism](#determinism--testing)). It is **not** a distributed ingest platform — the items in [Roadmap](#roadmap) are explicitly not built yet.

---

## Table of Contents

- [Overview](#overview)
- [Key features](#key-features)
- [Quick start](#quick-start)
- [Build](#build)
- [Plug in a brain](#plug-in-a-brain)
- [Observe & export](#observe--export)
- [Determinism & testing](#determinism--testing)
- [Documentation](#documentation)
- [Project layout](#project-layout)
- [Roadmap](#roadmap)
- [License & citation](#license--citation)
- [Architecture (short)](#architecture-short)

---

## Overview

TrafficFlowViz (TFV) simulates road traffic at the level of individual vehicles. The engine advances on a fixed timestep; on each tick every vehicle perceives its surroundings, a decision *brain* maps that perception to an action, an `ActionValidator` bounds the action, and the engine integrates motion under road, lane, sign, and traffic-light constraints. The decision seam is the whole point: the same engine runs a hand-written car-following model, a neural network, or an externally trained policy with no engine changes.

It began as a passive visualizer and was migrated, strangler-fig style, into this microsimulation behind a stable `Simulation` facade — so the renderer kept working at every step. See [`docs/agent-simulation-design.md`](docs/agent-simulation-design.md) for the design and [`docs/ONBOARDING.md`](docs/ONBOARDING.md) for a hands-on usage guide.

---

## Key features

| Area | What's actually here |
| --- | --- |
| **Agent model** | IDM longitudinal car-following (`rule`), asymmetric keep-right **MOBIL** lane changes, deterministic BFS routing-by-intent, STOP/YIELD/speed-limit signs, and a central traffic-light controller (green → amber → all-red). |
| **Pluggable brains** | One `IBrain` seam (24-channel `Observation` → `Action`) + a versioned **C-ABI vtable**. Backends: built-in configurable **NN**, **C/DLL** (`dlopen`), **embedded Python** (pybind11), and **ONNX Runtime**. Each is opt-in and **fail-soft** (any failure falls back to the rule brain). |
| **Perception** | Authoritative world pose/heading, a uniform-grid spatial index, and four ego-frame F/R/L/R neighbour sectors. |
| **Observability** | Vehicle/brain **Inspector** (live 24-channel observation, action, violations), a **perception overlay**, a congestion heatmap, and screenshot/video recording. |
| **Reproducible export** | `RunManifest` (seed + config + brain hash + final-state digest), per-tick **trajectory** and per-segment **metrics** CSV, a windowless **`tfv_headless`** batch runner, and a GUI export toggle that byte-matches the headless output. |
| **Determinism** | Fixed timestep, seeded streams, no RNG in the step path; a standalone **golden-trajectory** test pins the behaviour (see below). |

---

## Quick start

```bash
# clone (ImGui is a submodule)
git clone --recursive https://github.com/Alireza0772/trafficflowviz.git
cd trafficflowviz

# configure + build (default preset, no extra dependencies)
cmake --preset default
cmake --build build

# run the GUI
./build/bin/trafficviz

# run the determinism test
ctest --test-dir build -R golden_trajectory      # expect: PASS: golden_trajectory (15 gates)

# headless reproducible run -> manifest.json + trajectory.csv + metrics.csv
./build/bin/tfv_headless --export-ticks=1200 --export-dir=out --sim-default-brain=nn
```

The default build produces three binaries in `build/bin/`: **`trafficviz`** (interactive GUI), **`tfv_headless`** (batch/export runner), and **`tfv_golden_test`** (the determinism contract).

---

## Build

**Prerequisites**

- A C++23 compiler (Clang or GCC)
- CMake ≥ 3.25
- SDL2 + SDL2_image + SDL2_ttf, and glm
- ImGui (vendored as a git submodule — use `--recursive`, or `git submodule update --init`)
- *Optional:* Python 3 + pybind11 (for the `python:` backend); ONNX Runtime (for the `onnx:` backend)

**Presets** (`CMakePresets.json`): `default`, `debug`, `vcpkg` (portable, needs `$VCPKG_ROOT`), `release`, `ci`. Non-default presets configure into their own build dir (e.g. `build-release/`).

**Options**

| CMake flag | Default | Description |
| --- | --- | --- |
| `TFV_WITH_PYTHON` | `OFF` | Embedded-Python brain backend (`python:`); needs Python3 Development.Embed + pybind11. |
| `TFV_WITH_ONNX` | `OFF` | ONNX Runtime brain backend (`onnx:`); needs ONNX Runtime. |
| `TFV_USE_SANITIZERS` | `OFF` | Address/UB sanitizers for debug builds. |
| `TFV_ENABLE_DOCS` | `OFF` | Build documentation targets. |

```bash
cmake --preset default -DTFV_WITH_PYTHON=ON -DTFV_WITH_ONNX=ON
cmake --build build
```

Both optional backends compile as dependency-free stubs when OFF, and the default build links neither — so a plain build needs nothing beyond SDL2 + glm.

---

## Plug in a brain

Select the decision brain with `sim.default_brain` in `trafficflowviz.conf` or `--sim-default-brain=...`:

| Value | Backend |
| --- | --- |
| `rule` | Built-in IDM rule brain (the safe fallback). |
| `nn` | Built-in feed-forward net, configured by `sim.nn.*` (`layers` must start at 24 and end at 4). |
| `dll:<path>` | A shared library implementing the C ABI in [`include/agents/tfv_brain.h`](include/agents/tfv_brain.h). |
| `python:<module>[:func]` | A Python module exposing `decide_batch(obs)` (requires `-DTFV_WITH_PYTHON=ON`). |
| `onnx:<path>` | A model with one float32 `[N,24]` input and one `[N,K]` output (requires `-DTFV_WITH_ONNX=ON`). |

Any unknown kind or failed load **falls back to `rule`** (logged) — the simulation never crashes on a bad model. The full observation/action contract, the per-backend authoring recipes, and reference plugins (`examples/brains/`) are documented in [`docs/ONBOARDING.md`](docs/ONBOARDING.md).

---

## Observe & export

- **Vehicle Inspector** (View → Vehicle Inspector): click a vehicle to see its brain, state, held action, full 24-channel observation, and perception sectors.
- **Perception Overlay** (View → Perception Overlay): draws the selected vehicle's sensing sectors and locked neighbours in the scene.
- **Heatmap** (`H`): per-segment congestion.
- **Export**: File → Export Trajectory in the GUI, or `tfv_headless`, writes `manifest.json` + `trajectory.csv` + `metrics.csv`. The manifest captures the seed, config, brain weights hash, and a final-state digest, so a run can be replayed on the same build and confirmed byte-for-byte.

---

## Determinism & testing

The reproducibility tier is **statistical / same-build**: identical seed + config + data + binary reproduce the same `final_state_digest`. The build uses `-O3 -march=native`, so the pinned hex digests reproduce on the *same* compiler/flags/CPU — a mismatch across machines is expected and is not necessarily a regression.

`tests/golden_trajectory.cpp` is a standalone (Catch2-free) harness, registered as the `golden_trajectory` ctest case. It builds scenarios programmatically and prints `PASS: golden_trajectory (15 gates)`, pinning **7 digests**: single-lane, multi-lane overtake, NN, the C-ABI vtable adapter, the exported-trajectory CSV, the lane-aware cross-segment leader, and the per-vehicle NN bank. Behaviour-neutral changes are proven digest-identical; behavioural changes are re-pinned with a guarding gate.

```bash
ctest --test-dir build --output-on-failure
./build/bin/tfv_golden_test --print-hash      # prints the live digests (for re-pinning)
```

---

## Documentation

- [`docs/ONBOARDING.md`](docs/ONBOARDING.md) — how to build, run, configure experiments, plug in a brain, and export/reproduce data.
- [`docs/agent-simulation-design.md`](docs/agent-simulation-design.md) — the architecture and phased design.

---

## Project layout

```
include/, src/
  core/         Engine, Simulation (the two-phase update), World, RoadNetwork, Configuration
  agents/       IBrain seam, RuleBasedBrain (IDM), NNBrain, ActionValidator,
                Vtable/Dll/Python/Onnx brains, tfv_brain.h (C ABI)
  perception/   uniform-grid spatial index + sector sensing
  control/      central traffic-light controller
  rendering/    SDL renderer, SceneRenderer, and the LayerStack (Simulation/Heatmap/
                DebugPerception/ImGui layers)
  io/           RunManifest + trajectory/metrics CSV exporters
  tools/        tfv_headless batch runner
examples/brains/  reference C, Python, and ONNX plugins
tests/            golden_trajectory determinism test
docs/             design + onboarding
```

---

## Roadmap

The agent-based simulation core (Phases 0–7) and the pluggable brain backends are implemented. Not yet built (each needs an SDK/platform outside the default toolchain):

- ONNX **CUDA/CoreML** execution providers (currently CPU only).
- **Windows** path handling for the `dll:`/`onnx:` loaders.
- **Parquet/Arrow** trajectory export (CSV is shipped).
- Live-feed ingest and the alert-manager update hook are present as stubs but not yet wired.

---

## License & citation

- Intended license: **MIT** — but **no `LICENSE` file is committed yet**; add one before any external use.
- Vendored dependencies keep their own licenses (ImGui — MIT; SDL2 — zlib).
- If you use TFV in academic work, please cite:

```bibtex
@software{TFV,
  author = {Senobari, Alireza and contributors},
  title  = {TrafficFlowViz -- An Agent-Based Traffic Microsimulation},
  url    = {https://github.com/Alireza0772/trafficflowviz}
}
```

---

## Architecture (short)

```
 LiveFeed (stub) ─┐
                  ▼
   ┌──────────────────────────────────────────────┐      rule · nn · dll:
   │ Simulation  (fixed timestep, seeded)          │      python: · onnx:
   │   Phase A   sense → Observation → IBrain ──────┼────▶ one IBrain seam
   │             → ActionValidator                  │      (+ C-ABI vtable)
   │   Phase B0  conflict-free lane-change commit   │
   │   Phase B   integrate · stop-lines · hand-off  │
   └───────────────┬──────────────────────────────-┘
       snapshot()  │  (thread-safe copy for rendering / export)
                   ▼
   LayerStack:  Simulation → Heatmap → DebugPerception → ImGui (Inspector)
```

The renderer only ever reads an immutable `snapshot()`, never the live state — the same boundary the CSV/manifest exporters use, which is why export is behaviour-neutral.
