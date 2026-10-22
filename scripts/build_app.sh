#!/usr/bin/env bash
set -euo pipefail

source Packaging/AppConfig.env

mkdir -p dist

if [ -d "${HOME}/.cargo/bin" ]; then
    export PATH="${HOME}/.cargo/bin:${PATH}"
fi
if [ -x "/tmp/rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/cargo" ]; then
    export PATH="/tmp/rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin:/tmp/cargo/bin:${PATH}"
fi

TARGET="${1:-all}"

if command -v cargo >/dev/null 2>&1; then
    if [ "${TARGET}" = "linux" ] || [ "${TARGET}" = "all" ]; then
        echo "Building Rust Linux x86_64 binary -> dist/${APP_EXEC}..."
        cargo build --release
        cp -f "target/release/${APP_EXEC}" "dist/${APP_EXEC}"
    fi

    if [ "${TARGET}" = "windows" ] || [ "${TARGET}" = "all" ]; then
        if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            echo "Building Rust Windows x86_64 binary -> dist/${APP_WIN_EXEC}..."
            cargo build --release --target x86_64-pc-windows-gnu
            cp -f "target/x86_64-pc-windows-gnu/release/${APP_EXEC}.exe" "dist/${APP_WIN_EXEC}"
        fi
    fi
else
    SRCS="Sources/DiskSonar/DiskSonarApp.c \
    Sources/DiskSonar/Theme/Theme.c \
    Sources/DiskSonar/Utils/Utils.c \
    Sources/DiskSonar/Utils/Canvas.c \
    Sources/DiskSonar/Models/FileNode.c \
    Sources/DiskSonar/Scanning/DiskScanner.c \
    Sources/DiskSonar/Views/Views.c"

    if [ "${TARGET}" = "linux" ] || [ "${TARGET}" = "all" ]; then
        echo "Building Linux x86_64 binary -> dist/${APP_EXEC}..."
        gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
            ${SRCS} -lX11 -lpthread -ldl -lm -o "dist/${APP_EXEC}"
    fi

    if [ "${TARGET}" = "windows" ] || [ "${TARGET}" = "all" ]; then
        if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
            echo "Building Windows x86_64 binary -> dist/${APP_WIN_EXEC}..."
            x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
                ${SRCS} -lgdi32 -luser32 -lshell32 -lole32 -luuid -lcomdlg32 -lpsapi -lm \
                -mwindows -o "dist/${APP_WIN_EXEC}"
        fi
    fi
fi

./scripts/make_icon.sh
cp -f Packaging/disksonar.desktop.template "dist/${APP_EXEC}.desktop"
cp -f Packaging/Icon/disksonar.png "dist/${APP_EXEC}.png"

echo "Build complete:"
ls -lh "dist/${APP_EXEC}" "dist/${APP_WIN_EXEC}" 2>/dev/null || ls -lh "dist/${APP_EXEC}"
