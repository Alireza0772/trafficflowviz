#include "rendering/platforms/Metal.hpp"

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#endif

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace tfv
{
#if defined(__APPLE__)

    namespace
    {
        // CPU vertex: framebuffer-pixel position + RGBA float [0,1]. Matches the MSL layout below
        // (6 floats per vertex), so the whole frame is one big triangle batch.
        struct MVertex
        {
            float x, y;
            float r, g, b, a;
        };

        // Single pipeline: 2D pixel coords -> NDC (y flipped), per-vertex color, alpha blended.
        const char* kMSL = R"METAL(
        #include <metal_stdlib>
        using namespace metal;
        struct VOut { float4 pos [[position]]; float4 col; };
        struct Uniforms { float2 viewport; };
        vertex VOut tfv_vs(uint vid [[vertex_id]],
                           const device float* verts [[buffer(0)]],
                           constant Uniforms& u [[buffer(1)]]) {
            const device float* v = verts + vid * 6u;
            VOut o;
            o.pos = float4(v[0] / u.viewport.x * 2.0 - 1.0,
                           1.0 - v[1] / u.viewport.y * 2.0, 0.0, 1.0);
            o.col = float4(v[2], v[3], v[4], v[5]);
            return o;
        }
        fragment float4 tfv_fs(VOut in [[stage_in]]) { return in.col; }
        )METAL";
    } // namespace

    struct MetalResources
    {
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        CAMetalLayer* layer = nil;
        SDL_MetalView sdlView = 0;
        id<MTLRenderPipelineState> pipeline = nil;
        id<MTLTexture> msaa = nil; // multisample color target (resolved to the drawable)
        id<MTLBuffer> vbuf = nil;  // reused vertex buffer (grows as needed)
        NSUInteger vbufCap = 0;
        int msaaW = 0, msaaH = 0;
        int sampleCount = 4;
        double clearColor[4] = {0, 0, 0, 1};
        float curColor[4] = {1, 1, 1, 1}; // current setColor() (used by line/rect/point)
        std::vector<MVertex> batch;       // per-frame triangle soup
    };

    MetalRenderer::MetalRenderer(void* window) : m_window(window)
    {
        m_res = new MetalResources();
    }

    MetalRenderer::~MetalRenderer()
    {
        shutdown();
    }

    bool MetalRenderer::initialize()
    {
        auto* R = static_cast<MetalResources*>(m_res);
        SDL_Window* win = static_cast<SDL_Window*>(m_window);
        if(!win)
        {
            std::cerr << "[Metal] no window handle\n";
            return false;
        }
        R->device = MTLCreateSystemDefaultDevice();
        if(!R->device)
        {
            std::cerr << "[Metal] no Metal device on this machine\n";
            return false;
        }
        R->sdlView = SDL_Metal_CreateView(win);
        if(!R->sdlView)
        {
            std::cerr << "[Metal] SDL_Metal_CreateView failed: " << SDL_GetError() << "\n";
            return false;
        }
        R->layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(R->sdlView);
        R->layer.device = R->device;
        R->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        R->layer.framebufferOnly = NO; // allow drawable-texture readback (screenshots, Phase 3)
        R->queue = [R->device newCommandQueue];

        NSError* err = nil;
        id<MTLLibrary> lib = [R->device newLibraryWithSource:@(kMSL) options:nil error:&err];
        if(!lib)
        {
            std::cerr << "[Metal] shader compile failed: "
                      << (err ? err.localizedDescription.UTF8String : "?") << "\n";
            return false;
        }
        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = [lib newFunctionWithName:@"tfv_vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"tfv_fs"];
        pd.rasterSampleCount = static_cast<NSUInteger>(R->sampleCount); // hardware MSAA
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pd.colorAttachments[0].blendingEnabled = YES;
        pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        R->pipeline = [R->device newRenderPipelineStateWithDescriptor:pd error:&err];
        if(!R->pipeline)
        {
            std::cerr << "[Metal] pipeline creation failed: "
                      << (err ? err.localizedDescription.UTF8String : "?") << "\n";
            return false;
        }
        std::cout << "[Metal] renderer initialized (MSAA " << R->sampleCount << "x)\n";
        return true;
    }

    void MetalRenderer::shutdown()
    {
        if(!m_res)
            return;
        auto* R = static_cast<MetalResources*>(m_res);
        if(R->sdlView)
        {
            SDL_Metal_DestroyView(R->sdlView);
            R->sdlView = 0;
        }
        delete R; // ARC releases the Metal objects held by the struct
        m_res = nullptr;
    }

    void MetalRenderer::getWindowSize(int& w, int& h) const
    {
        if(m_window)
            SDL_GetWindowSize(static_cast<SDL_Window*>(m_window), &w, &h);
        else
        {
            w = 0;
            h = 0;
        }
    }

    void MetalRenderer::getDrawableSize(int& w, int& h) const
    {
        if(m_window)
            SDL_Metal_GetDrawableSize(static_cast<SDL_Window*>(m_window), &w, &h);
        else
            getWindowSize(w, h);
    }

    void* MetalRenderer::getNativeRenderer() const
    {
        auto* R = static_cast<MetalResources*>(m_res);
        return R ? (__bridge void*)R->layer : nullptr;
    }

    void MetalRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R)
            return;
        R->clearColor[0] = r / 255.0;
        R->clearColor[1] = g / 255.0;
        R->clearColor[2] = b / 255.0;
        R->clearColor[3] = a / 255.0;
        R->batch.clear(); // a new frame begins
    }

    void MetalRenderer::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R)
            return;
        R->curColor[0] = r / 255.0f;
        R->curColor[1] = g / 255.0f;
        R->curColor[2] = b / 255.0f;
        R->curColor[3] = a / 255.0f;
    }

    void MetalRenderer::fillGeometry(const RVertex* verts, int vcount, const int* idx, int icount)
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R || vcount <= 0 || icount <= 0)
            return;
        R->batch.reserve(R->batch.size() + static_cast<std::size_t>(icount));
        for(int i = 0; i < icount; ++i)
        {
            const int j = idx[i];
            if(j < 0 || j >= vcount)
                continue;
            const RVertex& s = verts[j];
            R->batch.push_back(
                {s.x, s.y, s.r / 255.0f, s.g / 255.0f, s.b / 255.0f, s.a / 255.0f});
        }
    }

    namespace
    {
        // Append a filled quad a->b->c->d (two triangles) in the current color.
        void pushQuad(MetalResources* R, float ax, float ay, float bx, float by, float cx, float cy,
                      float dx, float dy)
        {
            const float* c = R->curColor;
            const MVertex A{ax, ay, c[0], c[1], c[2], c[3]};
            const MVertex B{bx, by, c[0], c[1], c[2], c[3]};
            const MVertex C{cx, cy, c[0], c[1], c[2], c[3]};
            const MVertex D{dx, dy, c[0], c[1], c[2], c[3]};
            R->batch.push_back(A);
            R->batch.push_back(B);
            R->batch.push_back(C);
            R->batch.push_back(A);
            R->batch.push_back(C);
            R->batch.push_back(D);
        }
    } // namespace

    void MetalRenderer::fillRect(int x, int y, int w, int h)
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R)
            return;
        pushQuad(R, x, y, x + w, y, x + w, y + h, x, y + h);
    }

    void MetalRenderer::drawLine(int x1, int y1, int x2, int y2)
    {
        drawLine(x1, y1, x2, y2, 1);
    }

    void MetalRenderer::drawLine(int x1, int y1, int x2, int y2, int width)
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R)
            return;
        const float hw = std::max(0.5f, width * 0.5f);
        float dx = static_cast<float>(x2 - x1), dy = static_cast<float>(y2 - y1);
        const float len = std::sqrt(dx * dx + dy * dy);
        if(len < 1e-4f)
        {
            pushQuad(R, x1 - hw, y1 - hw, x1 + hw, y1 - hw, x1 + hw, y1 + hw, x1 - hw, y1 + hw);
            return;
        }
        const float nx = -dy / len * hw, ny = dx / len * hw;
        pushQuad(R, x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny);
    }

    void MetalRenderer::drawPoint(int x, int y)
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R)
            return;
        pushQuad(R, x - 0.5f, y - 0.5f, x + 0.5f, y - 0.5f, x + 0.5f, y + 0.5f, x - 0.5f, y + 0.5f);
    }

    void MetalRenderer::drawRect(int x, int y, int w, int h)
    {
        drawLine(x, y, x + w, y, 1);
        drawLine(x + w, y, x + w, y + h, 1);
        drawLine(x + w, y + h, x, y + h, 1);
        drawLine(x, y + h, x, y, 1);
    }

    void MetalRenderer::drawText(const std::string& /*text*/, int /*x*/, int /*y*/)
    {
        // Phase 4: glyph-atlas text. The scene's text comes from ImGui, not this path.
    }

    void MetalRenderer::present()
    {
        auto* R = static_cast<MetalResources*>(m_res);
        if(!R || !R->layer)
            return;
        @autoreleasepool
        {
            int dw = 0, dh = 0;
            getDrawableSize(dw, dh);
            if(dw <= 0 || dh <= 0)
                return;
            if(R->sampleCount > 1 && (!R->msaa || R->msaaW != dw || R->msaaH != dh))
            {
                MTLTextureDescriptor* td = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                 width:static_cast<NSUInteger>(dw)
                                                height:static_cast<NSUInteger>(dh)
                                             mipmapped:NO];
                td.textureType = MTLTextureType2DMultisample;
                td.sampleCount = static_cast<NSUInteger>(R->sampleCount);
                td.usage = MTLTextureUsageRenderTarget;
                td.storageMode = MTLStorageModePrivate;
                R->msaa = [R->device newTextureWithDescriptor:td];
                R->msaaW = dw;
                R->msaaH = dh;
            }
            id<CAMetalDrawable> drawable = [R->layer nextDrawable];
            if(!drawable)
                return;

            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            MTLRenderPassColorAttachmentDescriptor* ca = rp.colorAttachments[0];
            ca.clearColor = MTLClearColorMake(R->clearColor[0], R->clearColor[1], R->clearColor[2],
                                              R->clearColor[3]);
            ca.loadAction = MTLLoadActionClear;
            if(R->sampleCount > 1 && R->msaa)
            {
                ca.texture = R->msaa;
                ca.resolveTexture = drawable.texture;
                ca.storeAction = MTLStoreActionMultisampleResolve; // downsample MSAA -> drawable
            }
            else
            {
                ca.texture = drawable.texture;
                ca.storeAction = MTLStoreActionStore;
            }

            id<MTLCommandBuffer> cb = [R->queue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            if(!R->batch.empty())
            {
                [enc setRenderPipelineState:R->pipeline];
                const NSUInteger bytes = R->batch.size() * sizeof(MVertex);
                if(!R->vbuf || R->vbufCap < bytes)
                {
                    R->vbuf = [R->device newBufferWithLength:bytes
                                                     options:MTLResourceStorageModeShared];
                    R->vbufCap = bytes;
                }
                std::memcpy([R->vbuf contents], R->batch.data(), bytes);
                [enc setVertexBuffer:R->vbuf offset:0 atIndex:0];
                float vp[2] = {static_cast<float>(dw), static_cast<float>(dh)};
                [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
                [enc drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:R->batch.size()];
            }
            [enc endEncoding];
            [cb presentDrawable:drawable];
            [cb commit];
            R->batch.clear();
        }
    }

    void MetalRenderer::setAntiAliasing(bool enable)
    {
        // The pipeline's rasterSampleCount is fixed at initialize(), so we must NOT change the
        // render-pass sample count at runtime (a mismatch is a hard Metal error). MSAA stays 4x;
        // a runtime toggle would require rebuilding the pipeline (deferred).
        m_antiAliasingEnabled = enable;
    }

#else // ---- non-Apple stub (keeps the build cross-platform) ----
    MetalRenderer::MetalRenderer(void* window) : m_window(window) {}
    MetalRenderer::~MetalRenderer() {}
    bool MetalRenderer::initialize() { return false; }
    void MetalRenderer::shutdown() {}
    void MetalRenderer::clear(uint8_t, uint8_t, uint8_t, uint8_t) {}
    void MetalRenderer::present() {}
    void MetalRenderer::setColor(uint8_t, uint8_t, uint8_t, uint8_t) {}
    void MetalRenderer::drawLine(int, int, int, int) {}
    void MetalRenderer::drawLine(int, int, int, int, int) {}
    void MetalRenderer::drawPoint(int, int) {}
    void MetalRenderer::drawRect(int, int, int, int) {}
    void MetalRenderer::fillRect(int, int, int, int) {}
    void MetalRenderer::drawText(const std::string&, int, int) {}
    void MetalRenderer::fillGeometry(const RVertex*, int, const int*, int) {}
    void MetalRenderer::setAntiAliasing(bool enable) { m_antiAliasingEnabled = enable; }
    void* MetalRenderer::getNativeRenderer() const { return nullptr; }
    void MetalRenderer::getWindowSize(int& w, int& h) const { w = 0; h = 0; }
    void MetalRenderer::getDrawableSize(int& w, int& h) const { w = 0; h = 0; }
#endif
} // namespace tfv
