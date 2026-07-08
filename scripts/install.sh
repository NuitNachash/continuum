#!/bin/bash
set -e

INSTALL_PREFIX="${1:-/usr/local}"
CONFIG_DIR="$HOME/.config/continuum"

echo "Building Continuum..."
mkdir -p build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
make -j"$(nproc)"

echo "Installing binary to $INSTALL_PREFIX/bin..."
sudo make install

echo "Setting up config directory at $CONFIG_DIR..."
mkdir -p "$CONFIG_DIR"
if [ ! -f "$CONFIG_DIR/config.ini" ]; then
    cp ../config.example.ini "$CONFIG_DIR/config.ini"
    echo "Copied default config to $CONFIG_DIR/config.ini — edit this before running."
fi

echo ""
echo "Installation complete."
echo "Run 'continuum --help' to get started."
echo "Edit your config at: $CONFIG_DIR/config.ini"