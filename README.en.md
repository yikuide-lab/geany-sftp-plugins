# Geany SFTP Plugin

**Language**: [中文](README.md) | [English](README.en.md) | [한국어](README.ko.md) | [日本語](README.ja.md)

SFTP remote file management plugin for Geany IDE, written in C with GTK+3 and libssh2.

## Features

- Multi-server SFTP connection management (password & key auth)
- JSON config storage (json-glib)
- Remote file tree browser in sidebar
- Async file upload/download with real-time progress bar & cancel support
- Thread-safe transfers (GMutex + g_atomic)
- File sync with external diff tools (meld, kdiff3)
- Auto-upload on save
- Show/hide hidden files
- Integrated into Geany menus & sidebar

## Screenshots

*Coming soon*

## Dependencies

**Required**:
- GCC 4.8+
- Geany 1.36+
- GTK+3 development packages
- libssh2 1.8+ development packages
- GLib2 development packages
- json-glib development packages
- Make

**Optional**:
- meld, kdiff3 (file comparison tools)

## Supported Platforms

- Linux (Ubuntu/Debian, Fedora/RHEL, Arch, openSUSE)
- macOS (Homebrew)
- Windows (MSYS2)

## Build & Install

**Quick install** (auto-detects OS and installs dependencies):
```bash
./install.sh
```

**Manual install**:
```bash
# Ubuntu/Debian
sudo apt-get install build-essential geany libgeany-dev libgtk-3-dev libssh2-1-dev libglib2.0-dev libjson-glib-dev

# Fedora/RHEL
sudo dnf install gcc make geany geany-devel gtk3-devel libssh2-devel glib2-devel json-glib-devel

# Arch
sudo pacman -S base-devel geany gtk3 libssh2 glib2 json-glib

# macOS
brew install geany gtk+3 libssh2 glib json-glib pkg-config

# Build & install
make
sudo make install
```

## Usage

1. Start Geany → Tools → Plugin Manager → Check "SFTP Client"
2. Configure connections: Tools → Plugins → SFTP → Configure
3. Connect and browse files in "Remote Files" sidebar panel
4. Double-click files to download, use upload button to upload files

## Project Structure

```
sftp-plugin.h   - Header / type definitions
compat.h        - Cross-platform compatibility layer
sftp-plugin.c   - Plugin entry, Geany API integration
connection.c    - SFTP connection, async transfer
config.c        - JSON config (json-glib)
ui.c            - GTK+3 UI, progress dialog
sync.c          - File sync & diff
Makefile        - Build system (Linux/macOS/Windows)
install.sh      - Install script (auto-detects distro)
```

## License

GPLv2

---

Version: 1.0.0 | ~3000 lines of C code