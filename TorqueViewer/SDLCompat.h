#pragma once

#if defined(EMSCRIPTEN_BUILD)
#if defined(EMSCRIPTEN_USE_SDL3)
#include <SDL3/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include <SDL3/SDL.h>
#endif
