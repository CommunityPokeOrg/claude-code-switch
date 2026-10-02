#pragma once

#define SETTINGS_PATH "sdmc:/config/claude-code-switch/settings.json"
#define SETTINGS_DIR  "sdmc:/config/claude-code-switch"

#define MAX_API_KEY_LEN  191
#define MAX_BASE_URL_LEN 255
#define MAX_MODEL_LEN    63

#define DEFAULT_BASE_URL "https://api.anthropic.com"
#define DEFAULT_MODEL    "claude-sonnet-4-5"

typedef struct {
    char api_key[MAX_API_KEY_LEN + 1];
    char base_url[MAX_BASE_URL_LEN + 1];
    char model[MAX_MODEL_LEN + 1];
} Settings;

void settings_defaults(Settings *s);
// Returns true if a settings file existed and was parsed.
bool settings_load(Settings *s);
// Returns true on successful write.
bool settings_save(const Settings *s);
