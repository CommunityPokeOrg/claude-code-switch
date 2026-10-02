// Host-side screenshot harness: builds the app's real gfx.c + ui.c against
// desktop SDL2, seeds a demo conversation, and saves rendered frames to PNG
// for the README. Build with tools/hostshot/build.sh. Not part of the .nro.
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "ui.h"
#include "settings.h"

static int save_png(const char *path) {
    int rc = IMG_SavePNG((SDL_Surface *)gfx_frame_surface(), path);
    if (rc != 0) fprintf(stderr, "IMG_SavePNG %s: %s\n", path, IMG_GetError());
    return rc;
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "docs";
    char path[512];

    if (!gfx_init()) {
        fprintf(stderr, "gfx_init failed\n");
        return 1;
    }

    UiApp u;
    ui_init(&u);
    ui_set_context(&u, "claude-sonnet-4-5", true);

    UiInput in;
    memset(&in, 0, sizeof(in));
    UiOutput out;

    // Seed a conversation that shows off the bubble layout.
    ui_chat_add(&u, UI_LINE_SYS,
                "Claude Code Switch v0.2.0 - connected to api.anthropic.com");
    ui_chat_add(&u, UI_LINE_USER,
                "Explain what the Nintendo Switch's GPU is in one sentence.");
    ui_chat_add(&u, UI_LINE_ASSISTANT,
                "The Switch uses an NVIDIA Tegra X1 with a 256-core "
                "Maxwell GPU - the same architecture as desktop GTX 900 "
                "cards, clocked low enough to run on a tablet's thermal "
                "budget.");
    ui_chat_add(&u, UI_LINE_USER,
                "Can homebrew apps use it for 3D graphics?");
    ui_chat_add(&u, UI_LINE_ASSISTANT,
                "Yes - homebrew can target deko3d (a native low-level API), "
                "OpenGL ES via libEGL + nouveau mesa, or SDL2's renderer "
                "which this app uses for its UI.");
    ui_chat_add(&u, UI_LINE_ERROR,
                "Error: Network error: Couldn't resolve host name");
    ui_chat_add(&u, UI_LINE_DIM, "(request cancelled)");
    ui_chat_add(&u, UI_LINE_ASSISTANT,
                "Here's a longer reply to demonstrate wrapped text inside "
                "a bubble. The bubble hugs its content, wraps at about 70% "
                "of the chat width, and the log scrolls smoothly with "
                "touch, the d-pad, or the right stick. Labels mark who "
                "said what, and a typing indicator appears while Claude "
                "is thinking.");

    ui_compose_set(&u, "draft: how do I install this?");
    u.focus_visible = true;
    u.focus_col = 1; // Send button focused

    for (int i = 0; i < 40; i++) ui_frame(&u, &in, &out);
    snprintf(path, sizeof(path), "%s/chat.png", outdir);
    save_png(path);

    // Thinking state
    ui_set_requesting(&u, true);
    ui_compose_clear(&u);
    u.focus_col = 0;
    for (int i = 0; i < 40; i++) ui_frame(&u, &in, &out);
    snprintf(path, sizeof(path), "%s/thinking.png", outdir);
    save_png(path);

    // Settings screen
    ui_set_requesting(&u, false);
    Settings s;
    settings_defaults(&s);
    snprintf(s.api_key, sizeof(s.api_key), "sk-ant-api03-fakeexamplekey0000");
    ui_open_settings(&u);
    ui_settings_load(&u, &s);
    u.set_sel = 1;
    u.focus_visible = true;
    for (int i = 0; i < 40; i++) ui_frame(&u, &in, &out);
    snprintf(path, sizeof(path), "%s/settings.png", outdir);
    save_png(path);

    // Empty state with key set
    ui_chat_clear(&u);
    u.screen = 0;
    for (int i = 0; i < 20; i++) ui_frame(&u, &in, &out);
    snprintf(path, sizeof(path), "%s/empty.png", outdir);
    save_png(path);

    ui_quit(&u);
    gfx_quit();
    return 0;
}
