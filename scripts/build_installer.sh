#!/usr/bin/env bash
set -euo pipefail

source Packaging/AppConfig.env

# Ensure Windows executable and icons exist
./scripts/build_app.sh windows

ISS_PATH="Packaging/Windows/disksonar.iss"

if command -v iscc >/dev/null 2>&1; then
    echo "Compiling Inno Setup installer using native iscc..."
    iscc "${ISS_PATH}"
elif command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    echo "Compiling Inno Setup installer using Docker (amake/innosetup)..."
    docker run --rm --security-opt seccomp=unconfined -v "${PWD}:/work" amake/innosetup "${ISS_PATH}" || {
        echo "Docker-based Inno Setup build completed with errors or unsupported syscalls."
        echo "Run native Inno Setup on Windows: iscc Packaging/Windows/disksonar.iss"
    }
else
    echo "Note: Inno Setup compiler (iscc) not found in local environment."
    echo "To compile the Windows installer, run Inno Setup on Windows or Wine:"
    echo "  iscc Packaging/Windows/disksonar.iss"
    exit 0
fi

if ls dist/*setup*.exe 1>/dev/null 2>&1; then
    echo "Installer generated successfully:"
    ls -lh dist/*setup*.exe
fi
