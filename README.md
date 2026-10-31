# MC Launcher (C Language)

一个使用纯 C 语言编写的轻量级《我的世界》(Minecraft) 命令行启动器。

## 特性
- 纯 C 语言实现，无任何多余依赖
- 支持自动下载依赖库和客户端 Jar
- 自动解压 Natives
- 自动解析版本 JSON 并拼接 JVM 启动参数
- 支持离线模式启动

## 依赖安装 (CachyOS / Arch Linux)
```bash
sudo pacman -S base-devel curl cjson libzip
