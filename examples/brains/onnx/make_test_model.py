#!/usr/bin/env python3
"""Mint a tiny ONNX cruise model for the TrafficFlowViz ONNX backend.

This is a developer convenience — it is NOT invoked by the build. Run it where you have
torch installed to produce a model usable by the onnx smoke test / the app:

    python make_test_model.py cruise.onnx
    TFV_ONNX_TEST_MODEL=$PWD/cruise.onnx ctest -R onnx_brain_smoke   # (build with -DTFV_WITH_ONNX=ON)
    # or in trafficflowviz.conf:  sim.default_brain = onnx:/abs/path/cruise.onnx

Contract (see ../python/example_brain.py for the obs channel layout):
  input  "obs"    : float32, shape [N, 24]  (dynamic batch N; locked Observation channels)
  output "action" : float32, shape [N, 1]   (col 0 = accel request; cols 1..5 optional)

The model mirrors example_brain.c / example_brain.py: proportional cruise toward the
observed speed limit, accel = clip((v0 - speed) * 0.4, -6, 1.5), where speed = obs[:,0]*40
and v0 = obs[:,4]*40. The engine's ActionValidator clamps the output regardless.
"""
import sys
import torch
import torch.nn as nn

OBS_SPEED_SCALE = 40.0


class Cruise(nn.Module):
    def forward(self, obs):  # obs: [N, 24]
        speed = obs[:, 0] * OBS_SPEED_SCALE
        v0 = obs[:, 4] * OBS_SPEED_SCALE
        accel = torch.clamp((v0 - speed) * 0.4, -6.0, 1.5)
        return accel.reshape(-1, 1)  # [N, 1]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "cruise.onnx"
    model = Cruise().eval()
    dummy = torch.zeros(1, 24, dtype=torch.float32)
    torch.onnx.export(
        model, dummy, out,
        input_names=["obs"], output_names=["action"],
        dynamic_axes={"obs": {0: "N"}, "action": {0: "N"}},
        opset_version=13,
    )
    print("wrote", out)


if __name__ == "__main__":
    main()
