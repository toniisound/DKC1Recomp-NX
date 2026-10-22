/* Overlay canvas for the Switch build. See switch_ui.h. */
#ifdef __SWITCH__

#include "switch_ui.h"

#include <string.h>

#include "switch_font8x8.h"
#include "switch_graphics.h"

static uint32_t s_canvas[kDkc1SwitchUiWidth * kDkc1SwitchUiHeight];
static SDL_Texture *s_texture;

bool Dkc1SwitchUiInit(SDL_Renderer *renderer) {
  /* OpenGL presenter: the canvas is blended by switch_graphics.c. */
  if (Dkc1SwitchGraphicsActive()) return true;
  if (s_texture) return true;
  if (!renderer) return false;
  s_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                kDkc1SwitchUiWidth, kDkc1SwitchUiHeight);
  if (!s_texture) return false;
  SDL_SetTextureBlendMode(s_texture, SDL_BLENDMODE_BLEND);
  /* Crisp glyph edges at any output size. */
  (void)SDL_SetTextureScaleMode(s_texture, SDL_ScaleModeNearest);
  Dkc1SwitchUiClear();
  return true;
}

void Dkc1SwitchUiShutdown(void) {
  if (s_texture) SDL_DestroyTexture(s_texture);
  s_texture = NULL;
}

void Dkc1SwitchUiClear(void) { memset(s_canvas, 0, sizeof s_canvas); }

void Dkc1SwitchUiFill(int x, int y, int w, int h, uint32_t argb) {
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w > kDkc1SwitchUiWidth ? kDkc1SwitchUiWidth : x + w;
  int y1 = y + h > kDkc1SwitchUiHeight ? kDkc1SwitchUiHeight : y + h;
  for (int row = y0; row < y1; row++) {
    uint32_t *line = s_canvas + (size_t)row * kDkc1SwitchUiWidth;
    for (int col = x0; col < x1; col++) line[col] = argb;
  }
}

int Dkc1SwitchUiTextWidth(const char *text) {
  return text ? (int)strlen(text) * kDkc1SwitchUiGlyph : 0;
}

static void DrawGlyph(int x, int y, unsigned char c, uint32_t argb) {
  if (c < 0x20 || c > 0x7E) c = '?';
  const uint8_t *rows = kDkc1Font8x8[c - 0x20];
  for (int ry = 0; ry < 8; ry++) {
    const int py = y + ry;
    if (py < 0 || py >= kDkc1SwitchUiHeight) continue;
    uint32_t *line = s_canvas + (size_t)py * kDkc1SwitchUiWidth;
    for (int rx = 0; rx < 8; rx++) {
      const int px = x + rx;
      if (px < 0 || px >= kDkc1SwitchUiWidth) continue;
      if (rows[ry] & (1u << rx)) line[px] = argb;
    }
  }
}

static void DrawString(int x, int y, const char *text, uint32_t argb) {
  for (; text && *text; text++, x += kDkc1SwitchUiGlyph)
    DrawGlyph(x, y, (unsigned char)*text, argb);
}

void Dkc1SwitchUiText(int x, int y, const char *text, uint32_t argb) {
  DrawString(x + 1, y + 1, text, 0xFF000000u);
  DrawString(x, y, text, argb);
}

void Dkc1SwitchUiTextCentered(int y, const char *text, uint32_t argb) {
  Dkc1SwitchUiText((kDkc1SwitchUiWidth - Dkc1SwitchUiTextWidth(text)) / 2,
                   y, text, argb);
}

void Dkc1SwitchUiPresent(SDL_Renderer *renderer) {
  if (Dkc1SwitchGraphicsActive()) {
    Dkc1SwitchGraphicsOverlay(s_canvas, kDkc1SwitchUiWidth,
                              kDkc1SwitchUiHeight);
    return;
  }
  if (!s_texture || !renderer) return;
  SDL_UpdateTexture(s_texture, NULL, s_canvas,
                    kDkc1SwitchUiWidth * (int)sizeof(uint32_t));
  /* Full output, independent of the game's letterbox viewport. */
  SDL_RenderCopy(renderer, s_texture, NULL, NULL);
}

#endif /* __SWITCH__ */
