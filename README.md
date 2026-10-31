# mc-launcher
. 写清楚“依赖说明”

因为你是用 C 语言写的，动态链接了 libcurl、cjson 和 libzip。即使小白下载了你的二进制文件，如果他的 Linux 系统里没装这三个库，双击运行一样会报错 error while loading shared libraries。
所以在 Release 的说明里一定要写：

    运行前请确保安装了以下依赖：
    Ubuntu/Debian: sudo apt install libcurl4 libcjson1 libzip4
    Arch/CachyOS: sudo pacman -S curl cjson libzip
