#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#if defined(__APPLE__)
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#include <Cocoa/Cocoa.h>
#endif

extern "C"
{
#if defined(__APPLE__) || defined(WGPU_NATIVE)
#include "webgpu.h"
#else
#include <webgpu/webgpu.h>
#endif
}

#if defined(__APPLE__)
static SDL_MetalView gMetalView = NULL;

void GFXSetCocoaWindow(SDL_Window* window, WGPUSurfaceSourceMetalLayer* s)
{
   if (gMetalView == NULL)
   {
      gMetalView = SDL_Metal_CreateView(window);
   }

   CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(gMetalView);
   NSWindow* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
   if (metalLayer && nsWindow)
   {
      metalLayer.contentsScale = nsWindow.backingScaleFactor;
      metalLayer.opaque = YES;
      metalLayer.framebufferOnly = YES;
      metalLayer.presentsWithTransaction = NO;
   }

   s->chain.next = 0;
   s->chain.sType = WGPUSType_SurfaceSourceMetalLayer;
   s->layer = metalLayer;
}

void GFXClearCocoaWindow()
{
   if (gMetalView != NULL)
   {
      SDL_Metal_DestroyView(gMetalView);
      gMetalView = NULL;
   }
}
#endif
