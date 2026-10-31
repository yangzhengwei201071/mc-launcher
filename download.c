/* download.c */
#include "download.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t write_file_cb(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

static size_t write_mem_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    char **mem = (char **)userdata;
    size_t old_len = *mem ? strlen(*mem) : 0;
    char *new_ptr = realloc(*mem, old_len + realsize + 1);
    if (!new_ptr) return 0;
    memcpy(new_ptr + old_len, ptr, realsize);
    new_ptr[old_len + realsize] = '\0';
    *mem = new_ptr;
    return realsize;
}

int download_to_file(const char *url, const char *dest_path) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    FILE *fp = fopen(dest_path, "wb");
    if (!fp) { curl_easy_cleanup(curl); return -1; }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? 0 : -1;
}

int download_to_memory(const char *url, char **out, size_t *out_len) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    *out = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) { free(*out); *out = NULL; return -1; }
    if (out_len && *out) *out_len = strlen(*out);
    return 0;
}
