#!/usr/bin/env python3
"""
TrafficFlowViz - Rush Hour Simulation

This script demonstrates using the Python bindings to create a rush hour traffic simulation.
"""

import trafficflowviz as tfv
import os

# Get the project root directory
script_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.abspath(os.path.join(script_dir, "../.."))

# Create engine instance
engine = tfv.Engine("Rush Hour Simulation", 1280, 720)

# Set data paths relative to project root
engine.set_road_csv(os.path.join(project_root, "data/roads/roads_complex.csv"))
engine.set_csv(os.path.join(project_root, "data/vehicles/vehicles.csv"))

# Initialize and run the simulation
engine.run()

