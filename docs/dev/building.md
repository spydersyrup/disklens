# Building and Testing

DiskSonar is implemented in C11 and Rust (stable). It requires no external GUI frameworks or heavy runtime engines.

---

## Requirements

| Component | Linux | Windows (Cross-compile from Linux) |
| :--- | :--- | :--- |
| **C Compiler** | GCC 9+ or Clang 10+ (`-std=c11`) | `x86_64-w64-mingw32-gcc` |
| **Resource Compiler** | N/A | `x86_64-w64-mingw32-windres` (`binutils-mingw-w64-x86-64`) |
| **Rust Toolchain** | Stable (`cargo`, `rustc`) | Stable with `x86_64-pc-windows-gnu` target |
| **System Libraries** | `libx11-dev`, `make`, `libc` | `gdi32`, `user32`, `shell32`, `ole32`, `advapi32` |
| **Packaging Tools** | `dpkg-deb`, `tar`, `sha256sum` | Inno Setup 6 (`iscc` or Docker `amake/innosetup`) |

### Installing Dependencies

#### Ubuntu / Debian / Kali
```bash
sudo apt-get update
sudo apt-get install -y gcc make libx11-dev gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64
```

#### Fedora / RHEL
```bash
sudo dnf install -y gcc make libX11-devel mingw64-gcc mingw64-binutils
```

#### Arch Linux
```bash
sudo pacman -S gcc make libx11 mingw-w64-gcc
```

---

## Build Targets

All primary build tasks are coordinated via the top-level `Makefile`:

```bash
# Build native Linux binary (dist/disksonar)
make linux

# Build native Windows 64-bit executable (dist/DiskSonar.exe)
make windows

# Build both Linux and Windows binaries
make all

# Build native Debian package (dist/disksonar_<version>_amd64.deb)
make deb

# Build Windows setup installer via Inno Setup
make installer

# Run automated self-test verification suite
make verify

# Create release tarballs, deb packages, and SHA-256 checksums
make package

# Clean build artifacts
make clean
```

---

## Self-Test Suite & Command Flags

DiskSonar contains a built-in automated test suite that validates every view, layout breakpoint, export format, and security sanitizer without requiring user interaction.

```bash
./dist/disksonar --verify .
```

### Command Line Flags

| Flag | Description |
| :--- | :--- |
| `--verify <dir>` | Runs the 38-check automated self-test suite and reports PASS/FAIL per component. |
| `--summary <dir>` | Runs in headless terminal mode and outputs category and size breakdowns. |
| `--top <count> <dir>` | Lists the top N largest files and folders in the target directory (default: 15). |
| `--json <out_file>` | Crawls the directory and exports the full tree to structured JSON format. |
| `--csv <out_file>` | Crawls the directory and exports all items to formula-injection safe CSV. |
| `--html <out_file>` | Generates a standalone, interactive HTML storage report with embedded styling. |
| `--exclude <pattern>` | Excludes matching file or directory glob patterns (or comma-separated lists). |
| `--help`, `-h` | Displays command line usage and parameter documentation. |
| `--version`, `-v` | Prints the version string and target architecture. |
