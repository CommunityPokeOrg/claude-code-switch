#pragma once

// Early boot diagnostics. Every stage of startup is logged to stderr
// (visible over nxlink) and, on Switch, to an SD-card log file so a hang
// or crash can be diagnosed after the fact without a connected PC.
// The log is rewritten on each launch.

void diag_init(void);
void diag_log(const char *fmt, ...);
void diag_close(void);
