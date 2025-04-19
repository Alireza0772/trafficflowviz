# Product Requirements Document – Table View

> *Traffic Flow Visualization & Analytics Platform*

---

## 1. Functional Requirements (FR)

| ID | Feature / Capability | User Story / Rationale | Priority | Acceptance Criteria | Owner |
|----|----------------------|------------------------|----------|---------------------|-------|
| FR‑01 | **Live Data Ingestion** | As an operator, I want the platform to subscribe to city RTTI feeds (REST/WebSocket) so that I can monitor traffic in real time. | Must‑Have | • Ingest ≥ 10 k msgs/s with ≤ 1 s latency.<br>• Supports JSON & Protobuf payloads.<br>• Auto‑reconnect on network drop. | Ingestion Team |
| FR‑02 | **Historical Log Loader** | As a researcher, I need to load archived CSV/Parquet logs to replay past events for analysis. | Must‑Have | • Import 24‑h city log (< 5 GB) in ≤ 60 s.<br>• Time‑sync with map playback controls. | Core Team |
| FR‑03 | **Multi‑Source Fusion** | Merge sensor feeds (loop, camera, GPS) into a unified timeline. | Must‑Have | • Clock‑drift ≤ 250 ms across sources.<br>• Duplicate filtering heuristics toggled per source. | Core Team |
| FR‑04 | **Map‑Based Flow Visualisation** | Show vehicle densities and vector arrows on a zoomable map. | Must‑Have | • 60 fps at 1080p on M1 GPU.<br>• Layer toggle for heatmap vs arrows. | UI/Graphics |
| FR‑05 | **Congestion Heatmaps** | Colour‑code segments by speed‑vs‑freeflow ratio. | Should‑Have | • Colour grade updates every 1 s.<br>• Hover tooltip shows ratio & sample size. | UI/Graphics |
| FR‑06 | **Travel‑Time Isochrones** | Compute reachable area from point‑of‑interest every 5 min. | Could‑Have | • Isochrone polygon within 300 ms.
• Export GeoJSON. | Algorithms |
| FR‑07 | **Python Scripting API** | Expose core to Python for algorithm testing and orchestration. | Must‑Have | • Wheel builds for macOS universal2 & Linux x64.<br>• Docs generated in Sphinx. | Integration |
| FR‑08 | **Plugin System** | Load custom C/C++/Rust DSP or ML plugins at runtime. | Should‑Have | • ABI‑stable C shim.<br>• Sandbox plugins in separate thread with watchdog. | Core Team |
| FR‑09 | **Alerts & Event Rules** | Trigger notifications when KPIs exceed threshold. | Could‑Have | • Rule editor UI.<br>• Webhook & email sink. | UI/Backend |
| FR‑10 | **Session Recording / Replay** | Record live session and replay for demos or debugging. | Should‑Have | • Record 8‑h session < 1 GB.<br>• Seek control with ± 1 s accuracy. | Core Team |

## 2. Non‑Functional Requirements (NFR)

| ID | Quality Attribute | Description | Metric / Target | Verification |
|----|------------------|-------------|-----------------|-------------|
| NFR‑01 | **Performance** | Real‑time processing capacity. | ≥ 100 k msgs/s, frame latency ≤ 16 ms. | Benchmark Suite |
| NFR‑02 | **Scalability** | Horizontal scale via message broker. | Add node → +90 % throughput within 30 s join. | Load Test |
| NFR‑03 | **Reliability** | Mean Time Between Failure. | ≥ 500 h MTBF; graceful degradation. | Chaos Test |
| NFR‑04 | **Security** | Protect data in transit & at rest. | TLS 1.3, OAuth 2.0, RBAC; no critical CVEs. | Pen‑Test Report |
| NFR‑05 | **Maintainability** | Ease of adding new features. | Cyclomatic avg < 10; 85 % unit coverage. | Code Audit |
| NFR‑06 | **Usability** | Intuitive UI for planners. | SUS ≥ 80 within pilot group. | UX Study |
| NFR‑07 | **Portability** | Runs on macOS, Linux, Windows. | Single codebase; CI pass on 3 OS. | Continuous CI |
| NFR‑08 | **Compliance** | Align with GDPR & ISO 37120. | Privacy review pass; KPI alignment docs. | Legal Review |

## 3. Open Questions

| # | Question | Owner | Due |
|---|----------|-------|-----|
| 1 | Final decision on map tile provider licensing? | Product | 2025‑05‑01 |
| 2 | GPU strategy: Metal‑only vs MoltenVK parity? | Tech Lead | 2025‑05‑15 |
| 3 | Preferred message broker (Kafka vs NATS)? | Architecture | 2025‑04‑30 |

## 4. Dependencies

| Dependency | Description | Status |
|------------|-------------|--------|
| City GIS data | Accurate road topology | Signed MOU pending |
| Sensor API keys | Access to RTTI feeds | Requested |
| Hardware testbed | M2 Pro Mac Minis | Ordered, ETA 2 w |

---
> *Created 2025‑04‑17*
> 