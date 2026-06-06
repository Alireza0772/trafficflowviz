# TrafficFlowViz — Agent-Based Simulation: Architecture & Design

> **Status:** Proposed design (pre-implementation). For review.
> **Supersedes the simulation core of** `ARCHITECTURE.md` (the rendering/UI/config/logging shell is retained).
> **Audience:** the maintainer (PhD research instrument) + future contributors.

This document describes the re-architecture of TrafficFlowViz from a *passive advection visualizer*
into an *agent-based traffic microsimulation* in which every vehicle and the traffic-light network
are autonomous decision-makers ("brains") that a researcher can author and fine-tune in
C++, Python, ONNX, or a DLL — without editing the engine.

---

## 0. Decisions locked for this design

| Decision | Choice | Consequence |
|---|---|---|
| Brain backends (first-class) | Built-in **C++ rule brain** + **configurable feed-forward net** (any depth/width; linear/regression as a degenerate case); **ONNX Runtime**; **embedded Python (pybind11)**; **C-ABI DLL** | Direct `libtensorflow` is a **non-goal**; TF/PyTorch arrive via `tf2onnx`. All backends gated behind CMake options so the core builds bare. |
| Reproducibility tier | **Statistical** | Same `{seeds + spawns + weights + scenario}` reproduce **within tolerance**, not bit-for-bit. We still build the cheap logical-determinism substrate (fixed timestep, seeded streams, deterministic ordering) and a manifest; we do **not** impose heavy FP flags or force deterministic ML kernels. A **bit-exact mode** is left as an opt-in build flag for later (see §8.4). |
| Migration | **Strangler-fig** | `World` stands up behind the existing `Simulation` facade; the build never regresses (see §12). |
| Core data layout | **Data-oriented SoA / ECS-lite**, hand-rolled (not EnTT) | Stable dense `EntityId`; flat component arrays; integer handles over pointers. |
| Scale target (initial) | **thousands** of vehicles | Architecture admits 10k+ later; SIMD MLP kernels / sub-interpreters deferred. |

**Defaults accepted (2026-06-06):** action space, decision rate, perception parameters, scenario format, and
observation layout are now locked per §13. Brain architecture is arbitrary/config-driven, and perception
ranges/angles are **fully configurable** (config keys, not constants).

---

## 1. Goals & non-goals

**Goals**
1. **Pluggable vehicle brains** emitting a discrete+continuous action set (accelerate, decelerate, turn L/R, go straight, change lane L/R, set signal/brake/flash lights).
2. A **single central traffic-light brain** with fixed weights commanding every intersection.
3. A **road system** of lanes, intersections, traffic lights, and signs that constrain decisions.
4. **Perception**: vehicles sense neighbors by range + sector (front/rear/left/right) and **read other vehicles' light states**, plus signs and the signal ahead.
5. **Expressive rendering** on the retained `LayerStack`: zoom-adaptive vehicle representation + rendered light systems + observability overlays.
6. **Scientist ergonomics**: author/tune a brain in Python/ONNX/DLL; define an experiment in config; get reproducible (statistical) runs and exportable data.

**Non-goals (for the first iteration)**
- Bit-exact cross-machine reproducibility (statistical tier chosen).
- Direct `libtensorflow` linkage.
- Occlusion modeling in perception (range+sector visibility only, documented simplification).
- Live network ingest (`LiveFeed` stays inert), 100k-vehicle scale, multi-GPU, Vulkan/Metal.

---

## 2. The paradigm shift

**Today (open loop):** `CSV positions → advect along segment → draw`. Data dictates motion;
`Vehicle::acc` is never integrated; routing is unseeded `rand()`; `dt` is raw wall-clock.

**Target (closed loop):** a fixed-timestep tick:

```
            ┌──────────── frozen world state @ tick N ────────────┐
            ▼                                                      │
  SENSE            DECIDE                ACT           INTEGRATE    │
  perception   lights: 1 central     validate &      acc→v→s,      │ → world @ N+1
  builds an    fixed brain, then     clamp action     lane change,  │   + immutable
  Observation  vehicles: batched     to physics/      turn via      │   double-buffered
  per agent    per brain-kind        gaps/signs       seeded RNG     │   snapshot
            └──────────────── RESOLVE: queues, vehicleCount, stats ──┘
```

Two-phase **sense-then-act** (sense reads only tick N; all actions commit to N+1) makes the
result independent of agent visitation order — a correctness *and* reproducibility property.

---

## 3. Domain model

Hand-rolled structure-of-arrays component pools indexed by a dense `EntityId`. Lookups via
`unordered_map`; **iteration always over a sorted `EntityId` vector** (never hash order).

```cpp
// include/sim/EntityId.hpp
struct EntityId { uint32_t index; uint32_t generation; };   // dense, recyclable, stable ordering
```

### 3.1 Vehicle (was `TrafficEntity.hpp::Vehicle`)

| Field group | Fields | Note |
|---|---|---|
| Identity/physical | `id, vehicleClass, length, width` | kept/extended |
| **Kinematics (lane-longitudinal)** | `segmentId, laneIndex, s (m along segment), v (m/s), a (m/s²)` | `a` is **finally integrated** |
| **World pose (authoritative)** | `worldPos: vec2, heading: float` | computed in sim, **not** recomputed in renderer |
| Intent | `targetLane, nextTurn` | from brain action + routing |
| **Signal state (observable)** | `lightBits: u8` = {BRAKE, LEFT, RIGHT, HAZARD, HIGH_BEAM_FLASH}, `blinkPhase: u8` | what *other* vehicles read |
| **Brain binding** | `brainKindId, weightSetId (→WeightStore), brainStateOffset, seed` | per-agent weights/scratch live outside the Vehicle |

### 3.2 Road system (extends `RoadNetwork`)

```cpp
struct Lane   { uint8_t index; TurnMask allowedTurns; uint32_t nextLaneByNode; };
struct Segment{ uint32_t id, fromNode, toNode; float length_m, speedLimit; vec2 dir;
                std::vector<Lane> lanes; std::vector<SignId> signs; };
struct Node   { uint32_t id; vec2 pos; std::vector<uint32_t> incoming, outgoing;
                uint32_t intersectionId; };   // which Intersection controls it
struct TrafficSign { uint32_t id; SignType type; float value; uint32_t segmentId; LaneMask lanes; float pos_m; };
```

### 3.3 Intersections & lights (commanded centrally — §5)

```cpp
enum class LightColor { Red, Yellow, Green };
struct TrafficLight { uint32_t id, nodeId; LaneMask governs; LightColor color; uint16_t countdown; };
struct Intersection { uint32_t nodeId; std::vector<PhasePlan> phases; uint16_t currentPhase, timeInPhase; };
// NOTE: TrafficLight has NO decide() path — centralization is enforced by construction.
```

### 3.4 The decision triad (new)

```cpp
// include/agents/Observation.hpp  — fixed length so every brain is interchangeable
using Observation = std::array<float, OBS_LEN>;   // OBS_LEN = 24 (draft, see §6.3)

// include/agents/Action.hpp  — a REQUEST, not a guarantee (validated in ACT phase)
struct Action {
    float   accel;            // m/s² target, clamped to vehicle profile
    int8_t  laneChange;       // -1 left, 0 none, +1 right
    int8_t  turn;             // 0 straight, -1 left, +1 right, 2 U-turn
    uint8_t lightCmd;         // BRAKE|LEFT|RIGHT|HAZARD|FLASH bits
};
```

### 3.5 Snapshot (replaces per-frame map copy)

```cpp
struct SimSnapshot {                       // immutable, double-buffered, render-ready
    uint64_t tick; double time;
    std::vector<VehicleView> vehicles;     // worldPos, heading, v, lightBits, class
    std::vector<LightView>   lights;
    SegmentStatsView         stats;
};
```

---

## 4. The Brain interface (the heart of the design)

### 4.1 One contract, two faces

A **stable, versioned C-ABI vtable** is the canonical boundary (it survives DLL / Python / ONNX
crossings of compiler ABIs and refuses to load on mismatch). A thin C++ `IBrain` wraps it so
in-process C++ brains take a fast path.

```c
/* include/agents/tfv_brain.h  — pure C ABI */
#define TFV_BRAIN_ABI 1
typedef struct {
    uint32_t abiVersion;     /* must == TFV_BRAIN_ABI                       */
    uint32_t obsLayoutId;    /* HARD GATE: must match engine's Observation  */
    uint32_t actLayoutId;    /* HARD GATE: must match engine's Action       */
    void*  (*create)(const char* configJson);
    void   (*destroy)(void* self);
    /* MANDATORY batched inference: N agents, contiguous SoA in/out */
    void   (*decide_batch)(void* self, const float* obs, int n, int obsLen,
                                       struct Action* out);
    void   (*reset)(void* self, uint64_t seed);
} TfvBrainVTable;
/* every backend exports: const TfvBrainVTable* tfv_brain_entry(void); */
```

```cpp
// include/agents/Brain.hpp  — engine-side wrapper
class IBrain {
public:
    virtual ~IBrain() = default;
    virtual void decideBatch(const Observation* obs, int n, Action* out) = 0;
    virtual void reset(uint64_t seed) = 0;
    virtual std::string weightsHash() const = 0;   // → reproducibility manifest
};
```

**Batching is mandatory:** the engine groups agents by `brainKindId`, fills one contiguous
observation arena per kind per tick, and makes **one** `decideBatch` call. This amortizes the
Python GIL (acquired once per tick) and FFI/inference overhead — the lever that makes Python/ONNX
viable at scale.

### 4.2 Backends

| Backend | File | Mechanism |
|---|---|---|
| **RuleBrain** | `agents/RuleBasedBrain.cpp` | IDM longitudinal + MOBIL lane-change; obeys signs/lights. Default + correctness **oracle**. |
| **NeuralBrain (configurable)** | `agents/NeuralBrain.cpp` | A built-in feed-forward net of **arbitrary architecture** (layer count/widths from config — "3×3×3" is just one example; a single layer = linear/logistic regression) with **per-vehicle weights** + optional seeded **connectivity mask** + optional per-synapse **integer delays** ("synapse lengths"); allocation-free forward pass. Reads the full Observation; output mapped to `Action`. Heavier or pre-trained models go through ONNX/Python instead. |
| **PythonBrain** | `agents/backends/PythonBrain.cpp` | pybind11; scientist implements `decide_batch(obs: np.ndarray) -> actions`; **zero-copy NumPy**, GIL once/tick. |
| **OnnxBrain** | `agents/backends/OnnxBrain.cpp` | ONNX Runtime session; `Observation`→tensor→`Action`. TF/PyTorch via `tf2onnx`. |
| **DllBrain** | `agents/backends/DllBrain.cpp` | `dlopen`/`LoadLibrary` → `tfv_brain_entry()`. |

A `BrainRegistry` maps a scenario-config string (`"rule" | "tinynet" | "python:mod.Class" | "onnx:path" | "dll:path"`)
to an `IBrain` and assigns per-vehicle weights/seeds.

### 4.3 The built-in NeuralBrain (configurable architecture)

> The "3×3×3" was only an example. The architecture is a **free, config-driven parameter** — any depth/width,
> or a degenerate single layer (= linear/logistic regression). Heavier or pre-trained models go through the
> ONNX or Python backends instead; this built-in brain exists for fast, dependency-free, in-process nets.
> **The engine never needs to know the architecture — only the fixed Observation→Action contract (§4.1, §6.3).**

- **Topology:** feed-forward net, layer sizes from config (e.g. `[Nobs, 8, 8, Nact]`, or `[Nobs, 3, 3]`, or `[Nobs, Nact]` for a linear model). Optional per-vehicle **connectivity mask** zeroes a seeded subset of edges ("different wiring per brain"); optional per-edge **integer delay** `d ∈ {0..D}` reads the source neuron's value from `d` ticks ago via a small ring buffer in `BrainState` — the literal "different synapse lengths."
- **Input:** the full normalized Observation (§6.3) feeds the input layer directly — so changing the net's size never changes the observation contract. (An optional projection is available if you want fewer inputs.)
- **Output:** the output layer maps to the `Action` fields `(accel, laneChange, turn)`; light commands derive from a small rule overlay (brake when decelerating hard, indicator when changing lane).
- **Weights:** generated per vehicle from `brainInit(vehicleId)`; stored as a blob in `WeightStore` and hashed for the manifest.

### 4.4 Action is a request

Every brain output passes a deterministic **validator/integrator** before it affects the world: it clamps
`accel` to the vehicle profile, rejects illegal lane changes/turns (lane connectivity, gaps), and enforces
sign/signal compliance. It exposes a **per-agent violation count** surfaced in the UI — so you can *see*
when and how often a learned brain is being corrected (safety + observability).

---

## 5. Central traffic-light controller

```cpp
// include/control/LightController.hpp
class LightController {                  // exactly ONE instance, fixed weights
    IBrain* brain;                       // same contract; intersection-level obs/act
public:
    void step(const WorldView& N, std::vector<PhaseCommand>& out);   // iterate sorted node ids
};
```

- One fixed-weight brain observes the whole intersection set and emits a phase command per intersection,
  **once per tick before vehicle decisions**.
- A deterministic **safety governor** wraps the output: it inserts amber/all-red clearance and enforces
  min-green, so even a learned controller can never produce an illegal phase flip.
- Centralization is structural: `TrafficLight` carries no `brainId` and has no decide path.

---

## 6. Perception

### 6.1 Spatial index

```cpp
// include/perception/SpatialIndex.hpp — uniform grid (deterministic, O(local))
class UniformGrid {
    void setBounds(AABB worldBounds, float cellSize);   // cellSize ≈ max sector range (~60 m)
    void rebuild(const VehicleSoA&);                    // each tick from frozen state; buckets = sorted ids
    template<class F> void forEach3x3(vec2 p, F&&) const;
};
```
A quadtree is rejected: non-deterministic split ordering, no benefit on near-uniform road density.

### 6.2 Sectors + light reading

```cpp
enum class Sector { Front, Rear, Left, Right };
struct SensedNeighbor { EntityId id; float relDist, relSpeed; uint8_t lightBits; uint16_t type; };
```
- Sector from the neighbor's **relative bearing in the observer's heading frame**
  (`Front |θ|<45°`, `Rear |θ|>135°`, else Left/Right by sign), with per-sector range caps.
  **All ranges and sector angles are config keys** (`perception.range_front/rear/side_m`,
  `perception.sector_front/rear_deg`); defaults front 60 m, rear 30 m, side 15 m, ±45°. The
  grid cell size auto-derives from the largest configured range.
- Keep the **nearest** neighbor per sector; ties broken by lower `id`.
- **Light readability is type-and-sector-gated**: brake visible from the rear, indicators from adjacent
  sectors, high-beam flash from the front. Optional per-agent `reactionMask` models inattentive drivers.

### 6.3 Observation layout (locked, OBS_LEN = 24)

| idx | channel |
|---|---|
| 0–3 | self: norm speed, accel, lane fraction, position-along-segment |
| 4–7 | segment ctx: speed-limit norm, congestion, dist-to-next-intersection, lane count |
| 8–10 | signal/sign: phase scalar, time-to-change, nearest-sign-type |
| 11–22 | 4 sectors × {relDist, relSpeed, lightBits-as-3-floats} |
| 23 | reserved (avoid reshuffling indices when extending) |

All channels normalized into `[-1,1]`/`[0,1]` with documented scales so weights are comparable across vehicles.

---

## 7. Rendering & observability (LayerStack retained)

### 7.1 Zoom-LOD vehicle representation

```
 scale < ~2     →  instanced points / particles (one batched draw)
 ~2 .. ~8       →  directional arrows (current glyph), from worldPos+heading
 > ~8           →  car-body quad + light quads: rear red (BRAKE), corner amber
                   (LEFT/RIGHT, blink by TICK not wall-clock), front white (FLASH)
```

### 7.2 New layers (additive z-indices)
- **SignalsLayer** — traffic lights + signs.
- **FlowLayer** — per-segment average-velocity arrows (between Simulation z=0 and Heatmap z=1).
- **DebugPerceptionLayer** — sector wedges + the exact neighbor each sector locked onto.
- **BrainInspectorLayer** — a clicked vehicle's live Observation vector, chosen Action, brain tag, violation count; hot-swap its brain between ticks.

### 7.3 Performance fixes folded in (current confirmed defects)
- Cache `TTF_Font` (today reopened/closed every `drawText`).
- Persistent sub-renderers + reusable vertex batches (today `RoadRenderer`/`VehicleRenderer` are rebuilt every frame; `setAntiAliasing` re-set per frame).
- One `SDL_RenderGeometry` batch per primitive class (add `drawTriangles(verts, idx)` to `Renderer`).
- Render off authoritative `worldPos/heading` → removes the `segs[v.segmentId]` index-vs-id aliasing bug.
- Implement the stubbed `onEvent()` → pan/zoom + interactive ImGui.

---

## 8. Determinism & reproducibility (statistical tier)

> **Guarantee offered:** given identical `{master seed (+derived streams), scenario/topology, brain weights,
> fixed timestep}`, runs reproduce **statistically / within tolerance**. We do **not** promise bit-exact
> replay. Rationale: the chosen tier keeps FP and ML-backend discipline light. The cheap logical-determinism
> substrate below is still built, because it removes *gratuitous* nondeterminism and is a prerequisite for
> clean experiments — and it leaves a short path to a bit-exact mode later (§8.4).

### 8.1 Why today is non-reproducible (must fix regardless of tier)
- Raw wall-clock `dt` (`Engine.cpp:176`) → trajectory depends on frame rate.
- `unordered_map` iteration order (`Simulation.cpp:82,142`) → varies across runs/allocators/std-libs.
- Unseeded global `rand()` (`Simulation.cpp:111`) → process-global, unrecorded.

### 8.2 The substrate we build
1. **Fixed-timestep accumulator** (reads `Configuration::getSimulationTimeStep()`, currently ignored). Brains see only the integer tick + `DT`. Clamp `MAX_FRAME_DT` (~0.25 s) to avoid the spiral of death.
2. **Named seeded RNG streams keyed by stable id** via SplitMix64/PCG: `spawn / world / structural / brainInit(id) / runtime(id,tick) / lightBrain`. **Per-id keying** means adding/removing a vehicle never perturbs another agent's stream — the precondition for single-variable A/B sweeps. Global `rand()` is banned (CI grep).
3. **Deterministic ordering**: iterate a sorted `EntityId` vector everywhere; tie-break neighbor selection by id; `unordered_map` for lookup only.
4. **Two-phase sense/act**: sense reads tick N only; all actions commit to N+1 → update order is irrelevant.

### 8.3 Reproducibility artifacts
- **RunManifest / RunBundle** (content-addressed): stores the resolved config, **every weight blob** (incl. RNG-generated MLP weights — replay *loads* them, never regenerates), pinned library versions, engine git SHA, and a `determinism_class` field.
- **TimeSeriesExporter**: per-tick agent/segment metrics → **CSV/Parquet** (positions, speeds, actions, light states, queue lengths) for pandas/R, using the `LoggingManager` async SPSC pattern.
- **Divergence metric** (not a hard CI gate at this tier): a run-twice comparison reports the first divergent tick + an L2 state-distance curve, so "reproducible within tolerance" is quantified, not asserted.

### 8.4 Upgrade path to bit-exact (deferred, opt-in)
A future `TFV_DETERMINISTIC` build mode would add: `-ffp-contract=off`, no fast-math, pinned ISA (no `-march=native` for archived runs), fixed-order reductions, a fixed-order C++ path for the 3×3×3 net, and single-thread deterministic ONNX kernels — plus a per-tick state-hash run-twice-diff CI gate. The architecture above is already compatible; this is mostly build flags + ordering discipline.

---

## 9. Configuration & experiment model

The layered `Configuration` (defaults<env<file<CLI, `saveToFile`) is the home for experiment settings:
`master_seed`, `perf.physics_hz` (50), `perf.decision_hz` (10), brain backend + weight paths, NN
topology/connectivity params, the `perception.*` range/angle keys (§13.4), and output dirs. A YAML
`ScenarioConfig` (one file fully describing an experiment) is persisted verbatim into the RunManifest.

---

## 10. Module / file layout

```
include/sim/        EntityId, World, Rng, SimClock
include/agents/     tfv_brain.h, Brain.hpp, Observation.hpp, Action.hpp, BrainRegistry
src/agents/         RuleBasedBrain, NeuralBrain(3x3x3)
src/agents/backends/ PythonBrain, OnnxBrain, DllBrain
include/control/    LightController            (+ src/)
include/world/      TrafficLight, Intersection, TrafficSign
include/perception/ PerceptionSystem, SpatialIndex   (+ src/)
include/experiment/ ScenarioConfig, RunManifest (+ src/)
include/io/         TimeSeriesExporter         (+ src/)
tests/              determinism/replay + golden-trajectory (RuleBrain)
```

---

## 11. Delta map (vs current code)

| Current piece | Change | What happens |
|---|---|---|
| `TrafficEntity.hpp` | **REPLACE** (L) | Vehicle→agent record; add Lane/Intersection/TrafficLight/Sign/Action/Observation. Dependency root. |
| `Simulation.cpp/.hpp` | **REPLACE** (L) | `update()`→fixed-step sense/decide/act/integrate/resolve; seeded RNG; fix vehicleCount-on-handoff. |
| `RoadNetwork.cpp/.hpp` | **EXTEND** (M) | Fix `route()`; add lanes, intersections, lights, signs; ID-based lookup. |
| `CSVLoader.cpp` | **EXTEND** (M) | Header-named schema loader; load scenarios/lights/signs/per-vehicle brain+weights+seed. |
| `Engine.cpp` | **EXTEND** (M) | **Fix double-init**; fixed-timestep accumulator; own new subsystems. |
| `SceneRenderer.cpp` | **EXTEND** (M) | Zoom-LOD + light glyphs; persistent batched renderers; fix `segs[v.segmentId]`. |
| Layer `onEvent()` | **REPLACE** (M) | Implement pan/zoom + interactive ImGui + brain hot-swap. |
| `Renderer.hpp`/SDLRenderer | **EXTEND** (S) | Batched triangles/points; cache font. |
| `Configuration` | **EXTEND** (S) | New keys (seed, timestep, backend, weights, perception, output). |
| `bindings/py_module.cpp` | **REPLACE** (M) | Fix broken/inverted bindings; expose Brain/Observation/Action + stepping + manifest. |
| `LayerStack`/Event/`LoggingManager`/`RecordingManager`/`AlertManager` | **KEEP** (S) | Backbone retained; alerts become meaningful with real speeds. |
| `LiveFeed` | **KEEP** (S) | Inert; untouched. |
| Global `rand()` / RNG | **REPLACE** (M) | Per-stream seeded `mt19937_64` keyed by id. |

---

## 12. Migration plan (strangler-fig, build always green)

- **Phase 0 — Stabilize (small, high value).** Fix double-init (`Engine.cpp:70`&`:166`); fixed-timestep accumulator; ban `rand()`→seeded stream; vehicleCount-on-handoff; implement `onEvent` (pan/zoom + ImGui); fix `segs[]` aliasing; cache font. *Result: the current visualizer works correctly and is navigable.*
- **Phase 1 — World behind the facade.** Introduce `World` (SoA) keeping `Simulation::snapshot()/getSegmentStats()/...` as shims. Integrate `acc`. Add `EntityId` ordering + two-phase loop.
- **Phase 2 — RuleBrain baseline.** Ship `IBrain` + `RuleBrain` (IDM+MOBIL). **Golden-trajectory test** locks behavior. *This is the first real agent sim and the correctness oracle.*
- **Phase 3 — Road semantics.** Multi-lane geometry, turn connectivity, signs; routing via fixed BFS (not random).
- **Phase 4 — Central lights.** `LightController` + safety governor; vehicles obey signals.
- **Phase 5 — Perception light-reading.** Spatial grid, sectors, observation vector, read neighbor lights.
- **Phase 6 — Brains.** `TinyNetBrain` (3×3×3) → `PythonBrain` → `OnnxBrain` → `DllBrain`, each gated by a CMake option.
- **Phase 7 — Observability & repro.** RunManifest, TimeSeriesExporter (Parquet/CSV), Debug/Inspector layers, divergence metric.

Each phase is independently shippable; the visualizer never regresses.

---

## 13. Resolved decisions (defaults accepted 2026-06-06)

1. **Brain architecture** — arbitrary / config-driven (any depth/width, or a regression/linear model); heavier or pre-trained models use the ONNX or Python backends. The engine fixes only the Observation→Action contract, never the architecture.
2. **Action space** — `{accel (m/s², continuous), laneChange∈{-1,0,+1}, turn∈{straight,L,R,U}, lightCmd bits}` (§3.4), plus 2 reserved fields for later additions (e.g. horn, variable target-speed).
3. **Decision rate** — physics integrates at a fixed **50 Hz**; brains decide at **10 Hz** (every 5th tick), holding the last action between decisions. Both are config keys (`perf.physics_hz`, `perf.decision_hz`).
4. **Perception — fully configurable.** All per-sector ranges *and* sector angles are config keys, not constants. Defaults: front 60 m, rear 30 m, sides 15 m; front/rear sectors ±45° (sides fill the remainder). Keys: `perception.range_front_m / range_rear_m / range_side_m`, `perception.sector_front_deg / sector_rear_deg`. The spatial-grid cell size auto-derives from the largest range.
5. **Scenario file format** — a **YAML** scenario/experiment manifest fully defines a run (master seed, timestep, brain assignments + weights/seeds, light program, signs, output paths); **CSV** (header-named) carries bulk vehicle/road spawn data. The YAML is persisted verbatim into the RunManifest.
6. **Observation layout** — the §6.3 layout (OBS_LEN = 24) is **locked**, including reserved slot 23; channels normalized with documented scales.

> All §13 items are settled. Implementation can proceed on the Phase 0→7 order in §12.

---

## 14. How this serves the three project goals

- **Flexible** — one brain contract; swap rule/NN/Python/ONNX/DLL per vehicle via config, no engine edits; scenarios are config-over-code; the model is parameterized (no magic numbers baked into the loop).
- **Verbose / observable** — live Observation/Action inspector, violation counts, per-tick Parquet/CSV export, run manifest, debug perception overlay; the async logger is retained and reused.
- **Expressive to an audience** — zoom-LOD particles↔arrows↔car glyphs, rendered brake/turn/flash light systems, signal/flow layers, and an interactive navigable UI.

---

*End of design. Review §0 (locked decisions) and §13 (open items); on approval, implementation follows the Phase 0→7 order in §12.*
