#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "gfx.h"
#include "settings.h"

// Platform-agnostic UI for the chat client. main.c translates libnx input
// into UiInput; the module renders via gfx.h and reports requested actions
// through UiOutput so it stays buildable on desktop SDL2 too.

typedef enum {
    UI_LINE_SYS = 0,
    UI_LINE_USER,
    UI_LINE_ASSISTANT,
    UI_LINE_ERROR,
    UI_LINE_DIM,
} UiLineKind;

// Buttons (bitmask). Mirrors the pad buttons we care about.
enum {
    UI_BTN_A     = 1u << 0,
    UI_BTN_B     = 1u << 1,
    UI_BTN_X     = 1u << 2,
    UI_BTN_Y     = 1u << 3,
    UI_BTN_PLUS  = 1u << 4,
    UI_BTN_MINUS = 1u << 5,
    UI_BTN_UP    = 1u << 6,
    UI_BTN_DOWN  = 1u << 7,
    UI_BTN_LEFT  = 1u << 8,
    UI_BTN_RIGHT = 1u << 9,
    UI_BTN_L     = 1u << 10,
    UI_BTN_R     = 1u << 11,
};

typedef struct {
    uint32_t down;   // UI_BTN_* pressed this frame
    uint32_t held;   // UI_BTN_* currently held
    int scroll_px;   // analog scroll delta this frame (px, + = scroll up)

    const char *typed;          // characters typed this frame (may be NULL)
    bool typed_enter;
    bool typed_backspace;
    bool typed_escape;

    bool touch_active;    // finger currently on screen
    bool touch_pressed;   // went down this frame
    bool touch_released;  // lifted this frame
    int touch_x, touch_y; // current finger position (screen px)
} UiInput;

// Keyboard targets the app may request.
enum {
    UI_KBD_NONE = -1,
    UI_KBD_COMPOSE = 0,
    UI_KBD_API_KEY,
    UI_KBD_MODEL,
    UI_KBD_BASE_URL,
};

typedef struct {
    int kbd_field;        // UI_KBD_* or UI_KBD_NONE
    char kbd_title[64];
    char kbd_guide[64];
    bool send;            // send the compose text
    bool quit;
    bool cancel_req;
    bool clear_chat;
    bool save_settings;   // copy ui_settings_edit() into live settings + save
} UiOutput;

typedef struct {
    // chat log
    void *items;          // opaque
    int item_count;

    // scroll state (px offset from the bottom; 0 = pinned)
    float scroll_off;
    float scroll_vel;
    bool scroll_drag;
    bool at_bottom;

    // compose
    char compose[1024];

    // focus navigation
    int focus_row;        // 0 header, 1 chat, 2 compose
    int focus_col;
    bool focus_visible;   // shown once the dpad is used

    // touch gesture
    int touch_start_x, touch_start_y;
    int touch_last_x, touch_last_y;
    bool touch_scrolling;
    uint64_t frame;

    // status
    char model[64];
    bool requesting;
    bool have_api_key;

    // settings screen
    int screen;           // 0 chat, 1 settings
    int set_sel;
    Settings edit;
    bool edit_loaded;

    // transient notice (bottom toast)
    char toast[128];
    int toast_frames;
} UiApp;

void ui_init(UiApp *u);
void ui_quit(UiApp *u);

// Feed the UI the live model name / api-key presence so the header and
// empty state stay accurate.
void ui_set_context(UiApp *u, const char *model, bool have_api_key);

void ui_set_requesting(UiApp *u, bool requesting);

void ui_chat_add(UiApp *u, UiLineKind kind, const char *text);
void ui_chat_add_fmt(UiApp *u, UiLineKind kind, const char *fmt, ...);
void ui_chat_clear(UiApp *u);

const char *ui_compose(const UiApp *u);
void ui_compose_set(UiApp *u, const char *text);
void ui_compose_clear(UiApp *u);

// Settings draft being edited on the settings screen.
Settings *ui_settings_edit(UiApp *u);
void ui_settings_load(UiApp *u, const Settings *s);
void ui_open_settings(UiApp *u);

void ui_toast(UiApp *u, const char *text);

// One frame: consume input, update state, render.
void ui_frame(UiApp *u, const UiInput *in, UiOutput *out);
