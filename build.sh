#!/usr/bin/env bash
set -euo pipefail
TYPE="Debug"
[[ "${1:-}" == "release" ]] && TYPE="RelWithDebInfo"
if [[ "${1:-}" == "clean" || "${2:-}" == "clean" ]]; then
  echo "[clean] removing build/"
  rm -rf build
fi
mkdir -p build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE="${TYPE}" -DTFV_ENABLE_PYTHON=ON
ninja
printf "\n✅ Build finished.\nExecutable: build/bin/trafficviz\nPython module: build/python/trafficviz*.so\n"
