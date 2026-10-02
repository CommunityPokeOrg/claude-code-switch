#include <switch.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include "net.h"
#include "cJSON.h"
#include "diag.h"

void conv_init(Conversation *c) {
    memset(c, 0, sizeof(*c));
}

void conv_add(Conversation *c, const char *role, const char *content) {
    if (c->count == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 16;
        ChatMessage *nm = realloc(c->messages, ncap * sizeof(ChatMessage));
        if (!nm) return;
        c->messages = nm;
        c->cap = ncap;
    }
    c->messages[c->count].role = strdup(role);
    c->messages[c->count].content = strdup(content);
    c->count++;
}

void conv_pop(Conversation *c) {
    if (c->count == 0) return;
    c->count--;
    free(c->messages[c->count].role);
    free(c->messages[c->count].content);
}

void conv_clear(Conversation *c) {
    for (size_t i = 0; i < c->count; i++) {
        free(c->messages[i].role);
        free(c->messages[i].content);
    }
    c->count = 0;
}

void conv_free(Conversation *c) {
    conv_clear(c);
    free(c->messages);
    c->messages = NULL;
    c->cap = 0;
}

// ---------------------------------------------------------------
// Worker thread state
// ---------------------------------------------------------------

typedef struct {
    NetJob *job;
    Settings settings;
    Conversation conv;
    atomic_bool cancel;
    atomic_int state;
} Worker;

static Worker g_worker;
static Thread g_thread;
static bool g_thread_started = false;

// ---------------------------------------------------------------
// Lazy socket/curl init: the BSD service is only brought up when the
// first request is sent, so startup never blocks on it. It is a local
// service connect (fast), but keeping it off the boot path means a
// socket-init failure or hang in an emulator can never freeze the UI.
// ---------------------------------------------------------------

static bool g_sock_ready = false;
static bool g_sock_failed = false;

static bool net_ensure_init(void) {
    if (g_sock_ready) return true;
    if (g_sock_failed) return false;
    diag_log("net: socketInitializeDefault...");
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        diag_log("net: socketInitializeDefault failed: 0x%x", rc);
        g_sock_failed = true;
        return false;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_sock_ready = true;
    diag_log("net: sockets + curl ready");
    return true;
}

void net_close(void) {
    if (g_sock_ready) {
        socketExit();
        g_sock_ready = false;
    }
}

typedef struct {
    char *data;
    size_t size;
    size_t cap;
} MemBuf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    MemBuf *m = userdata;
    size_t n = size * nmemb;
    if (n == 0) return 0;
    if (m->size + n + 1 > m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 8192;
        while (ncap < m->size + n + 1) ncap *= 2;
        if (ncap > 4 * 1024 * 1024) return 0; // cap responses at 4 MB
        char *nd = realloc(m->data, ncap);
        if (!nd) return 0;
        m->data = nd;
        m->cap = ncap;
    }
    memcpy(m->data + m->size, ptr, n);
    m->size += n;
    m->data[m->size] = 0;
    return n;
}

static int xfer_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                   curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    Worker *w = clientp;
    return atomic_load(&w->cancel) ? 1 : 0;
}

static char *build_request_body(Worker *w) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", w->settings.model);
    cJSON_AddNumberToObject(root, "max_tokens", 2048);
    cJSON_AddStringToObject(root, "system",
        "You are Claude, an AI coding assistant accessed through a "
        "terminal-style homebrew client on Nintendo Switch. "
        "Reply concisely in plain text. Formatting beyond newlines "
        "will not render.");

    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");
    for (size_t i = 0; i < w->conv.count; i++) {
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", w->conv.messages[i].role);
        cJSON_AddStringToObject(m, "content", w->conv.messages[i].content);
        cJSON_AddItemToArray(msgs, m);
    }

    cJSON_AddBoolToObject(root, "stream", 0);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

static void extract_response(Worker *w, const char *json_text) {
    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        snprintf(w->job->error, sizeof(w->job->error),
                 "Failed to parse API response JSON");
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsObject(err)) {
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(err, "message");
        cJSON *type = cJSON_GetObjectItemCaseSensitive(err, "type");
        snprintf(w->job->error, sizeof(w->job->error), "API error (%s): %s",
                 cJSON_IsString(type) ? type->valuestring : "?",
                 cJSON_IsString(msg) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
    if (!cJSON_IsArray(content)) {
        snprintf(w->job->error, sizeof(w->job->error),
                 "Unexpected API response shape (no content array)");
        cJSON_Delete(root);
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    w->job->result[0] = 0;
    size_t off = 0;
    cJSON *block;
    cJSON_ArrayForEach(block, content) {
        cJSON *type = cJSON_GetObjectItemCaseSensitive(block, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0) {
            cJSON *text = cJSON_GetObjectItemCaseSensitive(block, "text");
            if (cJSON_IsString(text)) {
                int n = snprintf(w->job->result + off,
                                 sizeof(w->job->result) - off, "%s%s",
                                 off ? "\n" : "", text->valuestring);
                if (n > 0) off += (size_t)n;
                if (off >= sizeof(w->job->result)) off = sizeof(w->job->result) - 1;
            }
        }
    }

    cJSON_Delete(root);
    if (off == 0)
        snprintf(w->job->result, sizeof(w->job->result), "(empty response)");
    atomic_store(&w->state, REQ_DONE);
}

static void worker_main(void *arg) {
    Worker *w = arg;

    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", w->settings.base_url);

    char *body = build_request_body(w);
    if (!body) {
        snprintf(w->job->error, sizeof(w->job->error),
                 "Out of memory building request");
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(body);
        snprintf(w->job->error, sizeof(w->job->error), "curl_easy_init failed");
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    char api_hdr[320];
    snprintf(api_hdr, sizeof(api_hdr), "x-api-key: %s", w->settings.api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "content-type: application/json");
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, api_hdr);

    MemBuf resp = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, w);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "claude-code-switch/0.2");

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &w->job->http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);

    if (atomic_load(&w->cancel)) {
        free(resp.data);
        atomic_store(&w->state, REQ_CANCELLED);
        return;
    }

    if (rc != CURLE_OK) {
        snprintf(w->job->error, sizeof(w->job->error),
                 "Network error: %s", curl_easy_strerror(rc));
        free(resp.data);
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    if (!resp.data || resp.size == 0) {
        snprintf(w->job->error, sizeof(w->job->error),
                 "Empty response (HTTP %ld)", (long)w->job->http_code);
        free(resp.data);
        atomic_store(&w->state, REQ_ERROR);
        return;
    }

    extract_response(w, resp.data);
    free(resp.data);
}

bool net_send_async(NetJob *job, const Conversation *conv, const Settings *s) {
    if (g_thread_started && atomic_load(&g_worker.state) == REQ_RUNNING)
        return false;

    memset(job, 0, sizeof(*job));
    if (!net_ensure_init()) {
        snprintf(job->error, sizeof(job->error),
                 "Network unavailable (socketInitialize failed)");
        job->state = REQ_ERROR;
        return false;
    }
    memset(&g_worker, 0, sizeof(g_worker));
    g_worker.job = job;
    g_worker.settings = *s;
    conv_init(&g_worker.conv);
    for (size_t i = 0; i < conv->count; i++)
        conv_add(&g_worker.conv, conv->messages[i].role,
                 conv->messages[i].content);
    atomic_store(&g_worker.state, REQ_RUNNING);
    atomic_store(&g_worker.cancel, false);

    Result rc = threadCreate(&g_thread, worker_main, &g_worker, NULL,
                             256 * 1024, 0x2C, -2);
    if (R_FAILED(rc)) {
        snprintf(job->error, sizeof(job->error),
                 "Failed to spawn request thread (0x%x)", rc);
        job->state = REQ_ERROR;
        return false;
    }
    rc = threadStart(&g_thread);
    if (R_FAILED(rc)) {
        snprintf(job->error, sizeof(job->error),
                 "Failed to start request thread (0x%x)", rc);
        job->state = REQ_ERROR;
        threadClose(&g_thread);
        return false;
    }
    g_thread_started = true;
    job->state = REQ_RUNNING;
    return true;
}

ReqState net_poll(const NetJob *job) {
    (void)job;
    if (!g_thread_started) return REQ_IDLE;
    return (ReqState)atomic_load(&g_worker.state);
}

void net_cancel(NetJob *job) {
    (void)job;
    if (g_thread_started && atomic_load(&g_worker.state) == REQ_RUNNING)
        atomic_store(&g_worker.cancel, true);
}

void net_finish(NetJob *job) {
    (void)job;
    if (g_thread_started) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_thread_started = false;
        conv_free(&g_worker.conv);
    }
}
