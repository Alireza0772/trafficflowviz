#!/bin/bash

# Update includes in all source and header files
find src include -type f \( -name "*.cpp" -o -name "*.hpp" \) | xargs grep -l "trafficflowviz" | while read file; do
  echo "Updating $file"
  sed -i '' 's/#include "trafficflowviz\//#include "/g' "$file"
done 