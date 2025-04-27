#ifndef TFV_LAYER_STACK_HPP
#define TFV_LAYER_STACK_HPP

#include "core/Layer.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace tfv
{

    /**
     * Manages a stack of layers that can be updated and rendered in sequence
     */
    class LayerStack
    {
      public:
        LayerStack() = default;
        ~LayerStack();

        // Add a layer to the stack
        void pushLayer(std::shared_ptr<Layer> layer);

        // Remove a layer from the stack
        void popLayer(std::shared_ptr<Layer> layer);

        // Get a layer by name
        std::shared_ptr<Layer> getLayerByName(const std::string& name);

        // Process events through all layers
        bool onEvent(void* event);

        // Update all layers
        void onUpdate(double dt);

        // Render all layers
        void onRender();

        // Render ImGui components for all layers
        void onImGuiRender();

        // Clear all layers
        void clear();

        // Forward iterator access
        std::vector<std::shared_ptr<Layer>>::iterator begin() { return m_layers.begin(); }
        std::vector<std::shared_ptr<Layer>>::iterator end() { return m_layers.end(); }
        std::vector<std::shared_ptr<Layer>>::reverse_iterator rbegin() { return m_layers.rbegin(); }
        std::vector<std::shared_ptr<Layer>>::reverse_iterator rend() { return m_layers.rend(); }

      private:
        std::vector<std::shared_ptr<Layer>> m_layers;

        // Sort layers by z-index
        void sortLayers();
    };

} // namespace tfv

#endif // TFV_LAYER_STACK_HPP