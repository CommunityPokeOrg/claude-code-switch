#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

#ifdef __SWITCH__
#define GFX_RES_PATH(p) "romfs:/" p
#else
#define GFX_RES_PATH(p) "romfs/" p
#endif

#define FONT_FILE_REGULAR  GFX_RES_PATH("Inter-Regular.ttf")
#define FONT_FILE_SEMIBOLD GFX_RES_PATH("Inter-SemiBold.ttf")

#define FONT_CACHE_MAX 12
#define CORNER_CACHE_MAX 8

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static SDL_Surface  *g_surf;     // ARGB8888 composition surface
static uint32_t     *g_px;
static int g_w, g_h, g_pitch;    // pitch in pixels
static GfxRect g_clip;

typedef struct {
    TTF_Font *font;
    GfxFontId id;
    int size;
} FontEnt;
static FontEnt g_fonts[FONT_CACHE_MAX];
static int g_font_count;

// One corner alpha-map (r x r, top-left quarter disk) per radius, cached.
typedef struct {
    int r;
    uint8_t *alpha; // r*r coverage values
} CornerEnt;
static CornerEnt g_corners[CORNER_CACHE_MAX];
static int g_corner_count;

static const char *font_file(GfxFontId id) {
    return id == GFX_FONT_SEMIBOLD ? FONT_FILE_SEMIBOLD : FONT_FILE_REGULAR;
}

static TTF_Font *get_font(GfxFontId id, int size) {
    for (int i = 0; i < g_font_count; i++)
        if (g_fonts[i].id == id && g_fonts[i].size == size)
            return g_fonts[i].font;
    if (g_font_count == FONT_CACHE_MAX)
        return g_fonts[0].font; // should not happen
    TTF_Font *f = TTF_OpenFont(font_file(id), size);
    if (!f) return NULL;
    g_fonts[g_font_count].font = f;
    g_fonts[g_font_count].id = id;
    g_fonts[g_font_count].size = size;
    g_font_count++;
    return f;
}

static uint8_t *get_corner(int r) {
    for (int i = 0; i < g_corner_count; i++)
        if (g_corners[i].r == r)
            return g_corners[i].alpha;
    if (g_corner_count == CORNER_CACHE_MAX || r <= 0)
        return NULL;
    uint8_t *a = malloc((size_t)r * r);
    if (!a) return NULL;
    // Coverage of a unit disk of radius r centered at (r, r) sampled at
    // pixel centers -> smooth AA edge.
    for (int y = 0; y < r; y++) {
        for (int x = 0; x < r; x++) {
            double dx = (x + 0.5) - r;
            double dy = (y + 0.5) - r;
            double d = sqrt(dx * dx + dy * dy);
            double cov = (double)r + 0.5 - d;
            if (cov > 1.0) cov = 1.0;
            if (cov < 0.0) cov = 0.0;
            a[y * r + x] = (uint8_t)(cov * 255.0 + 0.5);
        }
    }
    g_corners[g_corner_count].r = r;
    g_corners[g_corner_count].alpha = a;
    g_corner_count++;
    return a;
}

bool gfx_init(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return false;
    }

#ifdef __SWITCH__
    Uint32 win_flags = SDL_WINDOW_FULLSCREEN;
    int req_w = 0, req_h = 0;
#else
    Uint32 win_flags = 0;
    int req_w = 1280, req_h = 720;
#endif

    g_win = SDL_CreateWindow("Claude Code Switch",
                             SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             req_w, req_h, win_flags);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    g_ren = SDL_CreateRenderer(g_win, -1,
                               SDL_RENDERER_ACCELERATED |
                               SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) {
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }

    SDL_GetWindowSize(g_win, &g_w, &g_h);
    if (g_w <= 0) g_w = 1280;
    if (g_h <= 0) g_h = 720;

    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, g_w, g_h);
    if (!g_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return false;
    }

    g_surf = SDL_CreateRGBSurfaceWithFormat(0, g_w, g_h, 32,
                                          SDL_PIXELFORMAT_ARGB8888);
    if (!g_surf) {
        fprintf(stderr, "SDL_CreateRGBSurface: %s\n", SDL_GetError());
        return false;
    }
    g_px = (uint32_t *)g_surf->pixels;
    g_pitch = g_surf->pitch / 4;
    g_clip = (GfxRect){0, 0, g_w, g_h};

    // Warm the font cache.
    if (!get_font(GFX_FONT_REGULAR, 22)) {
        fprintf(stderr, "font load failed: %s\n", TTF_GetError());
        return false;
    }
    get_font(GFX_FONT_SEMIBOLD, 22);
    return true;
}

void gfx_quit(void) {
    for (int i = 0; i < g_font_count; i++)
        TTF_CloseFont(g_fonts[i].font);
    g_font_count = 0;
    for (int i = 0; i < g_corner_count; i++)
        free(g_corners[i].alpha);
    g_corner_count = 0;
    if (g_surf) SDL_FreeSurface(g_surf);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    TTF_Quit();
    SDL_Quit();
}

void gfx_get_size(int *w, int *h) { *w = g_w; *h = g_h; }

GfxSurface *gfx_frame_surface(void) { return (GfxSurface *)g_surf; }

void gfx_begin(void) { g_clip = (GfxRect){0, 0, g_w, g_h}; }

void gfx_present(void) {
    SDL_UpdateTexture(g_tex, NULL, g_surf->pixels, g_surf->pitch);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

void gfx_clip(int x, int y, int w, int h) {
    GfxRect n = {x, y, w, h};
    // Intersect with current clip.
    int x1 = n.x > g_clip.x ? n.x : g_clip.x;
    int y1 = n.y > g_clip.y ? n.y : g_clip.y;
    int x2 = n.x + n.w < g_clip.x + g_clip.w ? n.x + n.w : g_clip.x + g_clip.w;
    int y2 = n.y + n.h < g_clip.y + g_clip.h ? n.y + n.h : g_clip.y + g_clip.h;
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    g_clip = (GfxRect){x1, y1, x2 - x1, y2 - y1};
}

void gfx_clip_reset(void) { g_clip = (GfxRect){0, 0, g_w, g_h}; }

GfxRect gfx_clip_get(void) { return g_clip; }

static inline void blend_px(int x, int y, GfxColor c, uint8_t a) {
    if (a == 0) return;
    uint32_t *p = g_px + y * g_pitch + x;
    uint32_t dst = *p;
    uint32_t sa = a;
    uint32_t da = 255 - sa;
    uint32_t sr = (c >> 16) & 0xFF, sg = (c >> 8) & 0xFF, sb = c & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    *p = 0xFF000000u |
         ((sr * sa + dr * da) / 255 << 16) |
         ((sg * sa + dg * da) / 255 << 8) |
         ((sb * sa + db * da) / 255);
}

void gfx_fill(int x, int y, int w, int h, GfxColor c) {
    int x1 = x > g_clip.x ? x : g_clip.x;
    int y1 = y > g_clip.y ? y : g_clip.y;
    int x2 = x + w < g_clip.x + g_clip.w ? x + w : g_clip.x + g_clip.w;
    int y2 = y + h < g_clip.y + g_clip.h ? y + h : g_clip.y + g_clip.h;
    if (x2 <= x1 || y2 <= y1) return;
    uint8_t a = (uint8_t)(c >> 24);
    if (a == 255) {
        for (int yy = y1; yy < y2; yy++) {
            uint32_t *row = g_px + yy * g_pitch + x1;
            for (int xx = 0; xx < x2 - x1; xx++) row[xx] = c;
        }
    } else {
        for (int yy = y1; yy < y2; yy++)
            for (int xx = x1; xx < x2; xx++) blend_px(xx, yy, c, a);
    }
}

void gfx_fill_grad_v(int x, int y, int w, int h, GfxColor top, GfxColor bot) {
    int y1 = y > g_clip.y ? y : g_clip.y;
    int y2 = y + h < g_clip.y + g_clip.h ? y + h : g_clip.y + g_clip.h;
    if (y2 <= y1) return;
    int tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF;
    int br = (bot >> 16) & 0xFF, bg = (bot >> 8) & 0xFF, bb = bot & 0xFF;
    for (int yy = y1; yy < y2; yy++) {
        float t = h > 1 ? (float)(yy - y) / (float)(h - 1) : 0;
        GfxColor c = 0xFF000000u |
            ((uint32_t)(tr + (int)((br - tr) * t)) << 16) |
            ((uint32_t)(tg + (int)((bg - tg) * t)) << 8) |
            (uint32_t)(tb + (int)((bb - tb) * t));
        int x1 = x > g_clip.x ? x : g_clip.x;
        int x2 = x + w < g_clip.x + g_clip.w ? x + w : g_clip.x + g_clip.w;
        if (x2 <= x1) continue;
        uint32_t *row = g_px + yy * g_pitch + x1;
        for (int xx = 0; xx < x2 - x1; xx++) row[xx] = c;
    }
}

void gfx_hline(int x, int y, int w, GfxColor c) { gfx_fill(x, y, w, 1, c); }

// Blit an r x r alpha map at (dx,dy); flip_x/flip_y mirror it for the other
// three corners.
static void blit_corner(const uint8_t *map, int r, int dx, int dy,
                        bool flip_x, bool flip_y, GfxColor c) {
    uint8_t ca = (uint8_t)(c >> 24);
    for (int y = 0; y < r; y++) {
        int sy = flip_y ? (r - 1 - y) : y;
        int py = dy + y;
        if (py < g_clip.y || py >= g_clip.y + g_clip.h) continue;
        for (int x = 0; x < r; x++) {
            int sx = flip_x ? (r - 1 - x) : x;
            int px = dx + x;
            if (px < g_clip.x || px >= g_clip.x + g_clip.w) continue;
            uint32_t a = ((uint32_t)map[sy * r + sx] * ca) / 255;
            if (a) blend_px(px, py, c, (uint8_t)a);
        }
    }
}

void gfx_roundrect(int x, int y, int w, int h, int r, GfxColor c) {
    if (w <= 0 || h <= 0) return;
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    if (r <= 0) { gfx_fill(x, y, w, h, c); return; }
    const uint8_t *map = get_corner(r);
    if (!map) { gfx_fill(x, y, w, h, c); return; }

    gfx_fill(x + r, y, w - 2 * r, h, c);
    gfx_fill(x, y + r, r, h - 2 * r, c);
    gfx_fill(x + w - r, y + r, r, h - 2 * r, c);
    blit_corner(map, r, x, y, false, false, c);              // TL
    blit_corner(map, r, x + w - r, y, true, false, c);       // TR
    blit_corner(map, r, x, y + h - r, false, true, c);       // BL
    blit_corner(map, r, x + w - r, y + h - r, true, true, c);// BR
}

void gfx_circle(int cx, int cy, int r, GfxColor c) {
    const uint8_t *map = get_corner(r);
    if (!map) {
        for (int yy = cy - r; yy < cy + r; yy++)
            for (int xx = cx - r; xx < cx + r; xx++)
                blend_px(xx, yy, c, (uint8_t)(c >> 24));
        return;
    }
    blit_corner(map, r, cx - r, cy - r, false, false, c);
    blit_corner(map, r, cx, cy - r, true, false, c);
    blit_corner(map, r, cx - r, cy, false, true, c);
    blit_corner(map, r, cx, cy, true, true, c);
}

void gfx_roundrect_stroke(int x, int y, int w, int h, int r, int bw,
                          GfxColor c) {
    // Stroke = four straight strips + four corner rings. Corner ring alpha
    // is outer-disk coverage minus inner-disk coverage, computed inline.
    if (w <= 0 || h <= 0 || bw <= 0) return;
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    gfx_fill(x + r, y, w - 2 * r, bw, c);
    gfx_fill(x + r, y + h - bw, w - 2 * r, bw, c);
    gfx_fill(x, y + r, bw, h - 2 * r, c);
    gfx_fill(x + w - bw, y + r, bw, h - 2 * r, c);
    if (r <= 0) return;

    uint8_t ca = (uint8_t)(c >> 24);
    double ro = r, ri = r - bw;
    // Each corner ring is a quarter-annulus: outer-disk coverage minus
    // inner-disk coverage. Quarter disk centers in tile-local coords:
    // TL (r,r)  TR (0,r)  BL (r,0)  BR (0,0).
    for (int corner = 0; corner < 4; corner++) {
        int ox = (corner == 1 || corner == 3) ? x + w - r : x;
        int oy = (corner >= 2) ? y + h - r : y;
        double ax = (corner == 1 || corner == 3) ? 0.0 : (double)r;
        double ay = (corner >= 2) ? 0.0 : (double)r;
        for (int yy = 0; yy < r; yy++) {
            int py = oy + yy;
            if (py < g_clip.y || py >= g_clip.y + g_clip.h) continue;
            for (int xx = 0; xx < r; xx++) {
                int px = ox + xx;
                if (px < g_clip.x || px >= g_clip.x + g_clip.w) continue;
                double dx = xx + 0.5 - ax, dy = yy + 0.5 - ay;
                double d = sqrt(dx * dx + dy * dy);
                double out_c = ro + 0.5 - d;
                double in_c = d - (ri - 0.5);
                double cov = out_c < in_c ? out_c : in_c;
                if (cov > 1.0) cov = 1.0;
                if (cov <= 0.0) continue;
                blend_px(px, py, c, (uint8_t)(cov * ca));
            }
        }
    }
}

void gfx_measure(GfxFontId font, int size, const char *str, int *w, int *h) {
    TTF_Font *f = get_font(font, size);
    if (!f || !str) { if (w) *w = 0; if (h) *h = 0; return; }
    TTF_SizeUTF8(f, str, w, h);
}

int gfx_line_height(GfxFontId font, int size) {
    TTF_Font *f = get_font(font, size);
    return f ? TTF_FontLineSkip(f) : size;
}

void gfx_text(int x, int y, GfxFontId font, int size, GfxColor c,
              const char *str) {
    if (!str || !*str) return;
    TTF_Font *f = get_font(font, size);
    if (!f) return;
    SDL_Color sc = {(uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 8) & 0xFF),
                    (uint8_t)(c & 0xFF), 255};
    SDL_Surface *t = TTF_RenderUTF8_Blended(f, str, sc);
    if (!t) return;
    gfx_blit_alpha((GfxSurface *)t, x, y, (uint8_t)(c >> 24));
    SDL_FreeSurface(t);
}

GfxSurface *gfx_render_text(GfxFontId font, int size, GfxColor c,
                            const char *str, int wrap_w) {
    if (!str) str = "";
    TTF_Font *f = get_font(font, size);
    if (!f) return NULL;
    SDL_Color sc = {(uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 8) & 0xFF),
                    (uint8_t)(c & 0xFF), 255};
    SDL_Surface *t;
    if (wrap_w > 0)
        t = TTF_RenderUTF8_Blended_Wrapped(f, str, sc, wrap_w);
    else
        t = TTF_RenderUTF8_Blended(f, str, sc);
    if (!t) {
        // Empty string: give back a 1px transparent surface so callers
        // can always blit safely.
        t = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32,
                                           SDL_PIXELFORMAT_ARGB8888);
    }
    return (GfxSurface *)t;
}

int gfx_surface_w(const GfxSurface *s) {
    return s ? ((const SDL_Surface *)s)->w : 0;
}
int gfx_surface_h(const GfxSurface *s) {
    return s ? ((const SDL_Surface *)s)->h : 0;
}
void gfx_surface_free(GfxSurface *s) {
    if (s) SDL_FreeSurface((SDL_Surface *)s);
}

void gfx_blit(const GfxSurface *s, int x, int y) {
    gfx_blit_alpha(s, x, y, 255);
}

void gfx_blit_alpha(const GfxSurface *s_, int x, int y, uint8_t alpha) {
    const SDL_Surface *s = (const SDL_Surface *)s_;
    if (!s || alpha == 0) return;
    if (SDL_MUSTLOCK((SDL_Surface *)s)) SDL_LockSurface((SDL_Surface *)s);

    int sw = s->w, sh = s->h;
    int dx1 = x > g_clip.x ? x : g_clip.x;
    int dy1 = y > g_clip.y ? y : g_clip.y;
    int dx2 = x + sw < g_clip.x + g_clip.w ? x + sw : g_clip.x + g_clip.w;
    int dy2 = y + sh < g_clip.y + g_clip.h ? y + sh : g_clip.y + g_clip.h;

    if (dx2 > dx1 && dy2 > dy1) {
        const uint8_t *src = (const uint8_t *)s->pixels;
        int bpp = s->format->BytesPerPixel;
        for (int yy = dy1; yy < dy2; yy++) {
            int sy = yy - y;
            const uint8_t *srow = src + sy * s->pitch;
            for (int xx = dx1; xx < dx2; xx++) {
                int sx = xx - x;
                const uint8_t *sp = srow + sx * bpp;
                uint32_t sa, sr, sg, sb;
                if (bpp == 4) {
                    uint32_t v = *(const uint32_t *)sp;
                    if (s->format->Amask) {
                        sa = (v >> 24) & 0xFF;
                        sr = (v >> 16) & 0xFF;
                        sg = (v >> 8) & 0xFF;
                        sb = v & 0xFF;
                    } else {
                        // paletted/other 32bpp path unlikely; treat as opaque
                        sa = 255; sr = (v >> 16) & 0xFF;
                        sg = (v >> 8) & 0xFF; sb = v & 0xFF;
                    }
                } else {
                    // Fallback: use SDL_GetRGBA for odd formats.
                    uint32_t v = 0;
                    memcpy(&v, sp, bpp < 4 ? bpp : 4);
                    uint8_t r8, g8, b8, a8;
                    SDL_GetRGBA(v, s->format, &r8, &g8, &b8, &a8);
                    sa = a8; sr = r8; sg = g8; sb = b8;
                }
                if (sa == 0) continue;
                sa = (sa * alpha) / 255;
                if (!sa) continue;
                blend_px(xx, yy,
                         0xFF000000u | (sr << 16) | (sg << 8) | sb,
                         (uint8_t)sa);
            }
        }
    }
    if (SDL_MUSTLOCK((SDL_Surface *)s)) SDL_UnlockSurface((SDL_Surface *)s);
}
