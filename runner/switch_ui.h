/* Tiny overlay canvas for the Switch build: pause menu and toasts.
 *
 * A fixed 16:9 ARGB canvas (kDkc1SwitchUiWidth x kDkc1SwitchUiHeight) is
 * drawn in software with an embedded 8x8 bitmap font, uploaded to one
 * streaming texture and stretched over the whole output with alpha
 * blending. No font files, no SDL_ttf. Only compiled on __SWITCH__. */
#ifndef DKC1_SWITCH_UI_H
#define DKC1_SWITCH_UI_H
#ifdef __SWITCH__

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

enum {
  kDkc1SwitchUiWidth = 426,
  kDkc1SwitchUiHeight = 240,
  kDkc1SwitchUiGlyph = 8,
};

bool Dkc1SwitchUiInit(SDL_Renderer *renderer);
void Dkc1SwitchUiShutdown(void);

/* Canvas drawing (software, ARGB8888 with straight alpha). */
void Dkc1SwitchUiClear(void);
void Dkc1SwitchUiFill(int x, int y, int w, int h, uint32_t argb);
int Dkc1SwitchUiTextWidth(const char *text);
/* Draws ASCII text with a 1-pixel drop shadow. */
void Dkc1SwitchUiText(int x, int y, const char *text, uint32_t argb);
void Dkc1SwitchUiTextCentered(int y, const char *text, uint32_t argb);

/* Uploads the canvas and draws it over the full render target. Call after
 * the game frame was copied and before SDL_RenderPresent. */
void Dkc1SwitchUiPresent(SDL_Renderer *renderer);

#endif /* __SWITCH__ */
#endif
