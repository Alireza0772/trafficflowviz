# ONNX Runtime brains

Run a trained model (PyTorch / TensorFlow → ONNX) as a vehicle brain.

## Build

ONNX support is opt-in (the default build needs no ONNX Runtime):

```bash
cmake -B build -S . -DTFV_WITH_ONNX=ON     # finds ONNX Runtime via:
#   1. find_package(onnxruntime CONFIG)   — vcpkg port "onnxruntime", or a distro config
#   2. cmake/FindONNXRuntime.cmake        — raw header+lib install; pass -DONNXRUNTIME_ROOT=<dir>
#                                            or set $ONNXRUNTIME_ROOT (e.g. brew --prefix onnxruntime)
```

Configuring with `-DTFV_WITH_ONNX=ON` but no ONNX Runtime found gives a clear error
naming `ONNXRUNTIME_ROOT`. (Note: `onnxruntime` is intentionally **not** in `vcpkg.json`
— it is a heavy port; add `{"name":"onnxruntime"}` yourself, or install the port, when you
want the vcpkg flow to resolve it.)

## Select the brain

In `trafficflowviz.conf`:

```
sim.default_brain = onnx:/abs/path/to/model.onnx
# optional: onnx:/abs/path/model.onnx?drives_lane_change=1
# sim.onnx.warmup = 1              # soft warm-up decode at load (default on)
# sim.onnx.drives_lane_change = 0  # let the model emit col 1 (laneChange) instead of MOBIL
```

## Model contract

- **input**: one `float32` tensor, shape `[N, 24]`, **dynamic batch** `N` (the 24 channels
  are the locked Observation layout — see `../python/example_brain.py`). The input *name* is
  discovered at load (not hardcoded).
- **output**: one `float32` tensor, shape `[N, K]`, `1 ≤ K ≤ 6`. Columns map to
  `[accel, laneChange, turn, lightCmd, reserved0, reserved1]`; only **col 0 (accel)** is
  required. The engine's `ActionValidator` clamps/scrubs the result, so the model emits raw
  requests safely.

Anything else (multi-input/output, non-float32, wrong trailing dim) is rejected at load and
the engine falls back to the rule brain; a per-tick failure degrades to safe holds. The
example model's internal `clamp` is illustrative only — the engine's `ActionValidator` is
the sole authority bounding any brain's output.

**Platform:** like the `dll:` backend, the ONNX backend is currently POSIX-targeted (the
model path is passed as `const char*`); Windows would need a `wchar_t` path conversion
guarded by `#ifdef _WIN32`.

## Make a test model

`make_test_model.py` (needs `torch`; **not** invoked by the build) writes a tiny cruise
model for the smoke test:

```bash
python make_test_model.py cruise.onnx
cmake -B build -S . -DTFV_WITH_ONNX=ON && cmake --build build
TFV_ONNX_TEST_MODEL=$PWD/cruise.onnx ctest --test-dir build -R onnx_brain_smoke
```
