#!/usr/bin/env bash
set -euo pipefail

source Packaging/AppConfig.env

mkdir -p dist

if command -v cargo >/dev/null 2>&1; then
    echo "Building Rust Linux x86_64 binary -> dist/${APP_EXEC}..."
    cargo build --release
    cp -f target/release/diskdude "dist/${APP_EXEC}"

    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "Building Rust Windows x86_64 binary -> dist/${APP_WIN_EXEC}..."
        cargo build --release --target x86_64-pc-windows-gnu
        cp -f target/x86_64-pc-windows-gnu/release/diskdude.exe "dist/${APP_WIN_EXEC}"
    fi
else
    echo "Error: cargo is required to build DiskLens." >&2
    exit 1
fi

./scripts/make_icon.sh
cp -f Packaging/diskdude.desktop.template dist/diskdude.desktop
cp -f Packaging/Icon/diskdude.png dist/diskdude.png

echo "Build complete:"
ls -lh "dist/${APP_EXEC}" "dist/${APP_WIN_EXEC}" 2>/dev/null || ls -lh "dist/${APP_EXEC}"
