#!/bin/bash

# Fix specific include paths based on the structure
find include -type f -name "*.hpp" | while read file; do
  echo "Checking $file"
  
  # Get the directory where the file is located
  dir=$(dirname "$file" | sed 's|include/||')
  
  # Check for includes without a directory prefix
  grep -l '#include ".*\.hpp"' "$file" | xargs sed -i '' -E "s|#include \"([^/]+\.hpp)\"|#include \"$dir/\1\"|g"
  
  # Fix known includes
  sed -i '' 's|#include "RoadNetwork.hpp"|#include "core/RoadNetwork.hpp"|g' "$file"
  sed -i '' 's|#include "TrafficEntity.hpp"|#include "core/TrafficEntity.hpp"|g' "$file"
  sed -i '' 's|#include "Simulation.hpp"|#include "core/Simulation.hpp"|g' "$file"
  sed -i '' 's|#include "Engine.hpp"|#include "core/Engine.hpp"|g' "$file"
  sed -i '' 's|#include "Renderer.hpp"|#include "rendering/Renderer.hpp"|g' "$file"
  sed -i '' 's|#include "SceneRenderer.hpp"|#include "rendering/SceneRenderer.hpp"|g' "$file"
  sed -i '' 's|#include "HeatmapRenderer.hpp"|#include "rendering/HeatmapRenderer.hpp"|g' "$file"
  sed -i '' 's|#include "LiveFeed.hpp"|#include "network/LiveFeed.hpp"|g' "$file"
  sed -i '' 's|#include "AlertManager.hpp"|#include "alerts/AlertManager.hpp"|g' "$file"
  sed -i '' 's|#include "CSVLoader.hpp"|#include "data/CSVLoader.hpp"|g' "$file"
  sed -i '' 's|#include "RecordingManager.hpp"|#include "recording/RecordingManager.hpp"|g' "$file"
done 