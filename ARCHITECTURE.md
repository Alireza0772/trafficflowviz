# Full Architecture Specification

> The remainder of this document replaces the old `ARCHITECTURE.md` and expands every subsystem in depth.

---

## 1. Design Goals

1. **Real‑time first**: 60 FPS @ 1080p on laptop GPUs.
2. **Deterministic replay**: identical outputs for identical seeds/logs.
3. **Research‑friendly**: painless Python import; rewind/step semantics.
4. **Portable & future‑proof**: abstracted renderers; C ABI plugins.
5. **Observable**: every message/metric traceable via logs or Prometheus.

## 2. High‑Level Component Diagram

```
                  +-----------------+
                  |  UI (ImGui)     |
                  +--------+--------+
                           |
+---------+  events  +-----v-----+  snap  +-------------+
| Window  +<-------->+ LayerStack+<------>+SceneRenderer|
+---------+          +-----+-----+        +-------------+
                           | update/render
                           v
                     +-----+----+
                     |Simulation|  owns  ┌──────────────┐
                     +-----+----+--------> RoadNetwork  |
                           |             └──────────────┘
                           |             ┌──────────────┐
                           +-------------> Vehicles     |
                           |   snapshot  └──────────────┘
           ingest (async)  |
+-----------+  RingQueue  +v+
| LiveFeed  +-------------> |   (lock‑free)
+-----------+              +-
```

## 3. Threading Model

* **Main thread** – window events, LayerStack update/render.
* **Simulation thread** – fixed‑step `Simulation::update()`; owns mutable state; publishes snapshots via `std::atomic<shared_ptr<const State>>` double‑buffer.
* **Ingest thread** – decodes WebSocket/Proto messages and writes into a ring buffer consumed by Simulation.
* **Logger thread** – drains a bounded MPSC queue (`LoggingManager`).

Locks are avoided during rendering; the renderer consumes an immutable vehicle snapshot.

## 4. Data Ingestion Pipeline

1. **Transport:** Compile‑time choice between Kafka consumer or vanilla WebSocket.
2. **Decoder:** JSON → Struct or Proto via `protozero`.
3. **Queue:** Lock‑free ring with back‑pressure (drops oldest on overflow).
4. **Merger:** Simulation thread merges updates, resolves duplicates and applies clock skew heuristic.

## 5. Simulation Core

* **RoadNetwork** – graph of segments & intersections; supports contraction hierarchy for path‑finding.
* **Vehicle** – id, pos (segment + offset), vel, heading.
* **SegmentStatistics** – ring‑buffer of last N speed samples; congestion level (0‑1).
* **Fixed‑step loop:** `for t in range(0, dt, step)`; ensures deterministic updates independent of FPS.
* **Alerts:** simple rule engine evaluating segment stats each stat window.

## 6. Rendering Pipeline

* **SceneRenderer** – records draw commands into an ImGui draw list (for SDL) or into MTL command buffer (Metal).
* **HeatmapRenderer** – screen‑space pass writing to a colour ramp texture.
* **Camera** – orthographic; supports pan/zoom & pixel‑perfect snapping.
* **Anti‑aliasing:** MSAA x4 optional per‑renderer.

## 7. Python Binding Internals

* **scikit‑build‑core** drives CMake build → wheel.
* Uses **pybind11** with custom type‑casters for `glm::vec` & `std::unordered_map`.
* Zero‑copy NumPy views for vehicle arrays via `py::array_t` with capsule deleter.
* GIL released during long C++ calls.

## 8. Plugin System

* C ABI: `tfv_plugin_create(const tfv::HostAPI*)` returns a `tfv_plugin*` vtable.
* Plugins loaded via `dlopen`/`LoadLibrary` into isolated thread; watchdog restarts on panic.
* Host exposes subset of API (road lookups, vehicle snapshot, logger).

## 9. Deployment Topologies

1. **Laptop demo** – all local; ingest from CSV; Metal renderer.
2. **Research cluster** – ingest pod (Kafka), TFV headless pods producing snapshots to Redis, web front‑end streams WebGL overlays.
3. **Control room** – TFV wall display; alerts webhook → Grafana.

## 10. Performance Guidelines

* Avoid allocation in render loop – reuse vertex buffers.
* Use SIMD (glm::vec) for vector math; compile with `-ffast-math` (non‑LR testing builds).
* Batch vehicles by segment to reduce state changes.
* Profile with Tracy (`./scripts/run_tracy.sh`).

## 11. Security & Compliance

* TLS 1.3 everywhere; cert pinning for ingest feeds.
* All PII stripped at ingest boundary; only hashed IDs leave cluster.
* Follows GDPR data‑minimisation principle; optional on‑prem deployment.

## 12. Mapping to Requirements

| Requirement                | Section | Implementation Notes                   |
| -------------------------- | ------- | -------------------------------------- |
| **FR‑01** Live Ingest      | §4      | Kafka/NATS consumer, ring buffer merge |
| **FR‑04** Map Visuals      | §6      | SDL/Metal renderer, heatmap layer      |
| **FR‑07** Python API       | §7      | pybind11 wheel, Gym adapter            |
| **NFR‑01** Performance     | §10     | 60 FPS, batching, SIMD                 |
| **NFR‑05** Maintainability | §7, §8  | plugin ABI, 85 % coverage              |

## 13. Future Work

* Vulkan & DirectX12 renderers via RenderGraph.
* Multi‑GPU simulation decomposition (city blocks split).
* WASM/WebGL build for browser‑only visualisation.
* Edge ingest with eBPF sensors pushing Proto via QUIC.

---