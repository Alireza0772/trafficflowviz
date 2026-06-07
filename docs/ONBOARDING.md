# TrafficFlowViz — Onboarding & Usage

## 1. What it is

TrafficFlowViz is an agent-based traffic **microsimulation**: every vehicle runs its own pluggable *brain* in a continuous **sense → decide → act** loop, mapping a fixed 24-channel `Observation` to an `Action` request that the engine validates and applies. Brains can be the built-in rule/neural backends or your own trained model loaded as a DLL, Python module, or ONNX file. Runs are **deterministic on the same build** ("statistical / same-build tier"): identical seed + config + data + binary reproduce the same `final_state_digest`.

---

## 2. Build

The default build needs **no extra dependencies**. Configure with a CMake preset, then build. Available presets: `default`, `debug`, `vcpkg`, `release`, `ci`.

```sh
cmake --preset default      # configures into ./build
cmake --build build         # builds all targets
```

Other presets configure into their own dirs (`build-debug`, `build-vcpkg`, `build-release`, `build-ci`):

```sh
cmake --preset release
cmake --build build-release
```

This produces three binaries:

| Binary             | Purpose                                                              |
| ------------------ | ------------------------------------------------------------------- |
| `trafficviz`       | Interactive GUI (SDL2 window, live visualization, inspector)        |
| `tfv_headless`     | Batch/headless runner for reproducible experiments and data export  |
| `tfv_golden_test`  | Determinism contract test (run via `ctest`, case `golden_trajectory`) |

Optional features are opt-in at configure time:

```sh
cmake --preset default -DTFV_WITH_PYTHON=ON   # enable python: brains (needs numpy)
cmake --preset default -DTFV_WITH_ONNX=ON     # enable onnx: brains (needs ONNX Runtime)
```

Run the determinism test:

```sh
ctest --test-dir build -R golden_trajectory
```

---

## 3. Run it (GUI)

```sh
./build/bin/trafficviz
```

### Camera, selection & feature controls

| Input                                   | Action                                                          |
| --------------------------------------- | --------------------------------------------------------------- |
| Left-drag                               | Pan the view                                                    |
| Left-click                              | Select nearest vehicle (~14 px); click empty road to deselect   |
| Mouse wheel                             | Zoom (cursor-anchored, clamp 0.1×..100×)                        |
| `+` / `=` / keypad `+`                  | Zoom in                                                         |
| `-` / keypad `-`                        | Zoom out                                                        |
| Arrow keys                              | Pan the view                                                    |
| `H`                                     | Toggle heatmap                                                  |
| `L`                                     | Toggle live feed                                                |
| `A`                                     | Toggle alerts                                                   |
| `R`                                     | Toggle recording                                                |
| `I`                                     | Toggle the entire ImGui interface on/off                        |
| `G`                                     | Toggle anti-aliasing                                            |
| `K`                                     | Toggle the Keybindings help window                              |
| `S`                                     | Save a screenshot (`trafficviz_<unixtime>.png`)                 |
| `Esc`                                   | Quit                                                            |

> **Note:** The `E` (Export) and `P` (Perception Overlay) letters shown in menus are display labels only — there is **no** key handler for them. Use the menu items instead (below). The `Heatmap` View-menu item is a no-op stub; toggle the heatmap with the `H` key.

Menus: **File** (Export Trajectory / Stop Export, Exit), **View** (Keybindings `K`, Vehicle Inspector `I`, Perception Overlay, Simulation Layer, Heatmap), **Recording** (Start/Stop Recording `R` → `output.mp4` @ 30 fps, Take Screenshot `S` → `screenshot.png`).

### 60-second first look

1. Launch `./build/bin/trafficviz` — the road network and moving vehicles appear.
2. **Click a vehicle** in the scene to select it.
3. Open **View → Vehicle Inspector** to read its live observation, decision, and perception sectors.
4. Open **View → Perception Overlay** to draw each vehicle's sensing in scene space.
5. Press `H` to toggle the congestion heatmap; scroll to zoom, drag to pan.

---

## 4. Configure an experiment

Configuration is a singleton applied in increasing priority: hardcoded defaults → `TFV_*` environment vars → the `.conf` file → `--key=value` CLI args (highest). The `.conf` is **auto-discovered** (no flag), loading the first that exists: `./trafficflowviz.conf`, then `<exe-dir>/trafficflowviz.conf`, then `$HOME/.trafficflowviz.conf`.

### File format (`trafficflowviz.conf`)

Plain `key=value`, one per line. Lines starting with `#` or `;` are comments; blank lines skipped. Keys and values are whitespace-trimmed; the value is everything after the **first** `=`. No sections, no quoting — all values are strings, coerced by the getters.

```conf
# trafficflowviz.conf
paths.data_dir = data
data.city_file = roads/roads_complex.csv
sim.master_seed = 42
sim.default_brain = nn
```

### Key options (defaults)

> Keys marked **(inline)** are *not* in the shipped `.conf` or hardcoded defaults; their default is the literal at the read site, but they work fine when added to the `.conf` or passed via CLI.

**data.\* — inputs**

| Key                  | Default                     | Meaning                                           |
| -------------------- | --------------------------- | ------------------------------------------------- |
| `paths.data_dir`     | `data`                      | Base directory for input files                    |
| `data.city_file`     | `roads/roads_complex.csv`   | Road-network CSV (joined onto `data_dir`)         |
| `data.vehicles_file` | `vehicles/vehicles.csv`     | Initial vehicles CSV                              |
| `data.signs_file`    | `roads/signs.csv`           | Traffic-signs CSV                                 |
| `data.icon_file`     | `icon.png`                  | Window icon (joined onto `paths.assets_dir`)      |

**perf.\* — engine timing / limits**

| Key                        | Default                | Meaning                                  |
| -------------------------- | ---------------------- | ---------------------------------------- |
| `perf.simulation_timestep` | `0.016`                | Fixed sim dt (seconds)                   |
| `perf.decision_hz`         | `10.0`                 | How often each brain re-decides (Hz)     |
| `perf.max_fps`             | `60`                   | GUI frame cap                            |
| `perf.worker_threads`      | hardware concurrency (`.conf` ships `8`) | Worker thread count    |
| `perf.max_vehicles`        | `100000`               | Vehicle cap                              |

**sim.\* — reproducibility / brain / models**

| Key                       | Default        | Meaning                                          |
| ------------------------- | -------------- | ------------------------------------------------ |
| `sim.master_seed`         | `12345`        | Master RNG seed (uint64)                         |
| `sim.default_brain`       | `rule`         | Brain backend (see §5)                           |
| `sim.idm.v0_cap`          | `13.9`         | IDM desired-speed cap (m/s)                      |
| `sim.idm.a_max`           | `1.5`          | IDM max acceleration                             |
| `sim.idm.b_comfort`       | `2.0`          | IDM comfortable deceleration                     |
| `sim.idm.b_max`           | `6.0`          | IDM max deceleration                             |
| `sim.idm.s0`              | `2.0`          | IDM minimum gap                                  |
| `sim.idm.T`               | `1.5`          | IDM safe time headway                            |
| `sim.idm.delta`           | `4.0`          | IDM acceleration exponent                        |
| `sim.nn.layers`           | `24,16,8,4` (inline) | NN layer sizes (must start 24, end 4)      |
| `sim.nn.activation`       | `tanh` (inline)      | NN hidden activation (`tanh`/`relu`/`identity`) |
| `sim.nn.lane_threshold`   | `0.5` (inline)       | NN lane-change commit threshold            |
| `sim.nn.population`       | `1` (inline)         | Heterogeneous net bank (1 = shared net)    |
| `sim.lane_width_m`        | `3.5` (inline)       | Lane width (m)                             |
| `sim.mobil.enabled`       | `1` (inline)         | MOBIL lane-change model on/off             |
| `sim.export.enabled`      | `0` (inline, GUI)    | Auto-start export for whole GUI run        |
| `sim.export.every_n`      | `1` (inline)         | Export every Nth tick (min 1)              |
| `validator.enabled`       | `1` (inline)         | Action validator on/off                    |

**perception.\* — sensor ranges/sectors (all inline)**

| Key                            | Default | Meaning                          |
| ------------------------------ | ------- | -------------------------------- |
| `perception.range_front_m`     | `60.0`  | Forward sensing range (m)        |
| `perception.range_rear_m`      | `30.0`  | Rear sensing range (m)           |
| `perception.range_side_m`      | `15.0`  | Side sensing range (m)           |
| `perception.sector_front_deg`  | `45.0`  | Front angular sector (deg)       |
| `perception.sector_rear_deg`   | `45.0`  | Rear angular sector (deg)        |
| `perception.cross_segment_leader` | `1`  | Cross-segment leader detection   |

**export.\* — headless output**

| Key             | Default                          | Meaning                                  |
| --------------- | -------------------------------- | ---------------------------------------- |
| `export.ticks`  | `600` (inline, min 1)            | Number of sim ticks to run headless      |
| `export.dir`    | `.` headless / `output` GUI      | Output directory                         |

### CLI override rule

Any arg starting with `--`, in the form `--key=value` (or `--key value`), sets a config key; every `-` in the **key** becomes `.` (the value is verbatim; underscores are **not** translated). CLI overrides win over everything.

```sh
# real working examples (binaries live in build/bin/ after a default build)
./build/bin/tfv_headless --export-ticks=1200 --export-dir=out --sim-default-brain=nn
./build/bin/trafficviz --sim-default-brain=rule --perf-decision-hz=20 --sim-master-seed=42
```

> The per-tick stride lives under `sim.export.every_n`, so its CLI form keeps the trailing underscore: `--sim-export-every_n=5`. (There is no `export.every_n`.)

`TFV_*` environment variables work for a fixed allow-list only: `TFV_DATA_DIR`, `TFV_ASSETS_DIR`, `TFV_OUTPUT_DIR`, `TFV_RENDERER`, `TFV_WINDOW_WIDTH`, `TFV_WINDOW_HEIGHT`, `TFV_DEBUG_MODE`, `TFV_MAX_VEHICLES`, `TFV_WEBSOCKET_URL`. Arbitrary keys cannot be set via env.

---

## 5. Choosing / plugging in a brain

Set `sim.default_brain` (or `--sim-default-brain=...`). Backends:

| Value                                 | Backend                                            |
| ------------------------------------- | -------------------------------------------------- |
| `rule` (or empty)                     | Built-in rule-based brain (the fallback)           |
| `nn`                                  | Built-in feed-forward net (configured by `sim.nn.*`) |
| `dll:<path>` or `dll:<path>?<config>` | Shared library implementing the C ABI              |
| `python:<module>[:func][?config]`     | Python module (`-DTFV_WITH_PYTHON=ON`)             |
| `onnx:<path>` or `onnx:<path>?<config>` | ONNX model (`-DTFV_WITH_ONNX=ON`)                |

**Fail-soft:** any unknown kind, malformed NN architecture, or failed DLL/Python/ONNX load **silently falls back to `rule`** (logged as a warning) — the sim never crashes. External brains are also hard-gated on an ABI/shape check before any decision call.

### Built-in NN config

`sim.nn.layers` (default `24,16,8,4`) is a CSV of layer sizes that **must start at 24** (observation length) and **end at 4** (action), or the brain is invalid and falls back to `rule`. `sim.nn.activation` (`tanh`/`relu`/`identity`), `sim.nn.lane_threshold` (`0.5`), and `sim.nn.population` (`1`, clamped 1..256; >1 = heterogeneous drivers by id-hash). Weights are deterministically derived from `sim.master_seed`.

### Write your own brain

Every brain maps a 24-float `Observation` to an `Action`. The `Action` is a **request** — the engine's `ActionValidator` is the sole authority that clamps acceleration, lane feasibility, and scrubs NaN/Inf. Examples live in **`examples/brains/`**.

#### Observation (24 channels)

Normalized floats; reconstruct absolutes with `OBS_SPEED_SCALE=40`, `OBS_RANGE_SCALE=60`, `OBS_ACCEL_SCALE=10`, `OBS_TIME_SCALE=10`.

| Idx | Name                  | Meaning                                                    |
| --- | --------------------- | ---------------------------------------------------------- |
| 0   | self_speed            | speed / 40                                                 |
| 1   | self_accel            | last accel / 10                                            |
| 2   | lane_fraction         | laneIndex / (laneCount−1), 0..1                            |
| 3   | position              | position along segment, 0..1                               |
| 4   | speed_limit           | effective limit / 40                                       |
| 5   | congestion            | segment congestion, 0..1                                   |
| 6   | dist_intersxn         | (1−pos)·length / 60                                        |
| 7   | lane_count            | lane count / 8                                             |
| 8   | signal_phase          | light ahead: green 0, amber .5, red 1                      |
| 9   | signal_ttc            | reserved (signal time-to-change)                           |
| 10  | sign_type             | normalized sign-type code ahead                            |
| 11  | front_gap             | bumper gap (m) / 60, clamped to 1                          |
| 12  | front_relspd          | (v_self − v_leader) / 40                                   |
| 13  | front_leader          | 1.0 if a front constraint exists, else 0                   |
| 14  | rear_dist             | rear sector rel. distance                                  |
| 15  | rear_relspd           | rear sector rel. speed                                     |
| 16  | rear_light            | rear sector light bits                                     |
| 17  | left_dist             | left sector rel. distance                                  |
| 18  | left_relspd           | left sector rel. speed                                     |
| 19  | left_light            | left sector light bits                                     |
| 20  | right_dist            | right sector rel. distance                                 |
| 21  | right_relspd          | right sector rel. speed                                    |
| 22  | right_light           | right sector light bits                                    |
| 23  | reserved              | reserved                                                   |

> Channels 8–22 (signals + world-space neighbor sectors) are filled later in development; early phases populate only the longitudinal subset (0, 1, 4, 5, 11, 12, 13) and leave the rest zero. Don't assume a model gets nonzero perception channels yet.

#### Action fields

| Field        | Type     | Meaning                                          |
| ------------ | -------- | ------------------------------------------------ |
| `accel`      | float    | target longitudinal accel (m/s²)                 |
| `laneChange` | int8     | −1 left, 0 none, +1 right                        |
| `turn`       | int8     | 0 straight, −1 left, +1 right, 2 U-turn          |
| `lightCmd`   | uint8    | light bitfield: BRAKE=1, LEFT=2, RIGHT=4, HAZARD=8, FLASH=16 |
| `reserved0`  | float    | reserved                                         |
| `reserved1`  | int32    | reserved                                         |

Unless a brain **declares** it drives lane changes, the engine's MOBIL model owns lane changes and the brain's `laneChange` is ignored.

#### Recipe — C / DLL (`examples/brains/example_brain.c`)

Build as a shared **MODULE** library and export the C symbol:

```c
int tfv_brain_create(const char* config,
                     tfv_brain_desc* outDesc,
                     const tfv_brain_vtable** outVtable,
                     void** outSelf);   // return 0 on success
```

Fill `tfv_brain_desc` with `abiVersion=1`, `obsLen=24`, `actLayout=1`, then point `outVtable` at a static vtable. `decide_batch(self, const float* obs, int n, int obsLen, tfv_action* out)` is mandatory; `destroy`, `reset(seed)`, `weights_hash`, `drives_lane_change` may be NULL. The `tfv_action` POD has 2 padding bytes — compare/serialize field-wise, never `memcmp`. Select with `sim.default_brain = dll:/abs/path/to/lib.so`.

#### Recipe — Python (`examples/brains/python/example_brain.py`, `-DTFV_WITH_PYTHON=ON`)

```python
def decide_batch(obs):           # obs: read-only float32 ndarray, shape (n, 24)
    # return ndarray shape (n, K), 1 <= K <= 6, float32
    # columns: [accel, laneChange, turn, lightCmd, reserved0, reserved1]
    ...                          # only column 0 (accel) is required
```

Must not mutate `obs` or retain it past the call. Optional hooks: `reset(seed)`, `weights_hash()`, and module attr `drives_lane_change = True`. Select with `sim.default_brain = python:example_brain` and `sim.python.path=<dir>` (or `python:example_brain?path=<dir>`); a trailing `:func` picks a non-default function name.

#### Recipe — ONNX (`examples/brains/onnx/`, `-DTFV_WITH_ONNX=ON`)

Export a model with **exactly one** float32 input `[N, 24]` (dynamic batch) and **exactly one** float32 output `[N, K]`, `1 ≤ K ≤ 6`, columns `[accel, laneChange, turn, lightCmd, reserved0, reserved1]` (only `accel` required). Anything else is rejected at load → fallback to rule. Select with `sim.default_brain = onnx:/abs/path/to/model.onnx`; optional `?drives_lane_change=1`. Keys: `sim.onnx.warmup` (default 1), `sim.onnx.drives_lane_change` (default 0). See `examples/brains/onnx/README.md` and `make_test_model.py`.

---

## 6. Observe

### Vehicle Inspector (View → Vehicle Inspector, `I` in the View menu)

With a vehicle selected it shows:

- **Header:** `Vehicle #<id>   brain: <brainKind>`.
- **State:** `segment`, `lane`, `speed` (m/s), `leader` id, `violations` count.
- **Action:** `accel`, `laneChange`, `turn`, `lights` (hex bitfield) — the currently held decision.
- **Observation (24 channels):** indexed rows with the labels from the table in §5.
- **Perception sectors:** Front / Rear / Left / Right; each prints `id`, `dist`, `relSpd`, `lights`, or `(clear)` when nothing is sensed.

If nothing is selected it prompts you to click a vehicle; if the selected vehicle despawned it shows `Vehicle #<id> not found (despawned)`.

### Perception Overlay (View → Perception Overlay)

Draws each vehicle's sensing ranges/sectors in scene space — the visual counterpart to the Inspector's textual readout. Toggle via the menu item (the `P` label is not a live key).

### Heatmap (`H`)

Toggles a congestion heatmap over the network. Toggle with the `H` key (the `Heatmap` View-menu item is a no-op stub).

The bottom status bar shows `FPS`, `Vehicles`, `Zoom`, and a red `● RECORDING` indicator when recording.

---

## 7. Export data & reproduce

Both GUI and headless paths use the **same exporters and the same digest**, so a GUI export started at tick 0 byte-matches a headless run of equal length and cadence.

### GUI export

**File → Export Trajectory** starts export (the item flips to **Stop Export**). It creates a timestamped subdir `export.dir/run_YYYYMMDD_HHMMSS/` (base default `output`) and writes `trajectory.csv` + `metrics.csv` there, sampled every `sim.export.every_n` ticks; stopping (or shutdown) also writes `manifest.json`. To auto-export the whole run, set `sim.export.enabled=1`.

### Headless export

```sh
./build/bin/tfv_headless                                           # uses trafficflowviz.conf
./build/bin/tfv_headless --export-ticks=1200 --export-dir=out --sim-default-brain=nn
```

Reads `export.ticks` (default 600), `sim.export.every_n` (default 1), `export.dir` (headless default `.`), the `data.*` files, and `perf.simulation_timestep`. It writes `manifest.json`, `trajectory.csv`, and `metrics.csv` **directly into `export.dir`** (no timestamped subdir, unlike the GUI).

### Output schemas

**`trajectory.csv`** — per-tick, per-vehicle (ascending vehicle id):

```
tick,sim_time,vehicle_id,segment_id,lane_index,position,world_x,world_y,heading,speed,light_bits,act_accel,act_lane_change,act_turn,act_light_cmd,violations
```

**`metrics.csv`** — per-tick, per-segment (ascending segment id):

```
tick,sim_time,segment_id,avg_speed,avg_density,congestion
```

**`manifest.json`** captures everything needed to replay: `schema_version`, `master_seed`, `brain_kind`, `brain_weights_hash`, `city_file`/`vehicles_file`/`signs_file`, `vehicle_count`, `decision_hz`, `fixed_dt`, `total_ticks`, `export_every_n`, `git_sha`, `wall_clock_utc`, `final_state_digest` (`0x%016llx`), and a `config` object holding all sorted `sim.*` + `perf.*` keys. `wall_clock_utc` is the only field never hashed.

### Reproduce a run

On the **same binary**, recreate the run from a manifest:

1. Set `sim.master_seed` to the manifest's `master_seed`.
2. Replay every `sim.*` / `perf.*` key from the manifest `config` object, plus `sim.default_brain` (matching `brain_kind`, weights verified by `brain_weights_hash`), `perf.simulation_timestep` = `fixed_dt`, `perf.decision_hz`, `sim.export.every_n` = `export_every_n`, and `export.ticks` = `total_ticks`.
3. Use the same input data files.

```sh
./build/bin/tfv_headless --sim-master-seed=42 --sim-default-brain=nn --export-ticks=1200 --export-dir=out
```

A matching `final_state_digest` proves a byte-identical replay (same-build tier).

---

## 8. Reproducibility & determinism

The simulation advances on a **fixed timestep** with **seeded** weight/RNG streams and **no RNG in the per-step path**, so the same build produces identical results from identical inputs. `final_state_digest` is an FNV-1a hash over the final snapshot only — per vehicle, in id order, mixing `id`, `segmentId`, `laneIndex`, `position` and `speed` (rounded, so it tolerates tiny FP noise). The **golden test** (`ctest -R golden_trajectory`) is the determinism contract: it pins both the state digest and the byte-exact trajectory CSV digest, and asserts that exporting leaves the sim state unchanged.

---

## 9. Troubleshooting

- **Empty network / no vehicles:** data paths are wrong. `data.city_file` / `data.vehicles_file` / `data.signs_file` are joined onto `paths.data_dir`, resolved relative to the working/exe dirs. The auto-discovered `.conf` depends on the working directory — running from a different cwd can pick a different (or no) config.
- **My brain isn't running (looks like plain IDM):** it fell back to `rule`. Check the log for a `LOG_WARN` — common causes: unknown `sim.default_brain` kind, NN `layers` not starting at 24 / ending at 4, a failed `dll:`/`python:`/`onnx:` load, or ABI/tensor-shape mismatch.
- **Headless can't find inputs:** `tfv_headless` needs the data dir reachable from its working directory (or set `paths.data_dir` / `TFV_DATA_DIR`). Its `export.dir` defaults to `.` — files land in the current directory unless you pass `--export-dir=...`.
- **`final_state_digest` differs across machines:** expected. Determinism is **same-build only**; a different compiler, flags, platform, or library version can change low bits. Reproduce on the same binary.
- **`E` / `P` keys do nothing:** by design — those are menu labels, not key handlers. Use **File → Export Trajectory** and **View → Perception Overlay**.
