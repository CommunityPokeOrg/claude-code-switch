#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "diag.h"

#define DIAG_DIR  "sdmc:/config/claude-code-switch"
#define DIAG_PATH "sdmc:/config/claude-code-switch/debug.log"

static FILE *g_log;

void diag_init(void) {
#ifdef __SWITCH__
    mkdir("sdmc:/config", 0777);
    mkdir(DIAG_DIR, 0777);
    g_log = fopen(DIAG_PATH, "w");
    if (!g_log)
        fprintf(stderr, "diag: could not open %s\n", DIAG_PATH);
#endif
}

void diag_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fputc('\n', g_log);
        fflush(g_log);
    }
}

void diag_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}
