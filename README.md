# DiskSonar

[![CI](https://github.com/spydersyrup/disksonar/actions/workflows/ci.yml/badge.svg)](https://github.com/spydersyrup/disksonar/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/spydersyrup/disksonar)](https://github.com/spydersyrup/disksonar/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

DiskSonar is a native disk storage analyzer for Linux and Windows written in C11. It builds a flat in-memory tree of every file and folder, then presents storage distribution across nine interactive visualizations, a 3-pass duplicate finder, an application leftover cleaner, a live system monitor, and folder snapshots.

Items are never deleted directly. Every item is staged in the Cleanup Queue first. Committing the queue moves items to the system Trash (FreeDesktop.org Trash on Linux, Recycle Bin on Windows) after confirmation, and updates ancestor sizes in place without rescanning.

## Features

- **Memory efficiency**: Nodes are stored in a contiguous arena layout (48 bytes per node with compact folder index tables and a string pool). A million files take approximately 60 MB of memory.
- **Multi-threaded scanner**: Spawns worker threads matching active CPU cores from a shared directory stack, using `fstatat` with `AT_SYMLINK_NOFOLLOW` on Linux and `FindFirstFileExA` on Windows.
- **Nine visualizations**:
  1. **Treemap**: Squarified layout showing proportional space usage.
  2. **Folders**: Card grid with file category distributions and top child previews.
  3. **Sunburst**: Concentric radial rings with drill-down and drill-up navigation.
  4. **Flame**: Horizontal hierarchical icicle bars.
  5. **Bubbles**: Spiral circle packing.
  6. **Mind Map**: Radial tree layout with Bezier connection curves.
  7. **Top Sizes**: Ranked list of largest files with relative size bars.
  8. **Age Map**: Files grouped into six age ranges from recent to multi-year archives.
  9. **List**: Sortable table by file size, space on disk, item count, or modification date.
- **Grouping modes**: Group views by folder structure, file category (video, audio, image, documents, developer, archives, apps, system, other), or last modified age.
- **Duplicate finder**: Three-pass filter checking exact file size, 16 KB head and tail SHA-256 prehash, and full streaming SHA-256 hash.
- **Application leftovers**: Locates installed apps and residual configuration or cache directories in user home folders.
- **Drive auto-discovery**: Detects local physical volumes, mount points, and used capacity with quick-scan cards on the idle start screen.
- **System monitor and snapshots**: Live process and resource monitor, plus JSON storage snapshots to track disk changes over time.

## Keyboard Shortcuts

| Shortcut | Action |
| --- | --- |
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

## Building from Source

### Prerequisites

- **Linux**: `gcc`, `make`, `libx11-dev`
- **Windows (cross-compile from Linux)**: `x86_64-w64-mingw32-gcc`
- **Optional**: Rust toolchain (stable) for Rust wrapper build

### Build Commands

```bash
# Build native Linux binary (dist/disksonar)
make linux

# Build Windows executable (dist/DiskSonar.exe)
make windows

# Build both platforms
make all

# Run self-test verification suite
make verify

# Create release tarballs with checksums
make package
```

## Running

### Graphical Interface

```bash
# Launch GUI
./dist/disksonar

# Launch with pre-selected path
DISKSONAR_SCAN=/home ./dist/disksonar

# Launch directly into a specific view
DISKSONAR_MODE=Treemap ./dist/disksonar

# Exclude specific folders, extensions, or patterns
./dist/disksonar --exclude "node_modules,target,*.tmp,.cache"
```

### Headless CLI Reports and Exports

DiskSonar can run in headless mode without a display server to print summaries or produce file exports:

```bash
# Terminal summary with category breakdown
./dist/disksonar /home --summary

# Ranked listing of top largest items (default: 15)
./dist/disksonar /home --top 25

# Export full scan tree to JSON
./dist/disksonar /home --json storage-report.json

# Export flat item listing to CSV
./dist/disksonar /home --csv storage-report.csv

# Generate self-contained interactive HTML storage report
./dist/disksonar /home --html storage-report.html

# Combine exclusion patterns with reports
./dist/disksonar /home --exclude "target,node_modules" --summary --html report.html
```

HTML reports are self-contained with offline search and table sorting. File paths and names are contextual entity-escaped to protect against XSS, and CSV exports prefix formula indicator characters (`=`, `+`, `-`, `@`) to guard against spreadsheet formula injection.

## Exclusion Rules and Ignore Files

DiskSonar supports ignoring files and directories during scanning:

- **`.disksonarignore` files**: Place a `.disksonarignore` (or `.disklensignore` / `.diskdudeignore`) file in any directory to exclude patterns relative to that folder. Blank lines and lines starting with `#` are ignored. Append `/` to match directories specifically (e.g. `build/`).
- **Command-line flag**: Pass `--exclude <pattern>` (or `-e`) to exclude comma-separated glob patterns or names (e.g. `--exclude "node_modules,target,*.log"`).
- **Environment variable**: Set `DISKSONAR_EXCLUDE="pattern1,pattern2"` to apply persistent ignore rules.
- **Kernel filesystem guards**: Linux pseudo-filesystems such as `/proc`, `/sys`, `/dev`, and `/run` are automatically skipped to avoid kernel deadlocks.
