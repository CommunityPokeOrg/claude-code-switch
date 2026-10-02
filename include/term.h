#pragma once

#include <stddef.h>
#include <stdbool.h>

#define TERM_COLS 80
#define TERM_ROWS 45
#define TERM_MAX_LINES 512
#define TERM_STATUS_ROW 0
#define TERM_BODY_TOP 1
#define TERM_BODY_ROWS (TERM_ROWS - 3)
#define TERM_COMPOSE_ROW (TERM_ROWS - 2)
#define TERM_HINT_ROW (TERM_ROWS - 1)

typedef enum {
    LINE_SYS = 0,
    LINE_USER,
    LINE_ASSISTANT,
    LINE_ERROR,
    LINE_DIM,
} LineKind;

typedef struct {
    char lines[TERM_MAX_LINES][TERM_COLS + 1];
    LineKind kinds[TERM_MAX_LINES];
    int count;
    int scroll; // 0 = pinned to bottom
    bool dirty;
} TermBuffer;

void term_init(TermBuffer *tb);
void term_add(TermBuffer *tb, LineKind kind, const char *text);
void term_add_fmt(TermBuffer *tb, LineKind kind, const char *fmt, ...);
void term_scroll(TermBuffer *tb, int delta);
void term_scroll_to_bottom(TermBuffer *tb);
void term_render(TermBuffer *tb, const char *status, const char *compose,
                 const char *hints);
