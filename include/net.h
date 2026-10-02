#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "settings.h"

// Conversation message stored in RAM (not persisted).
typedef struct {
    char *role;     // "user" or "assistant"
    char *content;  // message text (UTF-8)
} ChatMessage;

typedef struct {
    ChatMessage *messages;
    size_t count;
    size_t cap;
} Conversation;

void conv_init(Conversation *c);
void conv_add(Conversation *c, const char *role, const char *content);
void conv_pop(Conversation *c);
void conv_clear(Conversation *c);
void conv_free(Conversation *c);

typedef enum {
    REQ_IDLE = 0,
    REQ_RUNNING,
    REQ_DONE,
    REQ_ERROR,
    REQ_CANCELLED,
} ReqState;

typedef struct {
    ReqState state;          // written by worker, read by UI (use net_poll)
    char result[32768];      // assistant text or error description
    char error[512];
    long http_code;
} NetJob;

// Start a worker thread POSTing the conversation to the Anthropic Messages API.
// The worker snapshot-copies the conversation, settings at call time.
// Returns false if a job is already running or the thread failed to start.
bool net_send_async(NetJob *job, const Conversation *conv, const Settings *s);

// Current state of the job (plain read is fine; field is volatile-updated).
ReqState net_poll(const NetJob *job);

// Ask the running request to abort (curl progress callback observes this).
void net_cancel(NetJob *job);

// After consuming a finished job (DONE/ERROR/CANCELLED), wait for the thread
// to fully exit and reset the job to REQ_IDLE.
void net_finish(NetJob *job);

// Tear down the socket service if it was lazily initialized.
void net_close(void);
