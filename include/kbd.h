#pragma once

#include <stdbool.h>
#include <stddef.h>

// Open the Switch software keyboard applet to edit `buf`.
// `buf` is used as the initial text and receives the result.
// Returns true if the user confirmed (OK / Enter), false on cancel/error.
bool kbd_prompt(char *buf, size_t buf_size, const char *header,
                const char *guide);

// USB keyboard state: call once per frame.
// Returns characters typed this frame via `out` (NUL-terminated); the caller
// also checks helper functions for special keys pressed this frame.
void kbd_poll_begin(void);
int  kbd_poll_chars(char *out, size_t out_size); // returns number written
bool kbd_pressed_enter(void);
bool kbd_pressed_backspace(void);
bool kbd_pressed_escape(void);

void kbd_init(void);
