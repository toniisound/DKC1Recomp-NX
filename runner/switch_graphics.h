/* OpenGL presenter for the Switch build (Mesa desktop GL 3.3 core via the
 * devkitPro SDL2 EGL backend). Provides the flat upscalers and the HD
 * "Reconstruct" upscaler shared with the Windows/Mac builds. If it cannot
 * start, the host falls back to the plain SDL renderer. */
#ifndef DKC1_SWITCH_GRAPHICS_H
#define DKC1_SWITCH_GRAPHICS_H
#ifdef __SWITCH__

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "desktop_graphics.h"

/* Call before SDL_CreateWindow: requests a GL 3.3 core context. */
void Dkc1SwitchGraphicsPrepareAttributes(void);
/* Restores SDL's default (GLES2) attributes for the SDL renderer fallback. */
void Dkc1SwitchGraphicsResetAttributes(void);
/* Creates the context, compiles the shaders, enables vsync. */
bool Dkc1SwitchGraphicsInit(SDL_Window *window);
bool Dkc1SwitchGraphicsActive(void);
bool Dkc1SwitchGraphicsVsync(void);
/* Draws one game frame (ARGB8888 pixels, w x h) fitted to the drawable with
 * the pixel aspect given by display_width. */
void Dkc1SwitchGraphicsDraw(const uint32_t *pixels, int w, int h,
                            int display_width,
                            const Dkc1GraphicsSettings *settings);
/* Alpha-blends an ARGB8888 overlay over the whole drawable. */
void Dkc1SwitchGraphicsOverlay(const uint32_t *argb, int w, int h);
void Dkc1SwitchGraphicsSwap(void);
void Dkc1SwitchGraphicsClose(void);

#endif /* __SWITCH__ */
#endif
