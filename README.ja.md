# Geany SFTP Plugin

**言語**: [中文](README.md) | [English](README.en.md) | [한국어](README.ko.md) | **日本語**

Geany IDE用のリモートSFTPファイル管理プラグイン。C、GTK+3、libssh2で実装。

## 機能

- マルチサーバーSFTP接続管理（パスワード・キー認証）
- JSON設定保存（json-glib）
- サイドバーリモートファイルツリーブラウザ
- リアルタイム進行状況とキャンセル対応の非同期ファイルアップロード/ダウンロード
- スレッドセーフ転送（GMutex + g_atomic）
- 外部diffツールとのファイル同期（meld、kdiff3）
- 保存時自動アップロード
- 隠しファイル表示/非表示
- Geanyメニューとサイドバーに統合

## スクリーンショット

*近日追加予定*

## 依存関係

**必須**:
- GCC 4.8+
- Geany 1.36+
- GTK+3開発パッケージ
- libssh2 1.8+開発パッケージ
- GLib2開発パッケージ
- json-glib開発パッケージ
- Make

**オプション**: meld、kdiff3

## 対応プラットフォーム

- Linux（Ubuntu/Debian、Fedora/RHEL、Arch、openSUSE）
- macOS（Homebrew）
- Windows（MSYS2）

## ビルドとインストール

**自動インストール**（OS自動検出・依存関係インストール）：
```bash
./install.sh
```

**手動インストール**：
```bash
# Ubuntu/Debian
sudo apt-get install build-essential geany libgeany-dev libgtk-3-dev libssh2-1-dev libglib2.0-dev libjson-glib-dev

# Fedora/RHEL
sudo dnf install gcc make geany geany-devel gtk3-devel libssh2-devel glib2-devel json-glib-devel

# Arch
sudo pacman -S base-devel geany gtk3 libssh2 glib2 json-glib

# macOS
brew install geany gtk+3 libssh2 glib json-glib pkg-config

# ビルド＆インストール
make
sudo make install
```

## 使用方法

1. **プラグイン有効化**: ツール → プラグインマネージャー → "SFTP Client"をチェック
2. **接続設定**: プラグインマネージャー → SFTP Client → 設定
3. **ファイルブラウジング**: サイドバー"Remote Files"パネルを使用
4. **ファイル転送**: ダブルクリックでダウンロード、アップロードボタンを使用
5. **同期**: 右クリック → "リモートバージョンと同期"

## プロジェクト構造

```
sftp-plugin.h   - ヘッダー/型定義
compat.h        - クロスプラットフォーム互換レイヤー
sftp-plugin.c   - プラグインエントリーポイント、Geany API統合
connection.c    - SFTP接続、非同期転送
config.c        - JSON設定（json-glib）
ui.c            - GTK+3 UI、進行状況ダイアログ
sync.c          - ファイル同期とdiff
Makefile        - ビルドシステム（Linux/macOS/Windows）
install.sh      - インストールスクリプト（ディストロ自動検出）
```

## ライセンス

GPLv2

---

バージョン 1.0.0 | ~3000行のCコード