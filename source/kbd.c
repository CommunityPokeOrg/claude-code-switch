#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "kbd.h"

bool kbd_prompt(char *buf, size_t buf_size, const char *header,
                const char *guide) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, buf);
    swkbdConfigSetOkButtonText(&kbd, "Send");

    char tmp[1024];
    if (buf_size < sizeof(tmp)) {
        snprintf(tmp, sizeof(tmp), "%s", buf);
    } else {
        snprintf(tmp, sizeof(tmp), "%.*s", (int)sizeof(tmp) - 1, buf);
    }

    rc = swkbdShow(&kbd, tmp, sizeof(tmp));
    swkbdClose(&kbd);

    if (R_FAILED(rc)) return false;
    snprintf(buf, buf_size, "%s", tmp);
    return true;
}

// ---------------------------------------------------------------
// USB keyboard (official USB HID keyboard support, fw 9.0.0+)
// ---------------------------------------------------------------

static HidKeyboardState s_prev;
static bool s_prev_valid = false;
static bool s_enter, s_backspace, s_escape;

void kbd_init(void) {
    hidInitializeKeyboard();
    memset(&s_prev, 0, sizeof(s_prev));
}

void kbd_poll_begin(void) {
    s_enter = s_backspace = s_escape = false;
}

static bool key_newly_down(const HidKeyboardState *cur, int key) {
    bool now = hidKeyboardStateGetKey(cur, (HidKeyboardKey)key);
    bool before = s_prev_valid &&
                  hidKeyboardStateGetKey(&s_prev, (HidKeyboardKey)key);
    return now && !before;
}

// US layout scancode -> char tables.
static char scancode_to_char(int sc, bool shift) {
    if (sc >= HidKeyboardKey_A && sc <= HidKeyboardKey_Z)
        return (char)('a' + (sc - HidKeyboardKey_A) +
                      (shift ? 'A' - 'a' : 0));

    if (sc >= HidKeyboardKey_D1 && sc <= HidKeyboardKey_D9) {
        char c = (char)('1' + (sc - HidKeyboardKey_D1));
        if (shift) {
            const char *shifted = "!@#$%^&*(";
            return shifted[c - '1'];
        }
        return c;
    }
    if (sc == HidKeyboardKey_D0) return shift ? ')' : '0';
    if (sc == HidKeyboardKey_Space) return ' ';
    if (sc == HidKeyboardKey_Minus) return shift ? '_' : '-';
    if (sc == HidKeyboardKey_Plus) return shift ? '+' : '=';
    if (sc == HidKeyboardKey_OpenBracket) return shift ? '{' : '[';
    if (sc == HidKeyboardKey_CloseBracket) return shift ? '}' : ']';
    if (sc == HidKeyboardKey_Pipe) return shift ? '|' : '\\';
    if (sc == HidKeyboardKey_Tilde) return shift ? '~' : '`';
    if (sc == HidKeyboardKey_Semicolon) return shift ? ':' : ';';
    if (sc == HidKeyboardKey_Quote) return shift ? '"' : '\'';
    if (sc == HidKeyboardKey_Backquote) return shift ? '~' : '`';
    if (sc == HidKeyboardKey_Comma) return shift ? '<' : ',';
    if (sc == HidKeyboardKey_Period) return shift ? '>' : '.';
    if (sc == HidKeyboardKey_Slash) return shift ? '?' : '/';
    if (sc == HidKeyboardKey_Tab) return ' ';
    return 0;
}

int kbd_poll_chars(char *out, size_t out_size) {
    HidKeyboardState cur;
    if (hidGetKeyboardStates(&cur, 1) == 0) {
        s_prev_valid = false;
        if (out_size) out[0] = 0;
        return 0;
    }

    bool shift = (cur.modifiers & HidKeyboardModifier_Shift) != 0;
    size_t n = 0;

    for (int sc = HidKeyboardKey_A; sc <= HidKeyboardKey_UpArrow; sc++) {
        if (!key_newly_down(&cur, sc)) continue;
        if (sc == HidKeyboardKey_Return || sc == HidKeyboardKey_NumPadEnter) {
            s_enter = true;
            continue;
        }
        if (sc == HidKeyboardKey_Backspace) { s_backspace = true; continue; }
        if (sc == HidKeyboardKey_Escape) { s_escape = true; continue; }
        char c = scancode_to_char(sc, shift);
        if (c && n + 1 < out_size) out[n++] = c;
    }
    out[n] = 0;

    s_prev = cur;
    s_prev_valid = true;
    return (int)n;
}

bool kbd_pressed_enter(void)     { return s_enter; }
bool kbd_pressed_backspace(void) { return s_backspace; }
bool kbd_pressed_escape(void)    { return s_escape; }
