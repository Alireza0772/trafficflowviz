# Traffic Flow Visualization Architecture

## Overview

The Traffic Flow Visualization application uses a layered architecture inspired by Walnut (https://github.com/StudioCherno/Walnut) for rendering and UI components. This document outlines the main components and architecture of the system.

## Core Components

### Engine

The `Engine` class is the central coordinator that initializes and manages all subsystems. It handles:
- Window and renderer creation
- Main application loop (update, render, event handling)
- Layer management
- Core simulation components

### Layer System

The application uses a layered architecture for rendering and UI components. Each layer can be independently updated, rendered, and receive events. Layers are processed in order from lowest to highest z-index.

#### Layer Interface

The `Layer` abstract class provides the interface for all rendering layers:
- `onAttach()` - Called when the layer is added to the stack
- `onDetach()` - Called when the layer is removed from the stack
- `onEvent(void* event)` - Processes events (returns true if handled)
- `onUpdate(double dt)` - Updates layer logic
- `onRender()` - Renders layer content
- `onImGuiRender()` - Renders ImGui components for this layer

#### LayerStack

The `LayerStack` class manages a collection of layers and handles:
- Adding and removing layers
- Maintaining z-index order
- Dispatching events, updates, and render calls to all layers

#### Implemented Layers

1. **SimulationLayer** (z-index 0)
   - Renders the core traffic simulation
   - Handles camera controls (pan/zoom)
   - Manages the SceneRenderer

2. **HeatmapLayer** (z-index 1)
   - Renders traffic congestion heatmap visualization
   - Overlays on top of the simulation layer
   - Can be toggled on/off

3. **ImGuiLayer** (z-index 100)
   - Handles all Dear ImGui UI rendering
   - Provides user interface controls, menus, and windows
   - Manages dockable UI layout

## Rendering System

The rendering system is abstracted to support multiple backends:

- `Renderer` - Abstract interface for rendering commands
- `SceneRenderer` - Handles traffic visualization rendering
- `HeatmapRenderer` - Specialized renderer for congestion heatmaps
- Platform-specific implementations (e.g., `SDLRenderer`)

## Benefits of Layered Architecture

1. **Separation of Concerns**
   - Each layer has a specific responsibility
   - Easier to maintain and extend

2. **Flexibility**
   - Layers can be added, removed, or reordered at runtime
   - Features can be toggled by enabling/disabling layers

3. **Event Handling**
   - Events cascade through layers in z-order
   - Top layers can intercept events before they reach lower layers

4. **Independent Updating**
   - Each layer manages its own update and render logic
   - Layers can be updated at different rates if needed

## Future Improvements

- Add more specialized visualization layers
- Implement additional renderer backends (Metal, Vulkan, etc.)
- Create a plugin system for extending functionality
- Add support for layer serialization 