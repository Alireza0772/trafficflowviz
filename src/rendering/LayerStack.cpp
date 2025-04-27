#include "core/LayerStack.hpp"

namespace tfv
{

    LayerStack::~LayerStack()
    {
        clear();
    }

    void LayerStack::pushLayer(std::shared_ptr<Layer> layer)
    {
        m_layers.push_back(layer);
        layer->onAttach();
        sortLayers();
    }

    void LayerStack::popLayer(std::shared_ptr<Layer> layer)
    {
        auto it = std::find(m_layers.begin(), m_layers.end(), layer);
        if(it != m_layers.end())
        {
            (*it)->onDetach();
            m_layers.erase(it);
        }
    }

    std::shared_ptr<Layer> LayerStack::getLayerByName(const std::string& name)
    {
        for(auto& layer : m_layers)
        {
            if(std::string(layer->getName()) == name)
            {
                return layer;
            }
        }
        return nullptr;
    }

    bool LayerStack::onEvent(void* event)
    {
        // Process events in reverse order (top to bottom)
        for(auto it = m_layers.rbegin(); it != m_layers.rend(); ++it)
        {
            if(!(*it)->isEnabled())
                continue;

            if((*it)->onEvent(event))
                return true; // Event was handled
        }
        return false;
    }

    void LayerStack::onUpdate(double dt)
    {
        // Update layers in order (bottom to top)
        for(auto& layer : m_layers)
        {
            if(layer->isEnabled())
                layer->onUpdate(dt);
        }
    }

    void LayerStack::onRender()
    {
        // Render layers in order (bottom to top)
        for(auto& layer : m_layers)
        {
            if(layer->isEnabled())
                layer->onRender();
        }
    }

    void LayerStack::onImGuiRender()
    {
        // Render ImGui components for each layer
        for(auto& layer : m_layers)
        {
            if(layer->isEnabled())
                layer->onImGuiRender();
        }
    }

    void LayerStack::clear()
    {
        for(auto& layer : m_layers)
        {
            layer->onDetach();
        }
        m_layers.clear();
    }

    void LayerStack::sortLayers()
    {
        // Sort layers by z-index (lower index = drawn first)
        std::sort(m_layers.begin(), m_layers.end(),
                  [](const std::shared_ptr<Layer>& a, const std::shared_ptr<Layer>& b)
                  { return a->getZIndex() < b->getZIndex(); });
    }

} // namespace tfv