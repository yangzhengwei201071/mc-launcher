/* json_util.c */
#include "json_util.h"
#include <string.h>
#include <stdlib.h>

const char *json_get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) return item->valuestring;
    return NULL;
}

const char *current_os_name(void) {
#ifdef __linux__
    return "linux";
#elif _WIN32
    return "windows";
#elif __APPLE__
    return "osx";
#else
    return "linux";
#endif
}

bool rules_allow(cJSON *param_obj, const char *os_name) {
    cJSON *rules = cJSON_GetObjectItemCaseSensitive(param_obj, "rules");
    if (!cJSON_IsArray(rules)) return true;  /* 无 rules 即默认允许 */

    bool allowed = false;
    int n = cJSON_GetArraySize(rules);
    for (int i = 0; i < n; i++) {
        cJSON *rule = cJSON_GetArrayItem(rules, i);
        const char *action = json_get_string(rule, "action");
        cJSON *os = cJSON_GetObjectItemCaseSensitive(rule, "os");
        const char *rule_os = os ? json_get_string(os, "name") : NULL;

        if (rule_os && strcmp(rule_os, os_name) != 0) continue;

        if (action && strcmp(action, "allow") == 0) allowed = true;
        if (action && strcmp(action, "disallow") == 0) allowed = false;
    }
    return allowed;
}
