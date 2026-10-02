#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "term.h"

// ANSI color for each line kind.
static const char *kind_color(LineKind k) {
    switch (k) {
        case LINE_USER:      return "36"; // cyan
        case LINE_ASSISTANT: return "37"; // white
        case LINE_ERROR:     return "31"; // red
        case LINE_DIM:       return "90"; // bright black
        case LINE_SYS:
        default:             return "33"; // yellow
    }
}

void term_init(TermBuffer *tb) {
    memset(tb, 0, sizeof(*tb));
    tb->dirty = true;
}

static void push_line(TermBuffer *tb, LineKind kind, const char *text) {
    if (tb->count == TERM_MAX_LINES) {
        memmove(tb->lines[0], tb->lines[1],
                (TERM_MAX_LINES - 1) * (TERM_COLS + 1));
        memmove(tb->kinds, tb->kinds + 1, (TERM_MAX_LINES - 1) * sizeof(LineKind));
        tb->count--;
    }
    snprintf(tb->lines[tb->count], TERM_COLS + 1, "%s", text);
    tb->kinds[tb->count] = kind;
    tb->count++;
    tb->dirty = true;
}

void term_add(TermBuffer *tb, LineKind kind, const char *text) {
    // Word-wrap text into TERM_COLS-wide lines; honor embedded newlines.
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t seg_len = nl ? (size_t)(nl - p) : strlen(p);

        while (seg_len > TERM_COLS) {
            size_t cut = TERM_COLS;
            // Prefer breaking at a space.
            size_t best = cut;
            for (size_t i = cut; i > cut - 20 && i > 0; i--) {
                if (p[i - 1] == ' ') { best = i - 1; break; }
            }
            if (best == cut) {
                cut = TERM_COLS;
            } else {
                cut = best;
            }
            char buf[TERM_COLS + 1];
            memcpy(buf, p, cut);
            buf[cut] = 0;
            push_line(tb, kind, buf);
            p += cut;
            while (*p == ' ') p++;
            seg_len = nl ? (size_t)(nl - p) : strlen(p);
        }

        char buf[TERM_COLS + 1];
        size_t n = seg_len > TERM_COLS ? TERM_COLS : seg_len;
        memcpy(buf, p, n);
        buf[n] = 0;
        push_line(tb, kind, buf);

        if (!nl) break;
        p = nl + 1;
    }
    if (text[0] == 0) push_line(tb, kind, "");
}

void term_add_fmt(TermBuffer *tb, LineKind kind, const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    term_add(tb, kind, buf);
}

static int max_scroll(const TermBuffer *tb) {
    int m = tb->count - TERM_BODY_ROWS;
    return m < 0 ? 0 : m;
}

void term_scroll(TermBuffer *tb, int delta) {
    tb->scroll += delta;
    if (tb->scroll < 0) tb->scroll = 0;
    if (tb->scroll > max_scroll(tb)) tb->scroll = max_scroll(tb);
    tb->dirty = true;
}

void term_scroll_to_bottom(TermBuffer *tb) {
    tb->scroll = 0;
    tb->dirty = true;
}

void term_render(TermBuffer *tb, const char *status, const char *compose,
                 const char *hints) {
    consoleClear();

    // Status bar (inverted).
    char bar[TERM_COLS + 1];
    snprintf(bar, sizeof(bar), "%-*.*s", TERM_COLS, TERM_COLS, status);
    printf("\x1b[7m%s\x1b[0m\n", bar);

    // Body: show [count - scroll - BODY_ROWS, count - scroll).
    int end = tb->count - tb->scroll;
    int start = end - TERM_BODY_ROWS;
    if (start < 0) start = 0;
    for (int i = 0; i < TERM_BODY_ROWS; i++) {
        int idx = start + i;
        if (idx < end) {
            printf("\x1b[%sm%s\x1b[0m\n", kind_color(tb->kinds[idx]),
                   tb->lines[idx]);
        } else {
            printf("\n");
        }
    }

    // Compose line.
    char comp[TERM_COLS + 1];
    snprintf(comp, sizeof(comp), "%-*.*s", TERM_COLS, TERM_COLS, compose);
    printf("\x1b[32m%s\x1b[0m\n", comp);

    // Hint bar (inverted).
    char hint[TERM_COLS + 1];
    snprintf(hint, sizeof(hint), "%-*.*s", TERM_COLS, TERM_COLS, hints);
    printf("\x1b[7m%s\x1b[0m", hint);

    consoleUpdate(NULL);
    tb->dirty = false;
}
