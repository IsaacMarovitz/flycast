/*
    Copyright 2025 flyinghead

    This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "metal_context.h"
#include "metal_driver.h"
#ifdef USE_SDL
#include "sdl/sdl.h"
#endif
#include "ui/imgui_driver.h"

MetalContext *MetalContext::contextInstance;

void MetalContext::CreateSwapChain()
{
    // WAIT IDLE

    commandBuffers.clear();

    [layer setPixelFormat:MTLPixelFormatRGBA8Unorm];
    [layer setColorspace:CGColorSpaceCreateWithName(kCGColorSpaceSRGB)];
    [layer setMaximumDrawableCount:3];
    [layer setDisplaySyncEnabled:TRUE];

    auto size = [layer drawableSize];
    SetWindowSize(size.width, size.height);
    resized = false;

    if (swapOnVSync && config::DupeFrames && settings.display.refreshRate > 60.f)
        swapInterval = settings.display.refreshRate / 60.f;
    else
        swapInterval = 1;

    commandBuffers.resize(3);

    quadPipeline->Init(shaderManager.get());
    quadPipelineWithAlpha->Init(shaderManager.get());
    quadDrawer->Init(quadPipeline.get());
    quadRotatePipeline->Init(shaderManager.get());
    quadRotateDrawer->Init(quadRotatePipeline.get());

    currentImage = 2;

    INFO_LOG(RENDERER, "Metal swap chain created: %d x %d, swap chain size %d", width, height, 3);
}

bool MetalContext::init()
{
    GraphicsContext::instance = this;

#ifdef USE_SDL
    if (!sdl_recreate_window(SDL_WINDOW_METAL))
        return false;

    auto view = SDL_Metal_CreateView((SDL_Window *)window);

    if (view == nullptr) {
        term();
        ERROR_LOG(RENDERER, "Failed to create SDL Metal View");
        return false;
    }

    layer = static_cast<CAMetalLayer*>(SDL_Metal_GetLayer(view));
#endif

    device = MTLCreateSystemDefaultDevice();

    if (!device) {
        term();
        NOTICE_LOG(RENDERER, "Metal Device is null.");
        return false;
    }

    [layer setDevice:device];
    queue = [device newCommandQueue];

    shaderManager = std::make_unique<MetalShaders>();
    quadPipeline = std::make_unique<MetalQuadPipeline>(true, false);
    quadPipelineWithAlpha = std::make_unique<MetalQuadPipeline>(false, false);
    quadDrawer = std::make_unique<MetalQuadDrawer>();
    quadRotatePipeline = std::make_unique<MetalQuadPipeline>(true, true);
    quadRotateDrawer = std::make_unique<MetalQuadDrawer>();

    NOTICE_LOG(RENDERER, "Created Metal view.");

    imguiDriver = std::unique_ptr<ImGuiDriver>(new MetalDriver());

    CreateSwapChain();

    return true;
}

std::string MetalContext::getDriverName() {
    return [[device name] UTF8String];
}

bool MetalContext::recreateSwapChainIfNeeded()
{
    if (resized || HasSurfaceDimensionChanged())
    {
        CreateSwapChain();
        lastFrameTexture = nil;
        return true;
    }
    else
        return false;
}

void MetalContext::Present()
{
    if (renderDone)
    {
        if (lastFrameTexture != nil && IsValid() && !gui_is_open())
            for (int i = 1; i < swapInterval; i++)
            {
                PresentFrame(lastFrameTexture, lastFrameViewport, lastFrameAR);
            }
        renderDone = false;
    }
    if (swapOnVSync == (settings.input.fastForwardMode || !config::VSync))
    {
        swapOnVSync = (!settings.input.fastForwardMode && config::VSync);
        resized = true;
    }
    if (resized)
        CreateSwapChain();
        lastFrameTexture = nil;
}

void MetalContext::DrawFrame(id<MTLTexture> texture, MTLViewport viewport, float aspectRatio) {
    MetalQuadVertex vtx[4] {
            { -1, -1, 0, 0, 0 },
            {  1, -1, 0, 1, 0 },
            { -1,  1, 0, 0, 1 },
            {  1,  1, 0, 1, 1 },
    };
    float shiftX, shiftY;
    getVideoShift(shiftX, shiftY);
    vtx[0].x = vtx[2].x = -1.f + shiftX * 2.f / viewport.width;
    vtx[1].x = vtx[3].x = vtx[0].x + 2;
    vtx[0].y = vtx[1].y = -1.f + shiftY * 2.f / viewport.height;
    vtx[2].y = vtx[3].y = vtx[0].y + 2;

    [commandEncoder pushDebugGroup:@"DrawFrame"];

    if (config::Rotate90)
        quadRotatePipeline->BindPipeline(commandEncoder);
    else
        quadPipeline->BindPipeline(commandEncoder);

    float screenAR = (float)width / height;
    float dx = 0;
    float dy = 0;
    if (aspectRatio > screenAR)
        dy = height * (1 - screenAR / aspectRatio) / 2;
    else
        dx = width * (1 - aspectRatio / screenAR) / 2;

    MTLViewport framePort = { dx, dy, width - dx * 2, height - dx * 2, 0, 1 };
    [commandEncoder setViewport:framePort];
    [commandEncoder setScissorRect:MTLScissorRect { (uint)dx, (uint)dy, (uint)(width - dx * 2), (uint)(height - dx * 2) }];
    if (config::Rotate90)
        quadRotateDrawer->Draw(commandEncoder, texture, vtx, config::TextureFiltering == 1);
    else
        quadDrawer->Draw(commandEncoder, texture, vtx, config::TextureFiltering == 1);

    [commandEncoder popDebugGroup];
}

void MetalContext::PresentFrame(id<MTLTexture> texture, MTLViewport viewport, float aspectRatio)
{
    lastFrameTexture = texture;
    lastFrameViewport = viewport;
    lastFrameAR = aspectRatio;

    if (texture != nil && IsValid())
    {
        gui_draw_osd();

        if (lastFrameTexture != nil) // Might have been nullified if swap chain recreated
            DrawFrame(texture, viewport, aspectRatio);

        imguiDriver->renderDrawData(ImGui::GetDrawData(), false);
    }
}

void MetalContext::PresentLastFrame()
{
    if (lastFrameTexture != nil && IsValid())
        DrawFrame(lastFrameTexture, lastFrameViewport, lastFrameAR);
}

void MetalContext::term() {
    GraphicsContext::instance = nullptr;
    imguiDriver.reset();
}

bool MetalContext::HasSurfaceDimensionChanged() const
{
    auto size = [layer drawableSize];
    return width != size.width || height != size.height;
}

void MetalContext::SetWindowSize(u32 width, u32 height)
{
    if (this->width != width && this->height != height)
    {
        this->width = width;
        this->height = height;

        if (width != 0)
            settings.display.width = width;

        if (height != 0)
            settings.display.height = height;

        resize();
    }
}

MetalContext::MetalContext() {
    verify(contextInstance == nullptr);
    contextInstance = this;
}

MetalContext::~MetalContext() {
    verify(contextInstance == this);
    contextInstance = nullptr;
}

bool MetalContext::GetLastFrame(std::vector<u8> &data, int &width, int &height)
{
    if (lastFrameTexture == nil)
        return false;

    if (width != 0) {
        height = width / lastFrameAR;
    }
    else if (height != 0) {
        width = lastFrameAR * height;
    }
    else
    {
        width = lastFrameViewport.width;
        height = lastFrameViewport.height;
        if (config::Rotate90)
            std::swap(width, height);
        // We need square pixels for PNG
        int w = lastFrameAR * height;
        if (width > w)
            height = width / lastFrameAR;
        else
            width = w;
    }

    return true;
}
