#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "settings.h"
#include "cJSON.h"

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    snprintf(s->base_url, sizeof(s->base_url), "%s", DEFAULT_BASE_URL);
    snprintf(s->model, sizeof(s->model), "%s", DEFAULT_MODEL);
}

static void copy_str(char *dst, size_t dst_size, const cJSON *item) {
    if (cJSON_IsString(item) && item->valuestring)
        snprintf(dst, dst_size, "%s", item->valuestring);
}

bool settings_load(Settings *s) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 64 * 1024) { fclose(f); return false; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[rd] = 0;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    copy_str(s->api_key, sizeof(s->api_key),
             cJSON_GetObjectItemCaseSensitive(root, "api_key"));
    copy_str(s->base_url, sizeof(s->base_url),
             cJSON_GetObjectItemCaseSensitive(root, "base_url"));
    copy_str(s->model, sizeof(s->model),
             cJSON_GetObjectItemCaseSensitive(root, "model"));

    if (s->base_url[0] == 0)
        snprintf(s->base_url, sizeof(s->base_url), "%s", DEFAULT_BASE_URL);
    if (s->model[0] == 0)
        snprintf(s->model, sizeof(s->model), "%s", DEFAULT_MODEL);

    cJSON_Delete(root);
    return true;
}

bool settings_save(const Settings *s) {
    mkdir("sdmc:/config", 0777);
    mkdir(SETTINGS_DIR, 0777);

    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "api_key", s->api_key);
    cJSON_AddStringToObject(root, "base_url", s->base_url);
    cJSON_AddStringToObject(root, "model", s->model);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return false;

    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) { free(text); return false; }
    fputs(text, f);
    fclose(f);
    free(text);
    return true;
}
