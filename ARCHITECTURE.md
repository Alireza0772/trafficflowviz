# TrafficFlowViz – System Architecture

## 1. Overview
TrafficFlowViz is a modular, high-performance C++ application for real-time and historical traffic flow visualization and analytics. The architecture is designed for extensibility, cross-platform support, and integration with live and historical data sources. It is inspired by the Walnut engine, with a layered approach for rendering, UI, and simulation.

---

## 2. High-Level System Diagram & Data Flow

- **Data Sources:**
  - Live feeds (REST/WebSocket, JSON/Protobuf) [FR-01]
  - Historical logs (CSV/Parquet) [FR-02]
  - Sensor fusion (loop, camera, GPS) [FR-03]
- **Core Engine:**
  - Manages simulation, road network, and traffic entities
  - Coordinates all subsystems and the main loop
- **Layered Rendering System:**
  - SimulationLayer: core visualization, camera controls [FR-04]
  - HeatmapLayer: congestion overlays [FR-05]
  - ImGuiLayer: dockable UI, controls, analytics [FR-09]
- **Extensibility:**
  - Python scripting API [FR-07]
  - Plugin system for DSP/ML modules [FR-08]
  - Self-driving traffic entities (planned)
- **Output:**
  - Real-time visualization, alerts, analytics, export (video/PNG, GeoJSON)

---

## 3. Core Components & Responsibilities

### Engine
- Central coordinator: initializes window, renderer, simulation, and layers
- Handles main loop (update, render, event dispatch)
- Manages plugin and scripting integration

### Layer System
- Each layer encapsulates a feature (simulation, heatmap, UI, etc.)
- Layers are managed by LayerStack (z-order, add/remove, event propagation)
- Layers can be toggled, reordered, or extended at runtime

#### Key Layers
- **SimulationLayer:**
  - Renders vehicles, roads, and self-driving entities
  - Handles camera and user interaction
- **HeatmapLayer:**
  - Visualizes congestion and speed ratios
  - Supports tooltips and overlays
- **ImGuiLayer:**
  - Provides dockable, resizable UI
  - Hosts analytics, controls, alerts, and rule editor

### Rendering System
- Abstracted for multiple backends (SDL, Metal, Vulkan, etc.)
- SceneRenderer: draws map, vehicles, and overlays
- HeatmapRenderer: specialized for congestion visualization

### Data & Integration
- CSVLoader: loads road and vehicle data
- LiveFeed: ingests real-time data (auto-reconnect, JSON/Protobuf)
- RecordingManager: session recording and replay
- Python bindings: exposes simulation and analytics to Python
- Plugin loader: loads and sandboxes C/C++/Rust modules

### Traffic Entities
- Supports both human-driven and self-driving vehicles (planned)
- Entities are extensible for future ML/AI integration

### Logging System
- Structured, colorized, thread-safe logging (console & file)
- Parameterized macros for consistent, filterable logs

---

## 4. Extensibility & Future-Proofing
- **Plugin System:** Load custom analytics, DSP, or ML modules at runtime (FR-08)
- **Python Scripting:** Automate scenarios, analytics, and orchestration (FR-07)
- **Self-Driving Entities:** Simulate and visualize autonomous vehicles (planned)
- **Layer Serialization:** Save/load UI and visualization state
- **Cross-Platform:** Designed for macOS, Linux, Windows (NFR-07)

---

## 5. Mapping to Functional Requirements
| FR    | Component(s)                   | Notes                        |
| ----- | ------------------------------ | ---------------------------- |
| FR-01 | LiveFeed, Engine               | Real-time data ingestion     |
| FR-02 | CSVLoader, RecordingManager    | Historical log loading       |
| FR-03 | LiveFeed, Data Fusion          | Multi-source sensor fusion   |
| FR-04 | SimulationLayer, SceneRenderer | Map-based visualization      |
| FR-05 | HeatmapLayer, HeatmapRenderer  | Congestion overlays          |
| FR-06 | Algorithms, Export             | Isochrone computation/export |
| FR-07 | Python Bindings                | Scripting API                |
| FR-08 | Plugin Loader                  | Runtime extensibility        |
| FR-09 | ImGuiLayer, Alerts             | Alerts & event rules         |
| FR-10 | RecordingManager               | Session recording/replay     |

---

## 6. Performance & Cross-Platform Considerations
- Optimized for ≥ 60 fps at 1080p (NFR-01)
- Thread-safe logging and data ingestion
- Designed for horizontal scalability and future distributed deployment
- CI/CD for macOS, Linux, Windows

---

## 7. Future Improvements
- Additional visualization layers (e.g., isochrones, ML overlays)
- More renderer backends (Metal, Vulkan)
- Enhanced plugin sandboxing and monitoring
- Full support for self-driving traffic entities
- Layer serialization and workspace export
- Advanced analytics and reporting modules

---
For detailed requirements, see [Product Requirements Document.md](Product%20Requirments%20Document.md).