#ifndef TFV_LAYER_HPP
#define TFV_LAYER_HPP

#include <string>

namespace tfv
{

    /**
     * Abstract layer interface for rendering or UI elements
     * Layers are processed in order from lowest to highest index
     */
    class Layer
    {
      public:
        virtual ~Layer() = default;

        // Initialize layer resources
        virtual void onAttach() {}

        // Clean up layer resources
        virtual void onDetach() {}

        // Process layer events
        virtual bool onEvent(void* event) { return false; }

        // Update layer logic
        virtual void onUpdate(double dt) {}

        // Render layer content
        virtual void onRender() {}

        // ImGui specific rendering (if needed by this layer)
        virtual void onImGuiRender() {}

        // Enable/disable layer
        virtual void setEnabled(bool enabled) { m_enabled = enabled; }
        virtual bool isEnabled() const { return m_enabled; }

        // Layer priority (higher = on top)
        virtual void setZIndex(int zIndex) { m_zIndex = zIndex; }
        virtual int getZIndex() const { return m_zIndex; }

        // Layer name for debugging
        virtual const char* getName() const { return m_debugName.c_str(); }
        virtual void setName(const std::string& name) { m_debugName = name; }

      protected:
        bool m_enabled = true;
        int m_zIndex = 0;
        std::string m_debugName = "Layer";
    };

} // namespace tfv

#endif // TFV_LAYER_HPP