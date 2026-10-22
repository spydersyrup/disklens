<p align="center">
  <img src="Packaging/Icon/disksonar.png" width="112" height="112" alt="DiskSonar icon" />
</p>

<h1 align="center">DiskSonar</h1>

<p align="center">
  <strong>A fast, native disk storage analyzer for your computer: map every byte, find duplicates, and clean up drive space.</strong>
</p>

<p align="center">
  Repository: <a href="https://github.com/spydersyrup/disksonar">github.com/spydersyrup/disksonar</a> · Releases: <a href="https://github.com/spydersyrup/disksonar/releases">Releases</a> · Documentation: <a href="docs/README.md">Developer docs</a>
</p>

<p align="center">
  <a href="https://github.com/spydersyrup/disksonar/releases/latest"><img src="https://img.shields.io/github/v/release/spydersyrup/disksonar?style=flat-square&color=2A2A2A&labelColor=2A2A2A&label=&logo=git&logoColor=E55B3C" alt="Latest release" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-2A2A2A?style=flat-square&logo=opensourceinitiative&logoColor=white" alt="MIT License" /></a>
  <img src="https://img.shields.io/badge/Windows_%C2%B7_Linux-2A2A2A?style=flat-square" alt="Windows and Linux" />
  <a href="https://www.rust-lang.org"><img src="https://img.shields.io/badge/Rust-2A2A2A?style=flat-square&logo=rust&logoColor=E55B3C" alt="Rust" /></a>
  <a href="https://en.wikipedia.org/wiki/C11_(C_standard_revision)"><img src="https://img.shields.io/badge/C11-2A2A2A?style=flat-square&logo=c&logoColor=659AD2" alt="C11" /></a>
  <a href="https://github.com/spydersyrup/disksonar/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/spydersyrup/disksonar/ci.yml?branch=main&style=flat-square&color=2A2A2A&labelColor=2A2A2A&label=CI&logo=githubactions&logoColor=white" alt="CI status" /></a>
  <a href="https://github.com/spydersyrup/disksonar/releases"><img src="https://img.shields.io/github/downloads/spydersyrup/disksonar/total?style=flat-square&color=2A2A2A&labelColor=2A2A2A&label=downloads&logo=github&logoColor=white" alt="GitHub downloads" /></a>
</p>

---

## Install

Download packages from the [latest release](https://github.com/spydersyrup/disksonar/releases/latest) page:

| System | Download | Notes |
| :--- | :--- | :--- |
| Windows | `setup.exe` | Inno Setup installer, with Explorer "Scan with DiskSonar" context menu |
| Windows | `.tar.gz` | Portable archive. Run `dist/DiskSonar.exe` |
| Linux | `.deb` | Debian, Ubuntu, Linux Mint, Pop!_OS, Kali (`sudo dpkg -i <file>`) |
| Linux | `.tar.gz` | Portable archive. Includes `.desktop` entry and `512x512` icon |

Every release includes `SHA256SUMS.txt` for verifying package integrity.

---

## Features

### Exploration & Visualization

- **Nine interactive views**: Treemap (squarified), Folders card grid, Sunburst rings, Flame icicle bars, Bubbles circle packing, Mind Map, Top Sizes ranked bars, Age Map, and Details List.
- **Dynamic grouping modes**: Regroup the visual tree by file category (Video, Audio, Image, Document, Developer, Archive, Apps, System, Other), directory hierarchy, or last-modified age without mutating the underlying filesystem tree.
- **Search & drill-down**: Filter files and directories in real time with Ctrl+K, adjust depth limits, and navigate into subtrees with instant back-stack navigation.
- **Exclusion rules**: Honor `.disksonarignore` rules at any directory level or pass `--exclude` glob patterns on the command line.

### Cleanup & Duplicates

- **Three-pass duplicate finder**: High-speed duplicate identification using exact byte-size grouping, 16 KB head/tail SHA-256 pre-hashing, and full bounded streaming verification.
- **Staging Area for Safe Cleanup**: Items are never deleted immediately. Files are staged into an inspectable review area showing item counts and total reclaimable capacity.
- **Safe Trash integration**: Moves files directly to system Trash (FreeDesktop.org Trash on Linux, Recycle Bin on Windows) with confirmation dialogs. Reclaimed space updates parent folder sizes in place.
- **Application leftovers**: Detects unused caches, logs, and residual configuration directories in user profile locations.

### Terminal & Reports

- **Headless CLI reports**: Run `--summary` and `--top 15` directly in terminal sessions or SSH connections without launching a window.
- **Portable exports**: Generate static standalone HTML reports with offline search, structured JSON trees, and formula-safe CSV spreadsheets.
- **Self-test suite**: Built-in 38-check automated verification suite (`--verify .`) validates rendering, responsive breakpoints, sanitizers, and export integrity.

### Private by default

- **100% offline**: Zero network calls, zero telemetry, no analytics, and no accounts. All directory scanning, hashing, and analysis happen strictly locally on your computer.
- **Read-only scanning**: Filesystem crawling only reads file metadata and directory entries. Files are never altered or moved without your explicit action.

---

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl+F` or `Ctrl+K` | Focus search filter |
| `Escape` | Dismiss modal dialog, context menu, or clear search |
| `Ctrl+1` .. `Ctrl+5` | Switch active tab (Explore, Duplicates, Apps, Monitor, Snapshots) |
| `1` .. `9` | Switch visualization mode in Explore tab |
| `+` / `-` | Increase or decrease folder depth limit |
| `A` | Toggle compact folder abbreviations |
| `Space` | Open Quick Look preview for selected file |
| `Enter` or `Right Arrow` | Drill into selected directory |
| `Backspace` or `Left Arrow` | Navigate to parent directory |
| `Up Arrow` / `Down Arrow` | Move selection to previous or next sibling |
| `Ctrl+R` | Rescan current root folder |
| `Ctrl+O` | Open folder selection dialog |
| `Ctrl+D` | Toggle dark theme |
| `Ctrl+I` | Toggle right-hand metadata inspector |
| `?` | Toggle shortcuts help dialog |

---

## Building from source

DiskSonar is written in C11 with a Rust bridge. You need a C11 compiler (`gcc` or `clang`), `make`, and a stable Rust toolchain. On Linux, install `libx11-dev`; on Windows, install MinGW-w64.

```bash
# Build native Linux binary (dist/disksonar)
make linux

# Build native Windows executable (dist/DiskSonar.exe)
make windows

# Run built-in self-test verification suite
make verify
```

See [Building and testing](docs/dev/building.md) for full system requirements and packaging targets (`make deb`, `make installer`, `make package`).

---

## Documentation

- [Developer guide](docs/README.md): requirements, building, and packaging workflows
- [Architecture](ARCHITECTURE.md): flat arena memory tree, parallel scanner, layout algorithms, and Trash safety
- [Building and testing](docs/dev/building.md): toolchain configurations and CLI flag reference
- [Design system](docs/DESIGN.md): tactile utility theme tokens and WCAG AA contrast specifications
- [Contributing](CONTRIBUTING.md): coding guidelines, code hygiene, and pull request guide
- [Security](SECURITY.md): vulnerability reporting and injection defense details
- [Changelog](CHANGELOG.md): release history and patch notes

---

## License

DiskSonar is licensed under the [MIT License](LICENSE).
