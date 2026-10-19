#!/usr/bin/env bash
set -euo pipefail

source Packaging/AppConfig.env

mkdir -p dist

if [ -x "/tmp/rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/cargo" ]; then
    export PATH="/tmp/rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin:/tmp/cargo/bin:${PATH}"
    export RUSTUP_HOME="/tmp/rustup"
    export CARGO_HOME="/tmp/cargo"
fi

if command -v cargo >/dev/null 2>&1; then
    echo "Building Rust Linux x86_64 binary -> dist/${APP_EXEC}..."
    cargo build --release
    cp -f target/release/disklens "dist/${APP_EXEC}"

    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "Building Rust Windows x86_64 binary -> dist/${APP_WIN_EXEC}..."
        cargo build --release --target x86_64-pc-windows-gnu
        cp -f target/x86_64-pc-windows-gnu/release/disklens.exe "dist/${APP_WIN_EXEC}"
    fi
else
    SRCS="Sources/DiskDude/DiskDudeApp.c \
    Sources/DiskDude/Theme/Theme.c \
    Sources/DiskDude/Utils/Utils.c \
    Sources/DiskDude/Utils/Canvas.c \
    Sources/DiskDude/Models/FileNode.c \
    Sources/DiskDude/Scanning/DiskScanner.c \
    Sources/DiskDude/Views/Views.c"

    echo "Building Linux x86_64 binary -> dist/${APP_EXEC}..."
    gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
        ${SRCS} -lX11 -lpthread -ldl -lm -o "dist/${APP_EXEC}"

    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "Building Windows x86_64 binary -> dist/${APP_WIN_EXEC}..."
        x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
            ${SRCS} -lgdi32 -luser32 -lshell32 -lole32 -luuid -lcomdlg32 -lpsapi -lm \
            -mwindows -o "dist/${APP_WIN_EXEC}"
    fi
fi

./scripts/make_icon.sh
cp -f Packaging/disklens.desktop.template dist/disklens.desktop
cp -f Packaging/Icon/disklens.png dist/disklens.png

echo "Build complete:"
ls -lh "dist/${APP_EXEC}" "dist/${APP_WIN_EXEC}" 2>/dev/null || ls -lh "dist/${APP_EXEC}"
