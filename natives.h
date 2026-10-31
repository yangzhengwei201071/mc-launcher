/* natives.h */
#ifndef NATIVES_H
#define NATIVES_H

/* 从 jar_path 提取原生库到 dest_dir，成功返回 0 */
int extract_natives(const char *jar_path, const char *dest_dir);

#endif

