#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "diag.h"
#include "gfx.h"
#include "ui.h"
#include "settings.h"
#include "kbd.h"
#include "net.h"

static Settings g_settings;
static Conversation g_conv;
static NetJob g_job;
static UiApp g_ui;

// Bounded string copy that won't trip -Wformat-truncation.
static void copy_field(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static bool send_message(const char *text) {
    if (!text || !*text) return false;
    if (g_settings.api_key[0] == 0) {
        ui_toast(&g_ui, "No API key set - add one in Settings");
        ui_open_settings(&g_ui);
        return false;
    }
    conv_add(&g_conv, "user", text);
    ui_chat_add(&g_ui, UI_LINE_USER, text);
    if (!net_send_async(&g_job, &g_conv, &g_settings)) {
        ui_chat_add_fmt(&g_ui, UI_LINE_ERROR, "%s",
                        g_job.error[0] ? g_job.error
                                       : "Could not start request");
        conv_pop(&g_conv);
        return false;
    }
    return true;
}

static void handle_request_done(ReqState st) {
    switch (st) {
        case REQ_DONE:
            conv_add(&g_conv, "assistant", g_job.result);
            ui_chat_add(&g_ui, UI_LINE_ASSISTANT, g_job.result);
            break;
        case REQ_ERROR:
            ui_chat_add_fmt(&g_ui, UI_LINE_ERROR, "Error: %s", g_job.error);
            conv_pop(&g_conv);
            break;
        case REQ_CANCELLED:
            ui_chat_add(&g_ui, UI_LINE_DIM, "(request cancelled)");
            conv_pop(&g_conv);
            break;
        default:
            return;
    }
    net_finish(&g_job);
}

// Map libnx pad state to UI buttons.
static uint32_t map_down(u64 d) {
    uint32_t o = 0;
    if (d & HidNpadButton_A)     o |= UI_BTN_A;
    if (d & HidNpadButton_B)     o |= UI_BTN_B;
    if (d & HidNpadButton_X)     o |= UI_BTN_X;
    if (d & HidNpadButton_Plus)  o |= UI_BTN_PLUS;
    if (d & HidNpadButton_Minus) o |= UI_BTN_MINUS;
    if (d & HidNpadButton_Up)    o |= UI_BTN_UP;
    if (d & HidNpadButton_Down)  o |= UI_BTN_DOWN;
    if (d & HidNpadButton_Left)  o |= UI_BTN_LEFT;
    if (d & HidNpadButton_Right) o |= UI_BTN_RIGHT;
    if (d & (HidNpadButton_L | HidNpadButton_ZL)) o |= UI_BTN_L;
    if (d & (HidNpadButton_R | HidNpadButton_ZR)) o |= UI_BTN_R;
    return o;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    diag_init();
    diag_log("=== claude-code-switch v0.2.1 boot ===");

    // RomFS holds the fonts gfx_init loads, so mount it first.
    bool romfs_ok = R_SUCCEEDED(romfsInit());
    diag_log("romfsInit: %s", romfs_ok ? "ok" : "FAILED");

    diag_log("gfx_init...");
    if (!gfx_init()) {
        diag_log("gfx_init failed - console fallback");
        // Fall back to the plain console so the failure is visible.
        consoleInit(NULL);
        printf("UI init failed (display/font). Press + to exit.\n");
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        if (romfs_ok) romfsExit();
        diag_close();
        return 1;
    }
    diag_log("gfx_init ok");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    // Networking is initialized lazily on first send (net_ensure_init in
    // net.c) so the UI always reaches its first frame regardless of what
    // socket setup does.
    kbd_init();
    diag_log("inputs ready");

    settings_defaults(&g_settings);
    settings_load(&g_settings);
    conv_init(&g_conv);
    ui_init(&g_ui);
    ui_set_context(&g_ui, g_settings.model, g_settings.api_key[0] != 0);

    if (!romfs_ok)
        ui_chat_add(&g_ui, UI_LINE_ERROR,
                    "romfsInit failed - TLS certificates unavailable.");
    // With no messages the empty-state card is shown (with an
    // "Open Settings" call-to-action when no API key is configured).

    // previous screen tracking so the settings draft loads on entry
    int last_screen = 0;
    bool touch_was_down = false;
    int touch_last_x = 0, touch_last_y = 0;
    bool running = true;

    while (appletMainLoop() && running) {
        padUpdate(&pad);

        UiInput in;
        memset(&in, 0, sizeof(in));
        in.down = map_down(padGetButtonsDown(&pad));
        in.held = map_down(padGetButtons(&pad));

        // sticks -> smooth scroll (use whichever is pushed harder)
        HidAnalogStickState ls = padGetStickPos(&pad, 0);
        HidAnalogStickState rs = padGetStickPos(&pad, 1);
        int sy = abs(rs.y) > abs(ls.y) ? rs.y : ls.y;
        if (abs(sy) > 4000) in.scroll_px = -sy / 1600;

        // touch
        HidTouchScreenState ts;
        memset(&ts, 0, sizeof(ts));
        int nw, nh;
        gfx_get_size(&nw, &nh);
        if (hidGetTouchScreenStates(&ts, 1) > 0 && ts.count > 0) {
            in.touch_active = true;
            in.touch_x = ts.touches[0].x;
            in.touch_y = ts.touches[0].y;
            // clamp to render surface
            if (in.touch_x < 0) in.touch_x = 0;
            if (in.touch_y < 0) in.touch_y = 0;
            if (in.touch_x >= nw) in.touch_x = nw - 1;
            if (in.touch_y >= nh) in.touch_y = nh - 1;
            if (!touch_was_down) in.touch_pressed = true;
            touch_was_down = true;
            touch_last_x = in.touch_x;
            touch_last_y = in.touch_y;
        } else {
            if (touch_was_down) {
                in.touch_released = true;
                in.touch_x = touch_last_x;
                in.touch_y = touch_last_y;
            }
            touch_was_down = false;
        }

        // USB keyboard
        static char typed_buf[64];
        kbd_poll_begin();
        kbd_poll_chars(typed_buf, sizeof(typed_buf));
        in.typed = typed_buf[0] ? typed_buf : NULL;
        in.typed_enter = kbd_pressed_enter();
        in.typed_backspace = kbd_pressed_backspace();
        in.typed_escape = kbd_pressed_escape();

        // load settings draft when the screen opens
        if (g_ui.screen == 1 && last_screen == 0)
            ui_settings_load(&g_ui, &g_settings);
        last_screen = g_ui.screen;

        UiOutput out;
        ui_frame(&g_ui, &in, &out);

        // ---- dispatch UI outputs ----
        if (out.quit) running = false;

        if (out.kbd_field != UI_KBD_NONE) {
            char buf[1024];
            const char *init = "";
            if (out.kbd_field == UI_KBD_COMPOSE)
                init = ui_compose(&g_ui);
            else if (out.kbd_field == UI_KBD_API_KEY)
                init = ui_settings_edit(&g_ui)->api_key;
            else if (out.kbd_field == UI_KBD_MODEL)
                init = ui_settings_edit(&g_ui)->model;
            else if (out.kbd_field == UI_KBD_BASE_URL)
                init = ui_settings_edit(&g_ui)->base_url;
            snprintf(buf, sizeof(buf), "%s", init);
            if (kbd_prompt(buf, sizeof(buf), out.kbd_title, out.kbd_guide)) {
                if (out.kbd_field == UI_KBD_COMPOSE) {
                    ui_compose_set(&g_ui, buf);
                    if (net_poll(&g_job) == REQ_RUNNING) {
                        ui_toast(&g_ui, "Kept as draft - request in flight");
                    } else if (buf[0] && send_message(buf)) {
                        ui_compose_clear(&g_ui);
                    }
                } else if (out.kbd_field == UI_KBD_API_KEY) {
                    copy_field(ui_settings_edit(&g_ui)->api_key,
                               sizeof(ui_settings_edit(&g_ui)->api_key), buf);
                } else if (out.kbd_field == UI_KBD_MODEL) {
                    copy_field(ui_settings_edit(&g_ui)->model,
                               sizeof(ui_settings_edit(&g_ui)->model), buf);
                } else if (out.kbd_field == UI_KBD_BASE_URL) {
                    copy_field(ui_settings_edit(&g_ui)->base_url,
                               sizeof(ui_settings_edit(&g_ui)->base_url), buf);
                }
            }
        }

        if (out.send) {
            if (net_poll(&g_job) == REQ_RUNNING) {
                ui_toast(&g_ui, "Request already in flight (B cancels)");
            } else if (ui_compose(&g_ui)[0]) {
                if (send_message(ui_compose(&g_ui)))
                    ui_compose_clear(&g_ui);
            }
        }

        if (out.cancel_req) net_cancel(&g_job);

        if (out.clear_chat) {
            conv_clear(&g_conv);
            ui_chat_clear(&g_ui);
            ui_chat_add(&g_ui, UI_LINE_SYS, "Conversation cleared.");
        }

        if (out.save_settings) {
            g_settings = *ui_settings_edit(&g_ui);
            ui_set_context(&g_ui, g_settings.model,
                           g_settings.api_key[0] != 0);
            if (settings_save(&g_settings))
                ui_toast(&g_ui, "Settings saved to SD card");
            else
                ui_chat_add(&g_ui, UI_LINE_ERROR,
                            "Failed to write settings to SD card!");
        }

        ReqState st = net_poll(&g_job);
        ui_set_requesting(&g_ui, st == REQ_RUNNING);
        if (st == REQ_DONE || st == REQ_ERROR || st == REQ_CANCELLED)
            handle_request_done(st);

        svcSleepThread(1000 * 1000); // ~1ms; vsync paces the loop
    }

    diag_log("main: exiting");
    if (net_poll(&g_job) == REQ_RUNNING) {
        net_cancel(&g_job);
        net_finish(&g_job);
    }

    conv_free(&g_conv);
    ui_quit(&g_ui);
    if (romfs_ok) romfsExit();
    net_close();
    gfx_quit();
    diag_log("main: clean shutdown");
    diag_close();
    return 0;
}
