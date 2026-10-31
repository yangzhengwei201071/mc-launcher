/* json_util.h */
#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <cjson/cJSON.h>
#include <stdbool.h>

/* 读取字符串字段，不存在返回 NULL */
const char *json_get_string(cJSON *obj, const char *key);

/* 判断一个带 rules 的参数对象是否适用于当前系统 */
bool rules_allow(cJSON *param_obj, const char *os_name);

/* 获取当前系统的 os name（linux / windows / osx） */
const char *current_os_name(void);

#endif
