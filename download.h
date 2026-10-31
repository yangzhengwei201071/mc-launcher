/* download.h */
#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stddef.h>

/* 下载 URL 到文件，成功返回 0 */
int download_to_file(const char *url, const char *dest_path);

/* 下载 URL 到内存，调用者负责 free(*out) */
int download_to_memory(const char *url, char **out, size_t *out_len);

#endif

