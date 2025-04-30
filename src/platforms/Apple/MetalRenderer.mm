#include "rendering/platforms/Metal.hpp"

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <Cocoa/Cocoa.h>
#endif

#include <iostream>

namespace tfv
{
// Metal implementation
#if defined(__APPLE__)


    // Helper class to hold Metal resources
#ifdef __OBJC__
    struct MetalResources
    {
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> commandQueue = nil;
        id<MTLRenderPipelineState> pipelineState = nil;
        id<MTLBuffer> vertexBuffer = nil;
        MTLRenderPassDescriptor* renderPassDescriptor = nil;
        MTKView* mtkView = nil;
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int width = 0;
        int height = 0;

        // Current drawing color
        float drawColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    };
#else
    // C++ compatible version for non-Objective-C compilation
    struct MetalResources
    {
        void* device = nullptr;
        void* commandQueue = nullptr;
        void* pipelineState = nullptr;
        void* vertexBuffer = nullptr;
        void* renderPassDescriptor = nullptr;
        void* mtkView = nullptr;
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int width = 0;
        int height = 0;

        // Current drawing color
        float drawColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    };

#endif

    MetalRenderer::MetalRenderer() : m_metalView(nullptr), m_antiAliasingEnabled(true)
    {
        // Allocate Metal resources
        m_metalResources = new MetalResources();
    }

    bool MetalRenderer::initialize()
    {
        if(!windowHandle)
        {
            std::cerr << "Invalid window handle for Metal renderer" << std::endl;
            return false;
        }

        MetalResources* res = static_cast<MetalResources*>(m_metalResources);
#ifdef __OBJC__
        NSWindow* nsWindow = static_cast<NSWindow*>(windowHandle);

        // Create MTKView
        NSRect frame = [nsWindow.contentView frame];
        res->width = frame.size.width;
        res->height = frame.size.height;

        // Create a Metal device
        res->device = MTLCreateSystemDefaultDevice();
        if(!res->device)
        {
            std::cerr << "Metal is not supported on this device" << std::endl;
            return false;
        }

        // Create the view
        res->mtkView = [[MTKView alloc] initWithFrame:frame device:res->device];
        res->mtkView.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        res->mtkView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        res->mtkView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        res->mtkView.sampleCount = m_antiAliasingEnabled ? 4 : 1;

        // Replace the content view
        [nsWindow setContentView:res->mtkView];

        // Create command queue
        res->commandQueue = [res->device newCommandQueue];

        // Create render pass descriptor
        res->renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];

        // Store the view for later
        m_metalView = res->mtkView;

        std::cout << "Metal renderer initialized successfully" << std::endl;
#else
        // Non-Objective-C++ implementation
        std::cerr << "Metal renderer can only be initialized in Objective-C++ mode" << std::endl;
        return false;
#endif
        return true;
    }

    void MetalRenderer::shutdown()
    {
        if(m_metalResources)
        {
            MetalResources* res = static_cast<MetalResources*>(m_metalResources);
            res->device = nil;
            res->commandQueue = nil;
            res->pipelineState = nil;
            res->vertexBuffer = nil;
            res->renderPassDescriptor = nil;
            res->mtkView = nil;
            delete res;
            m_metalResources = nullptr;
        }

        m_metalView = nullptr;
    }

    MetalRenderer::~MetalRenderer()
    {
        shutdown();
    }

    void* MetalRenderer::getNativeRenderer() const
    {
        return m_metalView;
    }

    void MetalRenderer::getWindowSize(int& width, int& height) const
    {
        if(m_metalResources)
        {
            MetalResources* res = static_cast<MetalResources*>(m_metalResources);
            width = res->width;
            height = res->height;
        }
        else
        {
            width = 0;
            height = 0;
        }
    }

    void MetalRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if(m_metalResources)
        {
            MetalResources* res = static_cast<MetalResources*>(m_metalResources);
            res->clearColor[0] = r / 255.0f;
            res->clearColor[1] = g / 255.0f;
            res->clearColor[2] = b / 255.0f;
            res->clearColor[3] = a / 255.0f;

#ifdef __OBJC__
            // Update MTKView clear color
            MTKView* view = static_cast<MTKView*>(m_metalView);
            view.clearColor = MTLClearColorMake(res->clearColor[0], res->clearColor[1],
                                                res->clearColor[2], res->clearColor[3]);
#endif
        }
    }

    void MetalRenderer::present()
    {
        // Metal rendering is handled by the MTKView's draw method
        // This will be triggered automatically by the display link
    }

    void MetalRenderer::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if(m_metalResources)
        {
            MetalResources* res = static_cast<MetalResources*>(m_metalResources);
            res->drawColor[0] = r / 255.0f;
            res->drawColor[1] = g / 255.0f;
            res->drawColor[2] = b / 255.0f;
            res->drawColor[3] = a / 255.0f;
        }
    }

    void MetalRenderer::drawLine(int x1, int y1, int x2, int y2)
    {
        // Basic line drawing using Metal
        // Simplified implementation - would be more efficient with batched rendering
        if(m_metalResources)
        {
            // Drawing would be implemented in the MTKView's drawInMTKView method
            // This would setup a line primitive and render it
        }
    }

    void MetalRenderer::drawLine(int x1, int y1, int x2, int y2, int width)
    {
        // Implementation for thicker lines
        // For Metal, this would typically be done using a triangle strip or shader
        if(m_metalResources)
        {
            // Implementation would go here
        }
    }

    void MetalRenderer::drawPoint(int x, int y)
    {
        if(m_metalResources)
        {
            // Draw a point using Metal
        }
    }

    void MetalRenderer::drawRect(int x, int y, int w, int h)
    {
        if(m_metalResources)
        {
            // Draw a rectangle outline using Metal
        }
    }

    void MetalRenderer::fillRect(int x, int y, int w, int h)
    {
        if(m_metalResources)
        {
            // Draw a filled rectangle using Metal
        }
    }

    void MetalRenderer::drawText(const std::string& text, int x, int y)
    {
        if(m_metalResources)
        {
            // Text rendering in Metal requires texture-based glyph rendering
            // This is a more complex implementation that would require a text atlas
        }
    }

    void MetalRenderer::setAntiAliasing(bool enable)
    {
        m_antiAliasingEnabled = enable;

        if(m_metalView)
        {
#ifdef __OBJC__
            MTKView* view = static_cast<MTKView*>(m_metalView);
            view.sampleCount = enable ? 4 : 1;
#endif
        }
    }
#else
    // Stub implementation for non-Apple platforms
    MetalRenderer::MetalRenderer() : m_metalView(nullptr) {}
    bool MetalRenderer::initialize(void* windowHandle)
    {
        return false;
    }
    void MetalRenderer::shutdown() {}
    MetalRenderer::~MetalRenderer() {}
    void* MetalRenderer::getNativeRenderer() const
    {
        return nullptr;
    }
    void MetalRenderer::getWindowSize(int& width, int& height) const
    {
        width = 0;
        height = 0;
    }
    void MetalRenderer::clear(uint8_t, uint8_t, uint8_t, uint8_t) {}
    void MetalRenderer::present() {}
    void MetalRenderer::setColor(uint8_t, uint8_t, uint8_t, uint8_t) {}
    void MetalRenderer::drawLine(int, int, int, int) {}
    void MetalRenderer::drawLine(int, int, int, int, int) {}
    void MetalRenderer::drawPoint(int, int) {}
    void MetalRenderer::drawRect(int, int, int, int) {}
    void MetalRenderer::fillRect(int, int, int, int) {}
    void MetalRenderer::drawText(const std::string&, int, int) {}
    void MetalRenderer::setAntiAliasing(bool enable)
    {
        m_antiAliasingEnabled = enable;
    }
#endif
}