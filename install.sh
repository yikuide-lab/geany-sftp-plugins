#!/bin/bash

# Geany SFTP Plugin - Install Script
# Supports: Ubuntu/Debian, Fedora/RHEL, Arch, openSUSE, macOS

set -e

VERSION="1.0.0"

echo "========================================="
echo "  Geany SFTP Plugin v${VERSION} Installer"
echo "========================================="
echo ""

# Detect OS
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
    elif [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
            ubuntu|debian|linuxmint|pop) OS="debian" ;;
            fedora|rhel|centos|rocky|alma) OS="fedora" ;;
            arch|manjaro|endeavouros) OS="arch" ;;
            opensuse*|sles) OS="suse" ;;
            *) OS="unknown" ;;
        esac
    else
        OS="unknown"
    fi
    echo "Detected: $OS"
}

# Install dependencies
install_deps() {
    echo "Installing dependencies..."
    case "$OS" in
        debian)
            sudo apt-get update
            sudo apt-get install -y build-essential geany \
                libgtk-3-dev libssh2-1-dev libglib2.0-dev libjson-glib-dev
            sudo apt-get install -y libgeany-dev || sudo apt-get install -y geany-dev
            ;;
        fedora)
            sudo dnf install -y gcc make geany geany-devel \
                gtk3-devel libssh2-devel glib2-devel json-glib-devel
            ;;
        arch)
            sudo pacman -S --needed --noconfirm base-devel geany \
                gtk3 libssh2 glib2 json-glib
            ;;
        suse)
            sudo zypper install -y gcc make geany geany-devel \
                gtk3-devel libssh2-devel glib2-devel json-glib-devel
            ;;
        macos)
            if ! command -v brew &> /dev/null; then
                echo "Error: Homebrew required. Install from https://brew.sh"
                exit 1
            fi
            brew install geany gtk+3 libssh2 glib json-glib pkg-config
            ;;
        *)
            echo "Unknown OS. Please install manually:"
            echo "  geany (dev), gtk+3 (dev), libssh2 (dev), glib2 (dev), json-glib (dev)"
            echo ""
            read -p "Dependencies installed? Continue? [y/N] " -n 1 -r
            echo
            [[ $REPLY =~ ^[Yy]$ ]] || exit 0
            ;;
    esac
}

# Check dependencies
check_deps() {
    echo "Checking dependencies..."
    local missing=0
    for pkg in geany gtk+-3.0 libssh2 glib-2.0 json-glib-1.0; do
        if pkg-config --exists "$pkg" 2>/dev/null; then
            echo "  ✓ $pkg"
        else
            echo "  ✗ $pkg"
            missing=1
        fi
    done
    return $missing
}

detect_os

if ! check_deps 2>/dev/null; then
    echo ""
    read -p "Install missing dependencies? [Y/n] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
        install_deps
    else
        echo "Aborted."
        exit 1
    fi
fi

echo ""
echo "✓ All dependencies satisfied"
echo ""

# Build & install
read -p "Build and install? [Y/n] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]] && [[ -n $REPLY ]]; then
    echo "Cancelled."
    exit 0
fi

echo ""
make clean
make

echo ""
if [[ "$OS" == "macos" ]]; then
    make install
else
    sudo make install
fi

PLUGINDIR="$(pkg-config --variable=libdir geany)/geany"
echo ""
echo "========================================="
echo "  Install complete!"
echo "========================================="
echo "  Plugin dir: ${PLUGINDIR}"
echo ""
echo "  Next: Restart Geany → Tools → Plugin Manager → Enable 'SFTP Client'"
echo ""
