#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "term.h"
#include "settings.h"
#include "kbd.h"
#include "net.h"

#define COMPOSE_MAX 900

// Bounded string copy that won't trip -Wformat-truncation.
static void str_copy(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

typedef enum {
    SCREEN_CHAT = 0,
    SCREEN_SETTINGS,
    SCREEN_QUIT,
} Screen;

static TermBuffer g_term;
static Settings g_settings;
static Conversation g_conv;
static NetJob g_job;
static char g_compose[COMPOSE_MAX + 1];
static char g_status[128];
static char g_hints[128];
static int g_frame = 0;

static const char *spinner(int frame) {
    static const char *frames[] = {"|", "/", "-", "\\"};
    return frames[(frame / 6) % 4];
}

static void send_message(const char *text) {
    if (!text || !*text) return;
    if (g_settings.api_key[0] == 0) {
        term_add(&g_term, LINE_ERROR,
                 "No API key set. Press - (Minus) to open Settings.");
        return;
    }
    conv_add(&g_conv, "user", text);
    term_add_fmt(&g_term, LINE_USER, "> %s", text);
    if (!net_send_async(&g_job, &g_conv, &g_settings)) {
        term_add(&g_term, LINE_ERROR, "Could not start request (busy?)");
        conv_pop(&g_conv);
    }
    term_scroll_to_bottom(&g_term);
}

static void update_status(void) {
    ReqState st = net_poll(&g_job);
    const char *net;
    switch (st) {
        case REQ_RUNNING:   net = "sending..."; break;
        case REQ_IDLE:
        default:            net = "ready"; break;
    }
    const char *scroll_note = g_term.scroll ? "  [scrolled]" : "";
    snprintf(g_status, sizeof(g_status),
             " Claude Code Switch | %s | net:%s%s",
             g_settings.model, net, scroll_note);
    if (st == REQ_RUNNING)
        snprintf(g_hints, sizeof(g_hints),
                 " %s thinking  | B cancel | - settings | + quit",
                 spinner(g_frame));
    else
        snprintf(g_hints, sizeof(g_hints),
                 " A compose | dpad/stick scroll | X clear | - settings | + quit");
}

static void handle_request_done(void) {
    ReqState st = net_poll(&g_job);
    switch (st) {
        case REQ_DONE:
            conv_add(&g_conv, "assistant", g_job.result);
            term_add(&g_term, LINE_ASSISTANT, g_job.result);
            break;
        case REQ_ERROR:
            term_add_fmt(&g_term, LINE_ERROR, "Error: %s", g_job.error);
            conv_pop(&g_conv);
            break;
        case REQ_CANCELLED:
            term_add(&g_term, LINE_DIM, "(request cancelled)");
            conv_pop(&g_conv);
            break;
        default:
            return;
    }
    net_finish(&g_job);
    term_scroll_to_bottom(&g_term);
}

static void chat_screen(PadState *pad, Screen *screen) {
    u64 down = padGetButtonsDown(pad);
    u64 held = padGetButtons(pad);

    if (down & HidNpadButton_Plus)  { *screen = SCREEN_QUIT; return; }
    if (down & HidNpadButton_Minus) { *screen = SCREEN_SETTINGS; return; }

    // Scrollback.
    if (down & HidNpadButton_ZL || down & HidNpadButton_L)
        term_scroll(&g_term, TERM_BODY_ROWS);
    else if (down & HidNpadButton_ZR || down & HidNpadButton_R)
        term_scroll(&g_term, -TERM_BODY_ROWS);
    else if (held & HidNpadButton_AnyUp)
        term_scroll(&g_term, 1);
    else if (held & HidNpadButton_AnyDown)
        term_scroll(&g_term, -1);

    if (down & HidNpadButton_B && net_poll(&g_job) == REQ_RUNNING) {
        net_cancel(&g_job);
        term_add(&g_term, LINE_DIM, "(cancelling...)");
    }

    if (down & HidNpadButton_X) {
        conv_clear(&g_conv);
        term_init(&g_term);
        term_add(&g_term, LINE_SYS, "Conversation cleared.");
    }

    if (down & HidNpadButton_A) {
        char draft[1024];
        str_copy(draft, sizeof(draft), g_compose);
        if (kbd_prompt(draft, sizeof(draft), "Message to Claude",
                       "Enter your message")) {
            if (net_poll(&g_job) == REQ_RUNNING) {
                str_copy(g_compose, sizeof(g_compose), draft);
                term_add(&g_term, LINE_DIM,
                         "(kept as draft - request still in flight)");
            } else {
                g_compose[0] = 0;
                send_message(draft);
            }
        } else {
            str_copy(g_compose, sizeof(g_compose), draft);
        }
    }

    // USB keyboard input.
    kbd_poll_begin();
    char typed[64];
    int n = kbd_poll_chars(typed, sizeof(typed));
    for (int i = 0; i < n; i++) {
        size_t len = strlen(g_compose);
        if (len < COMPOSE_MAX) {
            g_compose[len] = typed[i];
            g_compose[len + 1] = 0;
            g_term.dirty = true;
        }
    }
    if (kbd_pressed_backspace()) {
        size_t len = strlen(g_compose);
        if (len) g_compose[len - 1] = 0;
        g_term.dirty = true;
    }
    if (kbd_pressed_enter() && net_poll(&g_job) != REQ_RUNNING &&
        g_compose[0]) {
        send_message(g_compose);
        g_compose[0] = 0;
    }
    if (kbd_pressed_escape() && net_poll(&g_job) == REQ_RUNNING)
        net_cancel(&g_job);

    ReqState st = net_poll(&g_job);
    if (st == REQ_DONE || st == REQ_ERROR || st == REQ_CANCELLED)
        handle_request_done();

    // Compose line display.
    static char comp_line[TERM_COLS + 1];
    if (g_compose[0]) {
        size_t len = strlen(g_compose);
        const char *tail = len > 74 ? g_compose + len - 74 : g_compose;
        snprintf(comp_line, sizeof(comp_line), "> %.*s_",
                 (int)(sizeof(comp_line) - 4), tail);
    } else {
        str_copy(comp_line, sizeof(comp_line),
                 " > (press A for keyboard, or type on USB kbd)");
    }

    update_status();
    term_render(&g_term, g_status, comp_line, g_hints);
}

// ---------------------------------------------------------------
// Settings screen
// ---------------------------------------------------------------

typedef enum {
    SET_API_KEY = 0,
    SET_MODEL,
    SET_BASE_URL,
    SET_SAVE_BACK,
    SET_BACK,
    SET_COUNT,
} SettingItem;

static const char *setting_names[SET_COUNT] = {
    "API key",
    "Model",
    "Base URL",
    "Save & back",
    "Back (discard)",
};

static void mask_key(const char *key, char *out, size_t out_size) {
    size_t len = strlen(key);
    if (len == 0) { snprintf(out, out_size, "(not set)"); return; }
    if (len <= 8) { snprintf(out, out_size, "********"); return; }
    snprintf(out, out_size, "%.6s...%s (len %u)", key, key + len - 4,
             (unsigned)len);
}

static void settings_screen(PadState *pad, Screen *screen) {
    static int sel = 0;
    static Settings edit;
    static bool loaded = false;

    if (!loaded) {
        edit = g_settings;
        loaded = true;
    }

    u64 down = padGetButtonsDown(pad);

    if (down & HidNpadButton_AnyUp)   sel = (sel + SET_COUNT - 1) % SET_COUNT;
    if (down & HidNpadButton_AnyDown) sel = (sel + 1) % SET_COUNT;

    if (down & (HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus)) {
        loaded = false;
        *screen = SCREEN_CHAT;
        return;
    }

    if (down & HidNpadButton_A) {
        switch (sel) {
            case SET_API_KEY:
                kbd_prompt(edit.api_key, sizeof(edit.api_key),
                           "Anthropic API key", "sk-ant-...");
                break;
            case SET_MODEL:
                kbd_prompt(edit.model, sizeof(edit.model),
                           "Model", "e.g. claude-sonnet-4-5");
                break;
            case SET_BASE_URL:
                kbd_prompt(edit.base_url, sizeof(edit.base_url),
                           "Base URL", "https://api.anthropic.com");
                break;
            case SET_SAVE_BACK:
                g_settings = edit;
                if (settings_save(&g_settings))
                    term_add(&g_term, LINE_SYS, "Settings saved to SD card.");
                else
                    term_add(&g_term, LINE_ERROR,
                             "Failed to write settings to SD card!");
                loaded = false;
                *screen = SCREEN_CHAT;
                return;
            case SET_BACK:
                loaded = false;
                *screen = SCREEN_CHAT;
                return;
            default:
                break;
        }
    }

    // Render the settings screen directly with the console.
    consoleClear();
    printf("\x1b[7m%-*s\x1b[0m\n\n", TERM_COLS,
           " Settings (sdmc:/config/claude-code-switch/settings.json)");
    char masked[64];
    mask_key(edit.api_key, masked, sizeof(masked));
    const char *values[SET_COUNT] = {
        masked, edit.model, edit.base_url, "", "",
    };
    for (int i = 0; i < SET_COUNT; i++) {
        const char *cursor = (i == sel) ? " > " : "   ";
        if (values[i][0])
            printf("%s%s: %s\n", cursor, setting_names[i], values[i]);
        else
            printf("%s%s\n", cursor, setting_names[i]);
    }
    printf("\n\x1b[90m");
    printf("WARNING: the API key is stored in PLAINTEXT on the SD card.\n");
    printf("Anyone with SD or console access can read it. Use a dedicated\n");
    printf("key with a tight spend limit. Never use a production key.\n");
    printf("\x1b[0m\n");
    printf("\x1b[7m%-*s\x1b[0m", TERM_COLS,
           " A edit/select | B back | - back");
    consoleUpdate(NULL);
}

// ---------------------------------------------------------------

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        printf("socketInitialize failed: 0x%x\n", rc);
    }

    rc = romfsInit();
    if (R_FAILED(rc)) {
        printf("romfsInit failed: 0x%x\n", rc);
    }

    kbd_init();

    settings_defaults(&g_settings);
    bool had_settings = settings_load(&g_settings);
    conv_init(&g_conv);
    term_init(&g_term);

    term_add(&g_term, LINE_SYS,
             "Claude Code Switch - terminal client for the Anthropic API");
    term_add(&g_term, LINE_DIM,
             "Atmosphere homebrew build " __DATE__);
    if (!had_settings || g_settings.api_key[0] == 0) {
        term_add(&g_term, LINE_ERROR,
                 "No API key configured yet.");
        term_add(&g_term, LINE_SYS,
                 "Press - (Minus) to open Settings and enter your key.");
    }

    Screen screen = SCREEN_CHAT;

    while (appletMainLoop() && screen != SCREEN_QUIT) {
        padUpdate(&pad);
        g_frame++;

        switch (screen) {
            case SCREEN_CHAT:     chat_screen(&pad, &screen); break;
            case SCREEN_SETTINGS: settings_screen(&pad, &screen); break;
            default: break;
        }

        svcSleepThread(16 * 1000 * 1000); // ~60fps
    }

    if (net_poll(&g_job) == REQ_RUNNING) {
        net_cancel(&g_job);
        net_finish(&g_job);
    }

    conv_free(&g_conv);
    romfsExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
