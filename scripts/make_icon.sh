#!/usr/bin/env bash
set -euo pipefail

mkdir -p Packaging/Icon dist
gcc -O2 Packaging/Icon/make_icon.c -lm -o Packaging/Icon/make_icon_bin
./Packaging/Icon/make_icon_bin Packaging/Icon/disksonar.png
rm -f Packaging/Icon/make_icon_bin
echo "Generated Packaging/Icon/disksonar.png"

if command -v convert >/dev/null 2>&1; then
    mkdir -p Packaging/Windows
    convert Packaging/Icon/disksonar.png -define icon:auto-resize=256,128,64,48,32,16 Packaging/Windows/disksonar.ico
    echo "Generated Packaging/Windows/disksonar.ico"
fi
