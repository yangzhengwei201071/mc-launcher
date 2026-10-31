/* launch.c */
#include "launch.h"
#include "json_util.h"
#include "download.h"
#include "natives.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h> /*mkdir*/

extern char **environ;

/* ---------- 简单的字符串向量 ---------- */
typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrVec;

static void sv_init(StrVec *v) { v->items = NULL; v->len = 0; v->cap = 0; }

static void sv_push(StrVec *v, const char *s) {
    if (v->len + 1 >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->items = realloc(v->items, v->cap * sizeof(char *));
    }
    v->items[v->len++] = strdup(s);
    v->items[v->len] = NULL;
}

static void sv_free(StrVec *v) {
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    free(v->items);
}

/* ---------- 占位符替换 ---------- */
static char *subst(const char *src, const char *placeholder, const char *value) {
    const char *pos = strstr(src, placeholder);
    if (!pos) return strdup(src);

    size_t plen = strlen(placeholder);
    size_t vlen = strlen(value);
    size_t new_len = strlen(src) - plen + vlen;

    char *out = malloc(new_len + 1);
    size_t prefix = (size_t)(pos - src);
    memcpy(out, src, prefix);
    memcpy(out + prefix, value, vlen);
    strcpy(out + prefix + vlen, pos + plen);
    return out;
}

/* 对单个参数字符串依次替换多个占位符 */
static char *subst_all(const char *src,
                       const char *player, const char *version_id,
                       const char *game_dir, const char *assets_dir,
                       const char *natives_dir, const char *classpath) {
    char *s = strdup(src);
    char *tmp;

    #define REPLACE(ph, val) do { \
        tmp = subst(s, ph, val); free(s); s = tmp; \
    } while (0)

    REPLACE("${auth_player_name}", player);
    REPLACE("${version_name}", version_id);
    REPLACE("${game_directory}", game_dir);
    REPLACE("${assets_root}", assets_dir);
    REPLACE("${natives_directory}", natives_dir);
    REPLACE("${classpath}", classpath);
    REPLACE("${auth_uuid}", "00000000-0000-0000-0000-000000000000");
    REPLACE("${auth_access_token}", "0");
    REPLACE("${user_type}", "legacy");
    REPLACE("${version_type}", "release");

    #undef REPLACE
    return s;
}

/* 从 arguments 数组中收集参数，处理 rules */
static void collect_arguments(cJSON *args_array, const char *os,
                              const char *player, const char *version_id,
                              const char *game_dir, const char *assets_dir,
                              const char *natives_dir, const char *classpath,
                              StrVec *out) {
    int n = cJSON_GetArraySize(args_array);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(args_array, i);
        if (cJSON_IsString(item)) {
            char *s = subst_all(item->valuestring, player, version_id,
                                game_dir, assets_dir, natives_dir, classpath);
            sv_push(out, s);
            free(s);
        } else if (cJSON_IsObject(item)) {
            if (!rules_allow(item, os)) continue;
            cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "value");
            if (cJSON_IsString(value)) {
                char *s = subst_all(value->valuestring, player, version_id,
                                    game_dir, assets_dir, natives_dir, classpath);
                sv_push(out, s);
                free(s);
            } else if (cJSON_IsArray(value)) {
                int vn = cJSON_GetArraySize(value);
                for (int j = 0; j < vn; j++) {
                    cJSON *v = cJSON_GetArrayItem(value, j);
                    if (cJSON_IsString(v)) {
                        char *s = subst_all(v->valuestring, player, version_id,
                                            game_dir, assets_dir, natives_dir, classpath);
                        sv_push(out, s);
                        free(s);
                    }
                }
            }
        }
    }
}

/* ---------- 构建 classpath ---------- */
static char *build_classpath(cJSON *version_json, const char *mc_dir,
                             const char *version) {
    StrVec cp;
    sv_init(&cp);

    cJSON *libraries = cJSON_GetObjectItemCaseSensitive(version_json, "libraries");
    if (cJSON_IsArray(libraries)) {
        int n = cJSON_GetArraySize(libraries);
        for (int i = 0; i < n; i++) {
            cJSON *lib = cJSON_GetArrayItem(libraries, i);
            cJSON *downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
            if (!downloads) continue;
            cJSON *artifact = cJSON_GetObjectItemCaseSensitive(downloads, "artifact");
            if (!artifact) continue;
            cJSON *path = cJSON_GetObjectItemCaseSensitive(artifact, "path");
            if (!cJSON_IsString(path)) continue;

            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/libraries/%s", mc_dir, path->valuestring);
            sv_push(&cp, full);
        }
    }

    char client_jar[PATH_MAX];
    snprintf(client_jar, sizeof(client_jar), "%s/versions/%s/%s.jar",
             mc_dir, version, version);
    sv_push(&cp, client_jar);

    /* 用冒号连接 */
    size_t total = 0;
    for (size_t i = 0; i < cp.len; i++) total += strlen(cp.items[i]) + 1;
    char *result = malloc(total + 1);
    result[0] = '\0';
    for (size_t i = 0; i < cp.len; i++) {
        if (i > 0) strcat(result, ":");
        strcat(result, cp.items[i]);
    }

    sv_free(&cp);
    return result;
}

/* ---------- 下载依赖库 ---------- */
static int download_libraries(cJSON *version_json, const char *mc_dir) {
    cJSON *libraries = cJSON_GetObjectItemCaseSensitive(version_json, "libraries");
    if (!cJSON_IsArray(libraries)) return 0;

    char lib_dir[PATH_MAX];
    snprintf(lib_dir, sizeof(lib_dir), "%s/libraries", mc_dir);

    int n = cJSON_GetArraySize(libraries);
    for (int i = 0; i < n; i++) {
        cJSON *lib = cJSON_GetArrayItem(libraries, i);

        /* 检查 rules（例如只适用于特定系统） */
        if (!rules_allow(lib, current_os_name())) continue;

        cJSON *downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
        if (!downloads) continue;
        cJSON *artifact = cJSON_GetObjectItemCaseSensitive(downloads, "artifact");
        if (!artifact) continue;

        const char *url = json_get_string(artifact, "url");
        const char *path = json_get_string(artifact, "path");
        if (!url || !path) continue;

        char dest[PATH_MAX];
        snprintf(dest, sizeof(dest), "%s/%s", lib_dir, path);

        /* 如果文件已存在则跳过（第一版不做 sha1 校验） */
        if (access(dest, F_OK) == 0) continue;

        /* 创建父目录 */
        char tmp[PATH_MAX];
        strcpy(tmp, dest);
        for (char *p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(tmp, 0755);
                *p = '/';
            }
        }

        printf("[lib] %s\n", path);
        if (download_to_file(url, dest) != 0)
            fprintf(stderr, "  !! 下载失败: %s\n", url);
    }
    return 0;
}

/* ---------- 下载客户端 jar ---------- */
static int download_client_jar(cJSON *version_json, const char *mc_dir,
                               const char *version) {
    cJSON *downloads = cJSON_GetObjectItemCaseSensitive(version_json, "downloads");
    if (!downloads) return 0;
    cJSON *client = cJSON_GetObjectItemCaseSensitive(downloads, "client");
    if (!client) return 0;

    const char *url = json_get_string(client, "url");
    if (!url) return 0;

    char dest[PATH_MAX];
    snprintf(dest, sizeof(dest), "%s/versions/%s/%s.jar", mc_dir, version, version);

    if (access(dest, F_OK) == 0) return 0;

    printf("[jar] %s\n", dest);
    return download_to_file(url, dest);
}

/* ---------- 主入口 ---------- */
int launch_minecraft(const char *mc_dir, const char *version,
                     const char *username, const char *java_path,
                     const char *natives_dir) {
    const char *os = current_os_name();

    /* 1. 读取版本 JSON */
    char version_json_path[PATH_MAX];
    snprintf(version_json_path, sizeof(version_json_path),
             "%s/versions/%s/%s.json", mc_dir, version, version);

    FILE *fp = fopen(version_json_path, "rb");
    if (!fp) {
        fprintf(stderr, "找不到版本 JSON: %s\n", version_json_path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *json_text = malloc((size_t)fsize + 1);
    fread(json_text, 1, (size_t)fsize, fp);
    json_text[fsize] = '\0';
    fclose(fp);

    cJSON *version_json = cJSON_Parse(json_text);
    free(json_text);
    if (!version_json) {
        fprintf(stderr, "版本 JSON 解析失败\n");
        return 1;
    }

    /* 2. 下载依赖库和客户端 jar */
    printf("== 下载依赖库 ==\n");
    download_libraries(version_json, mc_dir);
    printf("== 下载客户端 jar ==\n");
    download_client_jar(version_json, mc_dir, version);

    /* 3. 解压 natives */
    printf("== 提取 natives ==\n");
    cJSON *libraries = cJSON_GetObjectItemCaseSensitive(version_json, "libraries");
    if (cJSON_IsArray(libraries)) {
        int n = cJSON_GetArraySize(libraries);
        for (int i = 0; i < n; i++) {
            cJSON *lib = cJSON_GetArrayItem(libraries, i);
            if (!rules_allow(lib, os)) continue;

            cJSON *downloads = cJSON_GetObjectItemCaseSensitive(lib, "downloads");
            if (!downloads) continue;
            cJSON *classifiers = cJSON_GetObjectItemCaseSensitive(downloads, "classifiers");
            if (!classifiers) continue;

            /* 查找 natives-linux / natives-windows / natives-osx */
            char classifier_key[64];
            snprintf(classifier_key, sizeof(classifier_key), "natives-%s", os);
            cJSON *native_artifact = cJSON_GetObjectItemCaseSensitive(classifiers, classifier_key);
            if (!native_artifact) continue;

            const char *path = json_get_string(native_artifact, "path");
            if (!path) continue;

            char jar_path[PATH_MAX];
            snprintf(jar_path, sizeof(jar_path), "%s/libraries/%s", mc_dir, path);

            if (access(jar_path, F_OK) == 0) {
                extract_natives(jar_path, natives_dir);
            }
        }
    }

    /* 4. 构建 classpath */
    char *classpath = build_classpath(version_json, mc_dir, version);

    /* 5. 拼接 JVM 和游戏参数 */
    StrVec jvm_args, game_args;
    sv_init(&jvm_args);
    sv_init(&game_args);

    char game_dir[PATH_MAX], assets_dir[PATH_MAX];
    snprintf(game_dir, sizeof(game_dir), "%s", mc_dir);
    snprintf(assets_dir, sizeof(assets_dir), "%s/assets", mc_dir);

    /* 基础 JVM 参数 */
    sv_push(&jvm_args, "-Xmx2G");

    /* 直接收集 JSON 中的 jvm 参数 */
    cJSON *arguments = cJSON_GetObjectItemCaseSensitive(version_json, "arguments");
    if (arguments) {
        cJSON *jvm = cJSON_GetObjectItemCaseSensitive(arguments, "jvm");
        if (cJSON_IsArray(jvm))
            collect_arguments(jvm, os, username, version, game_dir,
                              assets_dir, natives_dir, classpath, &jvm_args);

        cJSON *game = cJSON_GetObjectItemCaseSensitive(arguments, "game");
        if (cJSON_IsArray(game))
            collect_arguments(game, os, username, version, game_dir,
                              assets_dir, natives_dir, classpath, &game_args);
    }

    /* 兼容旧版 minecraftArguments（≤1.12.2） */
    const char *legacy_args = json_get_string(version_json, "minecraftArguments");
    if (legacy_args) {
        char *copy = strdup(legacy_args);
        char *token = strtok(copy, " ");
        while (token) {
            char *s = subst_all(token, username, version, game_dir,
                                assets_dir, natives_dir, classpath);
            sv_push(&game_args, s);
            free(s);
            token = strtok(NULL, " ");
        }
        free(copy);
    }

    /* 确保 -cp 和主类存在 */
    bool has_cp = false;
    for (size_t i = 0; i < jvm_args.len; i++)
        if (strcmp(jvm_args.items[i], "-cp") == 0 ||
            strcmp(jvm_args.items[i], "-classpath") == 0) has_cp = true;

    StrVec final_args;
    sv_init(&final_args);

    /* java 可执行文件 */
    sv_push(&final_args, java_path);

    /* JVM 参数 */
    for (size_t i = 0; i < jvm_args.len; i++)
        sv_push(&final_args, jvm_args.items[i]);

    if (!has_cp) {
        sv_push(&final_args, "-cp");
        sv_push(&final_args, classpath);
    }

    /* 主类 */
    const char *main_class = json_get_string(version_json, "mainClass");
    if (!main_class) main_class = "net.minecraft.client.main.Main";
    sv_push(&final_args, main_class);

    /* 游戏参数 */
    for (size_t i = 0; i < game_args.len; i++)
        sv_push(&final_args, game_args.items[i]);

    /* 6. 打印并启动 */
    printf("\n== 启动命令 ==\n");
    for (size_t i = 0; i < final_args.len; i++)
        printf("%s ", final_args.items[i]);
    printf("\n\n");

    pid_t pid;
    int status;
    if (posix_spawn(&pid, java_path, NULL, NULL, final_args.items, environ) != 0) {
        perror("posix_spawn");
        status = 1;
    } else {
        waitpid(pid, &status, 0);
    }

    /* 清理 */
    sv_free(&jvm_args);
    sv_free(&game_args);
    sv_free(&final_args);
    free(classpath);
    cJSON_Delete(version_json);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

