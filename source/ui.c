#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"

// ---------------------------------------------------------------
// Theme (hbmenu-style dark, warm accent)
// ---------------------------------------------------------------

#define C_BG_TOP    GFX_RGB(0x15, 0x16, 0x1B)
#define C_BG_BOT    GFX_RGB(0x1D, 0x1F, 0x27)
#define C_HEADER    GFX_RGB(0x20, 0x22, 0x2B)
#define C_CARD      GFX_RGB(0x28, 0x2B, 0x35)
#define C_CARD_HI   GFX_RGB(0x31, 0x34, 0x40)
#define C_FIELD     GFX_RGB(0x22, 0x24, 0x2D)
#define C_ACCENT    GFX_RGB(0xD9, 0x77, 0x57)
#define C_ACCENT_DK GFX_RGB(0xB8, 0x5C, 0x40)
#define C_TEXT      GFX_RGB(0xF2, 0xEF, 0xEA)
#define C_DIM       GFX_RGB(0x9A, 0x9D, 0xAA)
#define C_FAINT     GFX_RGB(0x62, 0x65, 0x72)
#define C_OK        GFX_RGB(0x6E, 0xC4, 0x7E)
#define C_ERR_TXT   GFX_RGB(0xEF, 0x8B, 0x8B)
#define C_ERR_BG    GFX_RGBA(0xC0, 0x4A, 0x4A, 0x3C)
#define C_SYS_BG    GFX_RGBA(0xFF, 0xFF, 0xFF, 0x14)
#define C_DIVIDER   GFX_RGB(0x33, 0x36, 0x42)
#define C_SCRIM     GFX_RGBA(0x00, 0x00, 0x00, 0x60)

#define HEADER_H     72
#define HINT_H       44
#define COMPOSE_H    88
#define CHAT_PAD_X   24
#define BUBBLE_R     20
#define BUBBLE_PAD_X 18
#define BUBBLE_PAD_Y 12
#define BUBBLE_GAP   16
#define LABEL_H      26
#define MAX_ITEMS    140
#define TAP_SLOP     14

typedef struct {
    UiLineKind kind;
    char *text;
    GfxSurface *surf;   // rendered wrapped text
    int w, h;           // surface dims
    int bubble_w, bubble_h;
} UiItem;

static UiItem *items_of(UiApp *u) { return (UiItem *)u->items; }

// ---------------------------------------------------------------
// Small vector icon helpers
// ---------------------------------------------------------------

// Filled triangle, (x1,y1) apex / two base points — used for arrows.
static void draw_tri(int x0, int y0, int x1, int y1, int x2, int y2,
                     GfxColor c) {
    // Sort vertices by y.
    int ax = x0, ay = y0, bx = x1, by = y1, cx = x2, cy = y2;
    if (ay > by) { int t; t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    if (ay > cy) { int t; t = ax; ax = cx; cx = t; t = ay; ay = cy; cy = t; }
    if (by > cy) { int t; t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; }
    for (int y = ay; y <= cy; y++) {
        int xa, xb;
        if (y < by) {
            float t = by == ay ? 0 : (float)(y - ay) / (by - ay);
            xa = ax + (int)((bx - ax) * t);
        } else {
            float t = cy == by ? 1 : (float)(y - by) / (cy - by);
            xa = bx + (int)((cx - bx) * t);
        }
        float t2 = cy == ay ? 1 : (float)(y - ay) / (cy - ay);
        xb = ax + (int)((cx - ax) * t2);
        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        gfx_fill(xa, y, xb - xa + 1, 1, c);
    }
}

static void draw_arrow_right(int cx, int cy, int s, GfxColor c) {
    draw_tri(cx - s / 2, cy - s, cx + s, cy, cx - s / 2, cy + s, c);
    // stem
    gfx_fill(cx - s - s / 2, cy - s / 4, s, s / 2 + 1, c);
}

static void draw_chevron_left(int cx, int cy, int s, GfxColor c) {
    // "<" made of two slanted bars approximated with triangles
    draw_tri(cx - s / 2, cy, cx + s / 2, cy - s, cx + s / 2, cy - s + 8, c);
    draw_tri(cx - s / 2, cy, cx + s / 2, cy + s, cx + s / 2, cy + s - 8, c);
}

static void draw_icon_gear(int cx, int cy, GfxColor c) {
    // Sliders icon: three rails + knobs.
    for (int i = -1; i <= 1; i++) {
        int y = cy + i * 9;
        gfx_fill(cx - 11, y - 1, 22, 3, c);
    }
    gfx_circle(cx - 4, cy - 9, 4, c);
    gfx_circle(cx + 5, cy, 4, c);
    gfx_circle(cx - 2, cy + 9, 4, c);
}

static void draw_icon_trash(int cx, int cy, GfxColor c) {
    gfx_fill(cx - 10, cy - 10, 20, 3, c);        // lid
    gfx_fill(cx - 4, cy - 13, 8, 3, c);         // lid handle
    gfx_fill(cx - 8, cy - 6, 16, 16, c);        // body
    gfx_fill(cx - 5, cy - 3, 2, 10, C_CARD);    // body grooves
    gfx_fill(cx + 3, cy - 3, 2, 10, C_CARD);
}

static void draw_icon_power(int cx, int cy, GfxColor c) {
    gfx_circle(cx, cy + 2, 11, c);
    gfx_circle(cx, cy + 2, 8, C_CARD);
    gfx_fill(cx - 1, cy - 11, 3, 12, c);
}

// ---------------------------------------------------------------
// Chat log
// ---------------------------------------------------------------

void ui_init(UiApp *u) {
    memset(u, 0, sizeof(*u));
    u->items = calloc(MAX_ITEMS, sizeof(UiItem));
    u->focus_row = 2;
    u->focus_col = 0;
    u->focus_visible = false;
    u->at_bottom = true;
}

void ui_quit(UiApp *u) {
    if (!u->items) return;
    UiItem *items = items_of(u);
    for (int i = 0; i < u->item_count; i++) {
        free(items[i].text);
        gfx_surface_free(items[i].surf);
    }
    free(u->items);
    u->items = NULL;
}

static void free_item(UiItem *it) {
    free(it->text);
    gfx_surface_free(it->surf);
    memset(it, 0, sizeof(*it));
}

void ui_set_context(UiApp *u, const char *model, bool have_api_key) {
    if (model) snprintf(u->model, sizeof(u->model), "%s", model);
    u->have_api_key = have_api_key;
}

void ui_set_requesting(UiApp *u, bool r) { u->requesting = r; }

static void render_item_text(UiItem *it, int wrap_w) {
    gfx_surface_free(it->surf);
    GfxColor col = C_TEXT;
    int wrap = wrap_w;
    switch (it->kind) {
        case UI_LINE_SYS:
        case UI_LINE_DIM:  col = C_DIM; break;
        case UI_LINE_ERROR: col = C_ERR_TXT; break;
        default: break;
    }
    it->surf = gfx_render_text(GFX_FONT_REGULAR, 22, col,
                               it->text, wrap);
    it->w = gfx_surface_w(it->surf);
    it->h = gfx_surface_h(it->surf);
    int bw = it->w + BUBBLE_PAD_X * 2;
    int bh = it->h + BUBBLE_PAD_Y * 2;
    it->bubble_w = bw < 56 ? 56 : bw;
    it->bubble_h = bh < 46 ? 46 : bh;
}

void ui_chat_add(UiApp *u, UiLineKind kind, const char *text) {
    if (!u->items) return;
    UiItem *items = items_of(u);
    if (u->item_count == MAX_ITEMS) {
        free_item(&items[0]);
        memmove(items, items + 1, (MAX_ITEMS - 1) * sizeof(UiItem));
        u->item_count--;
    }
    UiItem *it = &items[u->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->text = strdup(text ? text : "");
    int g_w, g_h;
    gfx_get_size(&g_w, &g_h);
    int wrap = (int)((g_w - CHAT_PAD_X * 2) * 0.70f) - BUBBLE_PAD_X * 2;
    render_item_text(it, wrap);
    if (u->at_bottom) u->scroll_off = 0;
}

void ui_chat_add_fmt(UiApp *u, UiLineKind kind, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_chat_add(u, kind, buf);
}

void ui_chat_clear(UiApp *u) {
    UiItem *items = items_of(u);
    for (int i = 0; i < u->item_count; i++) free_item(&items[i]);
    u->item_count = 0;
    u->scroll_off = 0;
    u->at_bottom = true;
}

const char *ui_compose(const UiApp *u) { return u->compose; }
void ui_compose_clear(UiApp *u) { u->compose[0] = 0; }
void ui_compose_set(UiApp *u, const char *t) {
    snprintf(u->compose, sizeof(u->compose), "%s", t ? t : "");
}

Settings *ui_settings_edit(UiApp *u) { return &u->edit; }
void ui_settings_load(UiApp *u, const Settings *s) { u->edit = *s; }
void ui_open_settings(UiApp *u) { u->screen = 1; u->set_sel = 0; u->edit_loaded = true; }

void ui_toast(UiApp *u, const char *text) {
    snprintf(u->toast, sizeof(u->toast), "%s", text);
    u->toast_frames = 150;
}

// ---------------------------------------------------------------
// Layout
// ---------------------------------------------------------------

typedef struct {
    GfxRect chat;
    GfxRect field, send;
    GfxRect b_clear, b_settings, b_quit;
    GfxRect status;
    int chat_bottom;
} Layout;

static Layout compute_layout(void) {
    Layout l;
    int w, h;
    gfx_get_size(&w, &h);
    l.chat = (GfxRect){CHAT_PAD_X, HEADER_H, w - CHAT_PAD_X * 2,
                       h - HEADER_H - COMPOSE_H - HINT_H};
    l.chat_bottom = l.chat.y + l.chat.h;

    int by = h - COMPOSE_H - HINT_H + (COMPOSE_H - 56) / 2;
    l.send = (GfxRect){w - CHAT_PAD_X - 150, by, 150, 56};
    l.field = (GfxRect){CHAT_PAD_X, by, w - CHAT_PAD_X * 2 - 150 - 14, 56};

    int bw = 52, gap = 10, x = w - CHAT_PAD_X - bw;
    l.b_quit = (GfxRect){x, 10, bw, 52}; x -= bw + gap;
    l.b_settings = (GfxRect){x, 10, bw, 52}; x -= bw + gap;
    l.b_clear = (GfxRect){x, 10, bw, 52};
    l.status = (GfxRect){x - 130 - gap, 18, 130, 36};
    return l;
}

static int content_height(UiApp *u) {
    UiItem *items = items_of(u);
    int total = 0;
    for (int i = 0; i < u->item_count; i++) {
        bool labeled = items[i].kind == UI_LINE_USER ||
                       items[i].kind == UI_LINE_ASSISTANT;
        total += items[i].bubble_h + BUBBLE_GAP + (labeled ? LABEL_H : 0);
    }
    if (u->requesting) total += 46 + BUBBLE_GAP + LABEL_H;
    return total;
}

static float max_scroll(UiApp *u) {
    int w, h;
    gfx_get_size(&w, &h);
    Layout l = compute_layout();
    float m = (float)(content_height(u) - l.chat.h + 16);
    return m > 0 ? m : 0;
}

static void clamp_scroll(UiApp *u) {
    float m = max_scroll(u);
    if (u->scroll_off < 0) u->scroll_off = 0;
    if (u->scroll_off > m) u->scroll_off = m;
    u->at_bottom = u->scroll_off < 8;
}

// ---------------------------------------------------------------
// Icon buttons / focus
// ---------------------------------------------------------------

// focus ids: chat screen
enum { F_FIELD = 0, F_SEND, F_CLEAR, F_SETTINGS, F_QUIT, F_CHAT_COUNT };

static void draw_icon_button(GfxRect r, int icon, bool focused,
                             UiApp *u) {
    (void)u;
    gfx_roundrect(r.x, r.y, r.w, r.h, 16, C_CARD);
    if (focused)
        gfx_roundrect_stroke(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 18, 3,
                             C_ACCENT);
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    switch (icon) {
        case 0: draw_icon_trash(cx, cy, C_DIM); break;
        case 1: draw_icon_gear(cx, cy, C_DIM); break;
        case 2: draw_icon_power(cx, cy, C_DIM); break;
    }
}

// ---------------------------------------------------------------
// Chat screen render
// ---------------------------------------------------------------

static void draw_header(UiApp *u, Layout *l) {
    int w, h;
    gfx_get_size(&w, &h);
    gfx_fill(0, 0, w, HEADER_H, C_HEADER);
    gfx_hline(0, HEADER_H - 1, w, C_DIVIDER);

    gfx_text(28, 21, GFX_FONT_SEMIBOLD, 28, C_TEXT, "Claude");
    int tw, sw;
    gfx_measure(GFX_FONT_SEMIBOLD, 28, "Claude", &tw, NULL);
    gfx_measure(GFX_FONT_REGULAR, 20, "Code Switch", &sw, NULL);
    gfx_text(28 + tw + 8, 26, GFX_FONT_REGULAR, 20, C_DIM, "Code Switch");

    // model chip
    char chip[80];
    snprintf(chip, sizeof(chip), "%s", u->model[0] ? u->model : "no model");
    int cw;
    gfx_measure(GFX_FONT_REGULAR, 17, chip, &cw, NULL);
    int cx = 28 + tw + 8 + sw + 14;
    gfx_roundrect(cx, 22, cw + 24, 30, 15, C_FIELD);
    gfx_text(cx + 12, 28, GFX_FONT_REGULAR, 17, C_DIM, chip);

    // status pill
    GfxRect s = l->status;
    const char *txt = u->requesting ? "Thinking" : "Ready";
    GfxColor dot = u->requesting ? C_ACCENT : C_OK;
    gfx_roundrect(s.x, s.y, s.w, s.h, s.h / 2, C_FIELD);
    gfx_circle(s.x + 20, s.y + s.h / 2, 5, dot);
    gfx_text(s.x + 34, s.y + 8, GFX_FONT_REGULAR, 18, C_DIM, txt);
    if (u->requesting) {
        // animated dots after "Thinking"
        int tw2;
        gfx_measure(GFX_FONT_REGULAR, 18, "Thinking", &tw2, NULL);
        char dots[4] = "...";
        dots[(u->frame / 18) % 4] = 0;
        if (u->frame % 72 < 54)
            gfx_text(s.x + 34 + tw2, s.y + 8, GFX_FONT_REGULAR, 18, C_DIM,
                     dots);
    }

    draw_icon_button(l->b_clear, 0, u->focus_visible &&
                     u->focus_col == F_CLEAR, u);
    draw_icon_button(l->b_settings, 1, u->focus_visible &&
                     u->focus_col == F_SETTINGS, u);
    draw_icon_button(l->b_quit, 2, u->focus_visible &&
                     u->focus_col == F_QUIT, u);
}

static void draw_empty_state(UiApp *u) {
    int w, h;
    gfx_get_size(&w, &h);
    int cy = HEADER_H + (h - HEADER_H - COMPOSE_H - HINT_H) / 2;
    int cw = 560, ch = u->have_api_key ? 190 : 250;
    int cx = (w - cw) / 2, cyy = cy - ch / 2;

    gfx_roundrect(cx + 4, cyy + 6, cw, ch, 28, C_SCRIM);
    gfx_roundrect(cx, cyy, cw, ch, 28, C_CARD);

    // monogram tile
    gfx_roundrect(cx + cw / 2 - 30, cyy + 28, 60, 60, 18, C_ACCENT);
    gfx_text(cx + cw / 2 - 10, cyy + 40, GFX_FONT_SEMIBOLD, 32,
             GFX_RGB(0xFF, 0xFF, 0xFF), "C");

    const char *title = u->have_api_key ? "Start a conversation"
                                        : "API key required";
    int tw;
    gfx_measure(GFX_FONT_SEMIBOLD, 24, title, &tw, NULL);
    gfx_text(cx + (cw - tw) / 2, cyy + 104, GFX_FONT_SEMIBOLD, 24, C_TEXT,
             title);

    const char *sub = u->have_api_key
        ? "Tap the field below or press A to type."
        : "Open Settings and paste an Anthropic API key.";
    gfx_measure(GFX_FONT_REGULAR, 19, sub, &tw, NULL);
    gfx_text(cx + (cw - tw) / 2, cyy + 140, GFX_FONT_REGULAR, 19, C_DIM, sub);

    if (!u->have_api_key) {
        gfx_roundrect(cx + cw / 2 - 110, cyy + 178, 220, 48, 24, C_ACCENT);
        gfx_measure(GFX_FONT_SEMIBOLD, 20, "Open Settings", &tw, NULL);
        gfx_text(cx + (cw - tw) / 2, cyy + 192, GFX_FONT_SEMIBOLD, 20,
                 GFX_RGB(0xFF, 0xFF, 0xFF), "Open Settings");
    }
}

static void draw_items(UiApp *u, Layout *l) {
    UiItem *items = items_of(u);
    gfx_clip(l->chat.x, l->chat.y, l->chat.w, l->chat.h);

    float y = (float)l->chat_bottom - u->scroll_off - 10;
    int label_w_pad = 6;

    // Typing indicator is the newest entry: drawn first in this
    // bottom-up walk so it sits under the last message.
    if (u->requesting) {
        int th = 46;
        y -= th;
        if (y + th > l->chat.y - 40 && y < l->chat_bottom + 40) {
            gfx_roundrect(l->chat.x, (int)y, 76, th, BUBBLE_R, C_CARD);
            for (int d = 0; d < 3; d++) {
                float ph = (float)((u->frame + d * 10) % 30) / 30.0f;
                float a = 0.35f + 0.65f * (ph < 0.5f ? ph * 2 : (1 - ph) * 2);
                gfx_circle(l->chat.x + 20 + d * 18, (int)y + 23, 4,
                           GFX_RGBA(0x9A, 0x9D, 0xAA, (int)(a * 255)));
            }
            gfx_text(l->chat.x + label_w_pad, (int)y - LABEL_H + 4,
                     GFX_FONT_SEMIBOLD, 15, C_FAINT, "CLAUDE");
        }
        y -= BUBBLE_GAP + LABEL_H;
    }

    for (int i = u->item_count - 1; i >= 0; i--) {
        UiItem *it = &items[i];
        bool user = it->kind == UI_LINE_USER;
        bool labeled = user || it->kind == UI_LINE_ASSISTANT;
        int bw = it->bubble_w, bh = it->bubble_h;

        y -= bh;
        float bx;
        GfxColor bg;
        if (user) {
            bx = l->chat.x + l->chat.w - bw - 14; // clear of scrollbar
            bg = C_ACCENT;
        } else if (it->kind == UI_LINE_ASSISTANT) {
            bx = (float)l->chat.x;
            bg = C_CARD;
        } else if (it->kind == UI_LINE_ERROR) {
            bx = l->chat.x + (l->chat.w - bw) / 2.0f;
            bg = C_ERR_BG;
        } else {
            bx = l->chat.x + (l->chat.w - bw) / 2.0f;
            bg = C_SYS_BG;
        }

        if (y + bh > l->chat.y - 40 && y < l->chat_bottom + 40) {
            if (user)
                gfx_roundrect((int)bx + 3, (int)y + 4, bw, bh, BUBBLE_R,
                              C_SCRIM);
            gfx_roundrect((int)bx, (int)y, bw, bh, BUBBLE_R, bg);
            gfx_blit(it->surf, (int)bx + BUBBLE_PAD_X,
                     (int)y + BUBBLE_PAD_Y);
            if (labeled) {
                const char *who = user ? "YOU" : "CLAUDE";
                int ww;
                gfx_measure(GFX_FONT_SEMIBOLD, 15, who, &ww, NULL);
                gfx_text((int)(user ? bx + bw - ww - label_w_pad
                                    : bx + label_w_pad),
                         (int)y - LABEL_H + 4, GFX_FONT_SEMIBOLD, 15, C_FAINT,
                         who);
            }
        }
        y -= BUBBLE_GAP + (labeled ? LABEL_H : 0);
        if (y < l->chat.y - 80) break; // fully above viewport
    }

    gfx_clip_reset();

    // scrollbar
    float m = max_scroll(u);
    if (m > 1) {
        int track_x = l->chat.x + l->chat.w - 6;
        int track_h = l->chat.h;
        gfx_fill(track_x, l->chat.y, 4, track_h, GFX_RGBA(0xFF,0xFF,0xFF,0x18));
        float frac = (float)l->chat.h / (l->chat.h + m);
        int thumb_h = (int)(track_h * frac);
        if (thumb_h < 28) thumb_h = 28;
        float t = 1.0f - u->scroll_off / m;
        int thumb_y = l->chat.y + (int)((track_h - thumb_h) * t);
        gfx_roundrect(track_x - 1, thumb_y, 6, thumb_h, 3,
                      GFX_RGBA(0xFF, 0xFF, 0xFF, 0x55));
    }

    // scroll hint
    if (u->scroll_off > 60) {
        int w2, h2;
        gfx_get_size(&w2, &h2);
        const char *s = "scrolled up - tap to jump to latest";
        int tw;
        gfx_measure(GFX_FONT_REGULAR, 16, s, &tw, NULL);
        int bx = (w2 - tw) / 2 - 16, by = l->chat_bottom - 44;
        gfx_roundrect(bx, by, tw + 32, 32, 16, GFX_RGBA(0x00,0x00,0x00,0x90));
        gfx_text(bx + 16, by + 8, GFX_FONT_REGULAR, 16, C_DIM, s);
    }
}

static void draw_compose(UiApp *u, Layout *l) {
    // field
    gfx_roundrect(l->field.x, l->field.y, l->field.w, l->field.h, 28,
                  C_FIELD);
    if (u->focus_visible && u->focus_col == F_FIELD)
        gfx_roundrect_stroke(l->field.x - 2, l->field.y - 2,
                             l->field.w + 4, l->field.h + 4, 30, 3, C_ACCENT);
    else
        gfx_roundrect_stroke(l->field.x, l->field.y, l->field.w, l->field.h,
                             28, 1, C_DIVIDER);

    if (u->compose[0]) {
        // show the tail of the draft
        int max_w = l->field.w - 70;
        int tw, len = (int)strlen(u->compose);
        const char *show = u->compose;
        int off = 0;
        // find suffix that fits (crude: binary-ish scan from the end)
        for (; off < len; off++) {
            gfx_measure(GFX_FONT_REGULAR, 21, u->compose + off, &tw, NULL);
            if (tw <= max_w) break;
        }
        show = u->compose + off;
        gfx_clip(l->field.x + 22, l->field.y, l->field.w - 44, l->field.h);
        gfx_text(l->field.x + 22, l->field.y + 16, GFX_FONT_REGULAR, 21,
                 C_TEXT, show);
        gfx_clip_reset();
        // blinking cursor
        if ((u->frame / 30) & 1)
            gfx_fill(l->field.x + 22 + (tw > max_w ? max_w : tw) + 3,
                     l->field.y + 14, 2, 26, C_ACCENT);
    } else {
        gfx_text(l->field.x + 22, l->field.y + 16, GFX_FONT_REGULAR, 21,
                 C_FAINT, "Message Claude  (tap or press A)");
    }

    // send button
    bool can_send = u->compose[0] && !u->requesting;
    gfx_roundrect(l->send.x, l->send.y, l->send.w, l->send.h, 28,
                  can_send ? C_ACCENT : C_CARD_HI);
    if (u->focus_visible && u->focus_col == F_SEND)
        gfx_roundrect_stroke(l->send.x - 2, l->send.y - 2, l->send.w + 4,
                             l->send.h + 4, 30, 3, C_TEXT);
    const char *lbl = u->requesting ? "..." : "Send";
    int sw;
    gfx_measure(GFX_FONT_SEMIBOLD, 20, lbl, &sw, NULL);
    int tx = l->send.x + (l->send.w - sw - 18) / 2;
    gfx_text(tx, l->send.y + 17, GFX_FONT_SEMIBOLD, 20,
             can_send ? GFX_RGB(0xFF, 0xFF, 0xFF) : C_DIM, lbl);
    draw_arrow_right(tx + sw + 16, l->send.y + 28, 9,
                     can_send ? GFX_RGB(0xFF, 0xFF, 0xFF) : C_DIM);
}

static void draw_hints(UiApp *u) {
    int w, h;
    gfx_get_size(&w, &h);
    gfx_hline(0, h - HINT_H, w, C_DIVIDER);
    const char *s;
    if (u->screen == 1)
        s = "dpad + A select / edit   B back   |   touch works too";
    else
        s = u->requesting
            ? "B cancel   |   touch: tap + drag   |   dpad: scroll / select"
            : "A type   B cancel   X clear   - settings   + quit   |   touch + dpad";
    int tw;
    gfx_measure(GFX_FONT_REGULAR, 17, s, &tw, NULL);
    gfx_text((w - tw) / 2, h - HINT_H + 13, GFX_FONT_REGULAR, 17, C_FAINT, s);
}

static void draw_toast(UiApp *u) {
    if (u->toast_frames <= 0) return;
    int w, h;
    gfx_get_size(&w, &h);
    int tw;
    gfx_measure(GFX_FONT_REGULAR, 18, u->toast, &tw, NULL);
    int bw = tw + 48, bh = 44;
    int x = (w - bw) / 2, y = h - HINT_H - COMPOSE_H - bh - 18;
    uint8_t a = u->toast_frames < 30 ? (uint8_t)(u->toast_frames * 8) : 255;
    gfx_roundrect(x, y, bw, bh, bh / 2, GFX_RGBA(0x10, 0x11, 0x16, a));
    gfx_text(x + 24, y + 12, GFX_FONT_REGULAR, 18,
             GFX_RGBA(0xF2, 0xEF, 0xEA, a), u->toast);
    u->toast_frames--;
}

// ---------------------------------------------------------------
// Settings screen
// ---------------------------------------------------------------

enum { S_ROWS = 3, S_SAVE = 3, S_BACK = 4, S_COUNT = 5 };

static const char *set_names[S_ROWS] = {"API key", "Model", "Base URL"};
static const char *set_guides[S_ROWS] = {"sk-ant-...", "e.g. claude-sonnet-4-5",
                                         "https://api.anthropic.com"};

static void mask_key(const char *key, char *out, size_t sz) {
    size_t len = strlen(key);
    if (!len) { snprintf(out, sz, "(not set)"); return; }
    if (len <= 8) { snprintf(out, sz, "********"); return; }
    snprintf(out, sz, "%.6s...%s", key, key + len - 4);
}

static GfxRect settings_row_rect(int i, int w, int h) {
    (void)h;
    int col_w = w - 240 < 860 ? w - 240 : 860;
    int x = (w - col_w) / 2;
    int y = HEADER_H + 44 + i * 92;
    return (GfxRect){x, y, col_w, 76};
}

static GfxRect settings_btn_rect(int i, int w, int h) {
    int y = h - HINT_H - 92;
    int bw = 300;
    int x = (w - bw * 2 - 24) / 2 + i * (bw + 24);
    return (GfxRect){x, y, bw, 60};
}

static void draw_settings(UiApp *u) {
    int w, h;
    gfx_get_size(&w, &h);
    gfx_fill(0, 0, w, HEADER_H, C_HEADER);
    gfx_hline(0, HEADER_H - 1, w, C_DIVIDER);

    // back chevron
    gfx_roundrect(24, 10, 110, 52, 16, C_CARD);
    draw_chevron_left(48, 36, 10, C_DIM);
    gfx_text(66, 24, GFX_FONT_SEMIBOLD, 20, C_DIM, "Back");

    const char *t = "Settings";
    int tw;
    gfx_measure(GFX_FONT_SEMIBOLD, 28, t, &tw, NULL);
    gfx_text((w - tw) / 2, 21, GFX_FONT_SEMIBOLD, 28, C_TEXT, t);

    for (int i = 0; i < S_ROWS; i++) {
        GfxRect r = settings_row_rect(i, w, h);
        gfx_roundrect(r.x + 3, r.y + 5, r.w, r.h, 20, C_SCRIM);
        gfx_roundrect(r.x, r.y, r.w, r.h, 20, C_CARD);
        if (u->focus_visible && u->set_sel == i)
            gfx_roundrect_stroke(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 22, 3,
                                 C_ACCENT);
        gfx_text(r.x + 22, r.y + 12, GFX_FONT_SEMIBOLD, 16, C_FAINT,
                 set_names[i]);
        char val[256];
        const char *raw = i == 0 ? u->edit.api_key
                        : i == 1 ? u->edit.model : u->edit.base_url;
        if (i == 0) mask_key(raw, val, sizeof(val));
        else snprintf(val, sizeof(val), "%s", raw);
        if (!val[0]) snprintf(val, sizeof(val), "(empty)");
        gfx_clip(r.x + 20, r.y, r.w - 60, r.h);
        gfx_text(r.x + 22, r.y + 34, GFX_FONT_REGULAR, 21, C_TEXT, val);
        gfx_clip_reset();
        gfx_text(r.x + r.w - 40, r.y + r.h / 2 - 10, GFX_FONT_SEMIBOLD, 18,
                 C_FAINT, ">");
    }

    // about card
    int row_bottom = settings_row_rect(S_ROWS - 1, w, h).y + 76;
    GfxRect ab = {settings_row_rect(0, w, h).x, row_bottom + 18,
                  settings_row_rect(0, w, h).w, 64};
    gfx_roundrect(ab.x, ab.y, ab.w, ab.h, 20, C_FIELD);
    gfx_text(ab.x + 22, ab.y + 12, GFX_FONT_SEMIBOLD, 15, C_FAINT,
             "Claude Code Switch v0.2.1");
    gfx_text(ab.x + 22, ab.y + 34, GFX_FONT_REGULAR, 15, C_FAINT,
             "sdmc:/config/claude-code-switch/settings.json");

    // action buttons
    GfxRect save = settings_btn_rect(0, w, h);
    GfxRect back = settings_btn_rect(1, w, h);
    gfx_roundrect(save.x, save.y, save.w, save.h, 20, C_ACCENT);
    if (u->focus_visible && u->set_sel == S_SAVE)
        gfx_roundrect_stroke(save.x - 2, save.y - 2, save.w + 4,
                             save.h + 4, 22, 3, C_TEXT);
    gfx_measure(GFX_FONT_SEMIBOLD, 21, "Save & apply", &tw, NULL);
    gfx_text(save.x + (save.w - tw) / 2, save.y + 18, GFX_FONT_SEMIBOLD, 21,
             GFX_RGB(0xFF, 0xFF, 0xFF), "Save & apply");

    gfx_roundrect(back.x, back.y, back.w, back.h, 20, C_CARD_HI);
    if (u->focus_visible && u->set_sel == S_BACK)
        gfx_roundrect_stroke(back.x - 2, back.y - 2, back.w + 4,
                             back.h + 4, 22, 3, C_ACCENT);
    gfx_measure(GFX_FONT_SEMIBOLD, 21, "Discard", &tw, NULL);
    gfx_text(back.x + (back.w - tw) / 2, back.y + 18, GFX_FONT_SEMIBOLD, 21,
             C_DIM, "Discard");

    // warning
    const char *warn = "The API key is stored in plaintext on the SD card - use a dedicated key with a tight spend limit.";
    gfx_measure(GFX_FONT_REGULAR, 16, warn, &tw, NULL);
    gfx_text((w - tw) / 2, settings_btn_rect(0, w, h).y - 34,
             GFX_FONT_REGULAR, 16, C_FAINT, warn);

    draw_hints(u);
}

// ---------------------------------------------------------------
// Input + dispatch
// ---------------------------------------------------------------

static bool in_rect(GfxRect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void chat_touch_tap(UiApp *u, Layout *l, int x, int y,
                           UiOutput *out) {
    if (in_rect(l->field, x, y)) {
        out->kbd_field = UI_KBD_COMPOSE;
        snprintf(out->kbd_title, sizeof(out->kbd_title), "Message to Claude");
        snprintf(out->kbd_guide, sizeof(out->kbd_guide), "Enter your message");
    } else if (in_rect(l->send, x, y)) {
        out->send = true;
    } else if (in_rect(l->b_clear, x, y)) {
        out->clear_chat = true;
    } else if (in_rect(l->b_settings, x, y)) {
        ui_open_settings(u);
    } else if (in_rect(l->b_quit, x, y)) {
        out->quit = true;
    } else if (u->scroll_off > 60 && in_rect(l->chat, x, y) &&
               y > l->chat_bottom - 48) {
        u->scroll_off = 0; // jump to latest chip
    } else if (u->item_count == 0 && !u->have_api_key) {
        ui_open_settings(u); // empty-state CTA
    }
}

static void chat_input(UiApp *u, const UiInput *in, UiOutput *out,
                       Layout *l) {
    uint32_t down = in->down, held = in->held;

    if (down & UI_BTN_PLUS) { out->quit = true; return; }
    if (down & UI_BTN_MINUS) { ui_open_settings(u); return; }
    if (down & UI_BTN_X) { out->clear_chat = true; }
    if (down & UI_BTN_B && u->requesting) out->cancel_req = true;

    // scrolling via dpad up/down + sticks
    if (held & UI_BTN_UP)   u->scroll_off += 10;
    if (held & UI_BTN_DOWN) u->scroll_off -= 10;
    if (in->scroll_px) u->scroll_off += in->scroll_px;
    if (down & (UI_BTN_L)) u->scroll_off += 320;
    if (down & (UI_BTN_R)) u->scroll_off -= 320;

    // focus nav: left/right cycles elements
    if (down & (UI_BTN_LEFT | UI_BTN_RIGHT)) {
        int dir = (down & UI_BTN_RIGHT) ? 1 : -1;
        u->focus_col = (u->focus_col + dir + F_CHAT_COUNT) % F_CHAT_COUNT;
        u->focus_row = (u->focus_col <= F_SEND) ? 2 : 0;
        u->focus_visible = true;
    }

    if (down & UI_BTN_A) {
        u->focus_visible = true;
        switch (u->focus_col) {
            case F_FIELD:
                out->kbd_field = UI_KBD_COMPOSE;
                snprintf(out->kbd_title, sizeof(out->kbd_title),
                         "Message to Claude");
                snprintf(out->kbd_guide, sizeof(out->kbd_guide),
                         "Enter your message");
                break;
            case F_SEND:     out->send = true; break;
            case F_CLEAR:    out->clear_chat = true; break;
            case F_SETTINGS: ui_open_settings(u); break;
            case F_QUIT:     out->quit = true; break;
        }
    }

    // typed text -> compose
    if (in->typed) {
        size_t len = strlen(u->compose);
        for (const char *p = in->typed; *p; p++) {
            if (len < sizeof(u->compose) - 1) u->compose[len++] = *p;
        }
        u->compose[len] = 0;
    }
    if (in->typed_backspace) {
        size_t len = strlen(u->compose);
        if (len) u->compose[len - 1] = 0;
    }
    if (in->typed_enter && u->compose[0] && !u->requesting)
        out->send = true;
    if (in->typed_escape && u->requesting)
        out->cancel_req = true;

    // touch gestures
    if (in->touch_pressed) {
        u->touch_start_x = u->touch_last_x = in->touch_x;
        u->touch_start_y = u->touch_last_y = in->touch_y;
        u->touch_scrolling = false;
        u->scroll_vel = 0;
    }
    if (in->touch_active) {
        int dy = in->touch_y - u->touch_last_y;
        if (u->touch_scrolling ||
            (in_rect(l->chat, u->touch_start_x, u->touch_start_y) &&
             abs(in->touch_y - u->touch_start_y) > TAP_SLOP)) {
            u->touch_scrolling = true;
            u->scroll_off -= dy;
            u->scroll_vel = -dy * 1.0f;
        }
        u->touch_last_y = in->touch_y;
    }
    if (in->touch_released) {
        if (!u->touch_scrolling)
            chat_touch_tap(u, l, in->touch_x, in->touch_y, out);
        u->touch_scrolling = false;
    }
}

static void settings_input(UiApp *u, const UiInput *in, UiOutput *out,
                           int w, int h) {
    uint32_t down = in->down;

    if (down & (UI_BTN_B | UI_BTN_PLUS | UI_BTN_MINUS)) {
        u->screen = 0;
        return;
    }
    if (down & UI_BTN_UP)   { u->set_sel = (u->set_sel + S_COUNT - 1) % S_COUNT; u->focus_visible = true; }
    if (down & UI_BTN_DOWN) { u->set_sel = (u->set_sel + 1) % S_COUNT; u->focus_visible = true; }
    if (down & (UI_BTN_LEFT | UI_BTN_RIGHT) && u->set_sel >= S_SAVE)
        u->set_sel = u->set_sel == S_SAVE ? S_BACK : S_SAVE;

    if (down & UI_BTN_A) {
        u->focus_visible = true;
        if (u->set_sel < S_ROWS) {
            out->kbd_field = UI_KBD_API_KEY + u->set_sel;
            snprintf(out->kbd_title, sizeof(out->kbd_title), "%s",
                     set_names[u->set_sel]);
            snprintf(out->kbd_guide, sizeof(out->kbd_guide), "%s",
                     set_guides[u->set_sel]);
        } else if (u->set_sel == S_SAVE) {
            out->save_settings = true;
            u->screen = 0;
        } else {
            u->screen = 0;
        }
    }

    if (in->touch_released) {
        for (int i = 0; i < S_ROWS; i++) {
            if (in_rect(settings_row_rect(i, w, h), in->touch_x,
                        in->touch_y)) {
                u->set_sel = i;
                out->kbd_field = UI_KBD_API_KEY + i;
                snprintf(out->kbd_title, sizeof(out->kbd_title), "%s",
                         set_names[i]);
                snprintf(out->kbd_guide, sizeof(out->kbd_guide), "%s",
                         set_guides[i]);
                return;
            }
        }
        if (in_rect(settings_btn_rect(0, w, h), in->touch_x, in->touch_y)) {
            out->save_settings = true;
            u->screen = 0;
        } else if (in_rect(settings_btn_rect(1, w, h), in->touch_x,
                           in->touch_y) ||
                   (in->touch_y < HEADER_H && in->touch_x < 140)) {
            u->screen = 0;
        }
    }
}

// ---------------------------------------------------------------

void ui_frame(UiApp *u, const UiInput *in, UiOutput *out) {
    memset(out, 0, sizeof(*out));
    out->kbd_field = UI_KBD_NONE;
    u->frame++;

    int w, h;
    gfx_get_size(&w, &h);
    Layout l = compute_layout();

    if (u->screen == 0)
        chat_input(u, in, out, &l);
    else
        settings_input(u, in, out, w, h);

    // inertia after release
    if (!u->scroll_drag && !in->touch_active && fabsf(u->scroll_vel) > 0.4f) {
        u->scroll_off += u->scroll_vel;
        u->scroll_vel *= 0.90f;
    }
    clamp_scroll(u);

    // ---- render ----
    gfx_begin();
    gfx_fill_grad_v(0, 0, w, h, C_BG_TOP, C_BG_BOT);

    if (u->screen == 0) {
        if (u->item_count == 0)
            draw_empty_state(u);
        else
            draw_items(u, &l);
        draw_header(u, &l);
        draw_compose(u, &l);
        draw_hints(u);
    } else {
        draw_settings(u);
    }
    draw_toast(u);

    gfx_present();
}
