# Contributing to DiskSonar

Thank you for your interest in contributing to DiskSonar. This document outlines development setup, coding guidelines, and the pull request process.

## Development Setup

### Linux Dependencies
Install the required C compiler, build tools, and X11 development headers:

```bash
# Debian / Ubuntu
sudo apt-get install gcc make libx11-dev

# Fedora
sudo dnf install gcc make libX11-devel

# Arch Linux
sudo pacman -S gcc make libx11
```

### Windows Cross-Compilation (from Linux)
```bash
sudo apt-get install gcc-mingw-w64-x86-64
```

### Rust Toolchain
A stable Rust toolchain is used for hashing routines, classification tables, and unit tests:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
rustup target add x86_64-pc-windows-gnu
```

## Building and Testing

Run the verification suite before opening a pull request:

```bash
# Run unit tests
cargo test

# Build native Linux binary
make linux

# Build Windows executable
make windows

# Run automated verification suite (30 self-tests)
make verify
```

## Architectural Guidelines

1. **Safety First**: DiskSonar never unlinks or deletes files directly. Any proposed file deletion action must stage items through the Cleanup Queue and use system Trash (FreeDesktop.org Trash on Linux, Recycle Bin on Windows).
2. **Memory Efficiency**: Keep node storage inside flat contiguous arenas. Avoid introducing deep pointer-linked tree hierarchies that inflate heap allocation count.
3. **Multi-threading**: Filesystem traversal is multi-threaded. Shared state must remain protected with platform mutex primitives or atomic counters.
4. **No External Runtime Dependencies**: The core executable links only against standard OS system libraries (X11, pthread, dl, m on Linux; user32, gdi32, shell32, ole32, comdlg32, psapi on Windows).
5. **Documentation**: Maintain clean, direct prose without marketing buzzwords or em dashes.

## Submitting Pull Requests

1. Fork the repository and create your feature branch: `git checkout -b feature/my-enhancement`.
2. Make your changes with focused, descriptive commits.
3. Verify that `cargo test` and `make verify` pass with zero failures.
4. Open a pull request against `main` using the provided pull request template.
