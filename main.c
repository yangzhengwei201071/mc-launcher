#include "launch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "用法: %s <游戏目录> <版本> <离线用户名> [java路径] [natives目录]\n"
        "示例: %s ~/.minecraft 1.20.4 Steve /usr/lib/jvm/java-17-openjdk/bin/java /tmp/mc-natives\n",
        prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 1; }

    const char *mc_dir  = argv[1];
    const char *version = argv[2];
    const char *user    = argv[3];
    const char *java    = argc > 4 ? argv[4] : "java";
    const char *natives = argc > 5 ? argv[5] : "/tmp/mc-natives";

    return launch_minecraft(mc_dir, version, user, java, natives);
}
