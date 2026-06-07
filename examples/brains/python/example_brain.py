"""Reference external brain for the TrafficFlowViz embedded-Python backend.

Select it with, in trafficflowviz.conf:

    sim.default_brain = python:example_brain
    sim.python.path   = <directory containing this file>     # or use python:example_brain?path=<dir>

Requires a build with -DTFV_WITH_PYTHON=ON and numpy importable in the interpreter.

Contract
--------
decide_batch(obs) -> ndarray, shape (n, K), 1 <= K <= 6, float (float32 preferred):
    obs : read-only float32 ndarray, shape (n, 24) -- the locked Observation channels
          (col 0 = speed/40, col 4 = speed-limit/40, ... see include/agents/Observation.hpp).
    returns columns [accel, laneChange, turn, lightCmd, reserved0, reserved1];
            only accel (col 0) is required, trailing columns default to 0.
    MUST NOT mutate obs and MUST NOT retain the view past the call. The engine clamps and
    validates the result (acceleration limits, lane feasibility, NaN/Inf), so a model can
    emit raw requests safely.

Optional module hooks (used if present):
    reset(seed: int) -> None      # re-seed the model RNG for a reproducible (re-)init
    weights_hash() -> str         # identifies the weights in the reproducibility manifest
    drives_lane_change = False    # True only if decide_batch emits col 1 (laneChange);
                                  # otherwise the engine's MOBIL model handles lane changes.
"""

import numpy as np

OBS_SELF_SPEED = 0    # speed / OBS_SPEED_SCALE
OBS_SPEED_LIMIT = 4   # effective speed limit / OBS_SPEED_SCALE
OBS_SPEED_SCALE = 40.0


def decide_batch(obs):
    """Proportional cruise control toward the observed speed limit (mirrors example_brain.c)."""
    speed = obs[:, OBS_SELF_SPEED] * OBS_SPEED_SCALE
    v0 = obs[:, OBS_SPEED_LIMIT] * OBS_SPEED_SCALE
    accel = np.clip((v0 - speed) * 0.4, -6.0, 1.5).astype(np.float32)
    return accel.reshape(-1, 1)


def weights_hash():
    return "example-cruise-py:v1"
