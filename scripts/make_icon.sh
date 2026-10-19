#!/usr/bin/env bash
set -euo pipefail

mkdir -p Packaging/Icon dist
gcc -O2 Packaging/Icon/make_icon.c -lm -o Packaging/Icon/make_icon_bin
./Packaging/Icon/make_icon_bin Packaging/Icon/diskdude.png
rm -f Packaging/Icon/make_icon_bin
echo "Generated Packaging/Icon/diskdude.png"
