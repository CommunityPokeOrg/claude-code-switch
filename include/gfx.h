#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Portable drawing layer over SDL2 + SDL_ttf.
// Composes into an ARGB8888 software surface and presents through an SDL
// renderer. Compiles for __SWITCH__ (libnx NRO) and desktop SDL2 alike.

typedef uint32_t GfxColor; // 0xAARRGGBB

#define GFX_RGB(r, g, b)  ((GfxColor)(0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define GFX_RGBA(r, g, b, a) ((GfxColor)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

typedef struct {
    int x, y, w, h;
} GfxRect;

typedef enum {
    GFX_FONT_REGULAR = 0,
    GFX_FONT_SEMIBOLD,
    GFX_FONT_COUNT
} GfxFontId;

// SDL_Surface pointer, opaque here to keep the header SDL-free.
typedef struct SDL_Surface GfxSurface;

bool gfx_init(void);
void gfx_quit(void);

void gfx_get_size(int *w, int *h);

// Per-frame lifecycle: gfx_begin() clears nothing; draw calls follow;
// gfx_present() uploads the composed surface.
void gfx_begin(void);
void gfx_present(void);

// The composed frame surface (valid until the next gfx_begin). Mainly for
// host-side screenshot tooling.
GfxSurface *gfx_frame_surface(void);

// Clipping (screen-space rect). gfx_clip_reset() restores full surface.
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_reset(void);
GfxRect gfx_clip_get(void);

// Primitives
void gfx_fill(int x, int y, int w, int h, GfxColor c);
void gfx_fill_grad_v(int x, int y, int w, int h, GfxColor top, GfxColor bottom);
void gfx_roundrect(int x, int y, int w, int h, int r, GfxColor c);
void gfx_roundrect_stroke(int x, int y, int w, int h, int r, int bw, GfxColor c);
void gfx_circle(int cx, int cy, int r, GfxColor c);
void gfx_hline(int x, int y, int w, GfxColor c);

// Text: draws at (x,y) top-left. Sizes are pixels.
void gfx_text(int x, int y, GfxFontId font, int size, GfxColor c,
              const char *str);
void gfx_measure(GfxFontId font, int size, const char *str, int *w, int *h);
int  gfx_line_height(GfxFontId font, int size);

// Word-wrapped paragraph rendered once into a surface (caller frees with
// gfx_surface_free). wrap_w=0 means no wrap. Alpha is per-pixel.
GfxSurface *gfx_render_text(GfxFontId font, int size, GfxColor c,
                            const char *str, int wrap_w);
int  gfx_surface_w(const GfxSurface *s);
int  gfx_surface_h(const GfxSurface *s);
void gfx_surface_free(GfxSurface *s);

// Blit a surface at (x,y) honoring the clip rect.
void gfx_blit(const GfxSurface *s, int x, int y);
// Blit with extra alpha modulation 0..255 (for fades).
void gfx_blit_alpha(const GfxSurface *s, int x, int y, uint8_t alpha);
