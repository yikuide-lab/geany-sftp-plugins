# Geany SFTP Plugin

**语言 / Language**: [中文](README.md) | [English](README.en.md) | [한국어](README.ko.md) | [日本語](README.ja.md)

Geany IDE的SFTP远程文件管理插件，使用C语言、GTK+3和libssh2开发。

## 功能特性

- 多服务器SFTP连接管理（密码和密钥认证）
- JSON配置存储（json-glib）
- 侧边栏远程文件树浏览器
- 异步文件上传/下载，实时进度条和取消支持
- 线程安全传输（GMutex + g_atomic）
- 文件同步，支持外部diff工具（meld、kdiff3）
- 保存时自动上传
- 显示/隐藏文件选项
- 集成到Geany菜单和侧边栏

## 截图

*即将添加*

## 依赖要求

**必需**:
- GCC 4.8+
- Geany 1.36+
- GTK+3开发包
- libssh2 1.8+开发包
- GLib2开发包
- json-glib开发包
- Make

**可选**:
- meld, kdiff3（文件比较工具）

## 支持平台

- Linux（Ubuntu/Debian、Fedora/RHEL、Arch、openSUSE）
- macOS（Homebrew）
- Windows（MSYS2）

## 构建和安装

**一键安装**（自动检测系统并安装依赖）：
```bash
./install.sh
```

**手动安装**：
```bash
# Ubuntu/Debian
sudo apt-get install build-essential geany libgeany-dev libgtk-3-dev libssh2-1-dev libglib2.0-dev libjson-glib-dev

# Fedora/RHEL
sudo dnf install gcc make geany geany-devel gtk3-devel libssh2-devel glib2-devel json-glib-devel

# Arch
sudo pacman -S base-devel geany gtk3 libssh2 glib2 json-glib

# macOS
brew install geany gtk+3 libssh2 glib json-glib pkg-config

# 构建 & 安装
make
sudo make install
```

## 使用方法

1. 启动Geany → 工具 → 插件管理器 → 勾选"SFTP Client"
2. 配置连接：工具 → 插件 → SFTP → 配置
3. 在侧边栏"Remote Files"面板中连接和浏览文件
4. 双击文件下载，使用上传按钮上传文件

## 项目结构

```
sftp-plugin.h   - 头文件/类型定义
compat.h        - 跨平台兼容层
sftp-plugin.c   - 插件入口，Geany API集成
connection.c    - SFTP连接，异步传输
config.c        - JSON配置（json-glib）
ui.c            - GTK+3界面，进度对话框
sync.c          - 文件同步和diff
Makefile        - 构建系统（Linux/macOS/Windows）
install.sh      - 安装脚本（自动检测发行版）
```

## 许可证

GPLv2

---

版本: 1.0.0 | 代码行数: ~3000行C代码