/* natives.c */
#include "natives.h"
#include <zip.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int is_native(const char *name) {
    size_t len = strlen(name);
    return (len > 3 && strcmp(name + len - 3, ".so") == 0) ||
           (len > 4 && strcmp(name + len - 4, ".dll") == 0) ||
           (len > 6 && strcmp(name + len - 6, ".dylib") == 0);
}

int extract_natives(const char *jar_path, const char *dest_dir) {
    int err = 0;
    zip_t *za = zip_open(jar_path, ZIP_RDONLY, &err);
    if (!za) return -1;

    mkdir(dest_dir, 0755);

    zip_int64_t num = zip_get_num_entries(za, 0);
    for (zip_uint64_t i = 0; i < (zip_uint64_t)num; i++) {
        const char *name = zip_get_name(za, i, 0);
        if (!name || !is_native(name)) continue;

        /* 只取文件名部分，忽略 jar 内路径 */
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;

        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, base);

        zip_file_t *zf = zip_fopen_index(za, i, 0);
        if (!zf) continue;

        FILE *fp = fopen(out_path, "wb");
        if (fp) {
            char buf[8192];
            zip_int64_t n;
            while ((n = zip_fread(zf, buf, sizeof(buf))) > 0)
                fwrite(buf, 1, (size_t)n, fp);
            fclose(fp);
        }
        zip_fclose(zf);
    }

    zip_close(za);
    return 0;
}

