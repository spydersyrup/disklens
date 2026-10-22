# DiskSonar Architecture

DiskSonar is a high-performance native disk storage analyzer and cleanup utility built in C11 with a Rust host bridge. It maps directory trees, computes aggregated usage, detects duplicate files, and provides nine real-time visualizations and headless terminal reports.

There are zero external runtime GUI dependencies. The application renders directly into an in-memory 32-bit ARGB software canvas and blits directly to the native display server (X11 on Linux via XImage/MIT-SHM, and Win32 GDI on Windows via Device Context DIBs).

---

## 1. System Overview

The application is structured into modular subsystems:

```
+-----------------------------------------------------------+
|                   Rust Host / CLI Bridge                  |
|          (src/main.rs, src/models.rs, src/utils.rs)        |
+-----------------------------------------------------------+
                              |
+-----------------------------------------------------------+
|                 Application Controller (C11)              |
|          (Sources/DiskSonar/DiskSonarApp.c)               |
+-----------------------------------------------------------+
      |                   |                      |
      v                   v                      v
+------------+    +---------------+    +--------------------+
|  Scanner   |    | Visualization |    | Duplicate & Safety |
|   Engine   |    |    Canvas     |    |   Cleanup Queue    |
| (Scanning) |    | (Views/Theme) |    | (Utils/Models)     |
+------------+    +---------------+    +--------------------+
```

- **Rust Host (`src/main.rs`, `src/utils.rs`)**: Owns bounded SHA-256 hashing, human-readable formatters, unit tests, and the main entrypoint delegating to `dd_native_app_main`.
- **App Controller (`DiskSonarApp.c`)**: Manages UI state, view modes, active tab navigation, search queries, event routing, and verification self-checks.
- **Scanner Engine (`DiskScanner.c`)**: Multi-threaded parallel filesystem crawler.
- **Canvas & Views (`Canvas.c`, `Views.c`, `Theme.c`)**: Vector and bitmap software renderer for all 9 visualization modes.
- **Cleanup & Trash (`DiskScanner.c`, `Utils.c`)**: Staged queue management and OS Trash integration.

---

## 2. In-Memory Storage & Flat Arena Layout

To analyze millions of files within minimal memory, DiskSonar avoids pointer-heavy tree node hierarchies. Instead, all entries are stored in contiguous arena buffers:

- **Node Arena (`DDFileNode`)**: Fixed 48-byte struct containing file metadata (logical size, size on disk, timestamps, category, flags, parent index, first child index, and sibling index).
- **String Pool**: File and folder names are interned into a sequential character buffer. A million nodes occupy approximately 60 MB of RAM.
- **Folder Metadata**: Folder index tables store pre-aggregated type distributions across 9 categories (Video, Audio, Image, Document, Developer, Archive, Apps, System, Other). When files are marked for cleanup, ancestor sizes update in place without re-traversing the filesystem.

---

## 3. Parallel Work-Stealing Scanner

Filesystem crawling scales across all available CPU cores:

- **Worker Threads**: The scanner creates worker threads matching active CPU cores.
- **Work-Stealing Queue**: A lock-free or mutex-protected directory stack distributes directories among idle threads.
- **Fast System Calls**: Uses `fstatat` with `AT_SYMLINK_NOFOLLOW` on Linux to avoid symlink loops, and `FindFirstFileExA` / `FindNextFileA` with `FindExInfoBasic` on Windows.
- **Kernel Guard Rails**: Virtual kernel filesystems (`/proc`, `/sys`, `/dev`, `/run`) are automatically skipped on Linux to prevent kernel deadlocks.
- **Ignore Rules**: Honors `.disksonarignore`, `.disklensignore`, and `.diskdudeignore` files at each folder level, as well as command-line `--exclude` glob patterns.

---

## 4. Visualizations & Software Canvas

All rendering occurs in an in-memory 32-bit ARGB canvas (`DDCanvas`) before being presented to the display server:

| Visualization | Layout Algorithm |
| :--- | :--- |
| **Treemap** | Squarified treemap algorithm optimizing aspect ratios close to 1:1. |
| **Folders** | Card grid displaying category breakdowns and top item previews. |
| **Sunburst** | Concentric radial rings partitioned by angular proportions. |
| **Flame** | Hierarchical horizontal icicle bars showing deep nesting levels. |
| **Bubbles** | Spiral circle packing with collision resolution. |
| **Mind Map** | Radial hierarchical tree with cubic Bezier connection lines. |
| **Top Sizes** | Ranked listing of the largest storage consumers. |
| **Age Map** | Categorization into six age ranges (recent days to multi-year archives). |
| **List** | Sortable detailed table by size, disk usage, item count, or date. |

---

## 5. Duplicate Detection Pipeline

The duplicate engine uses a three-pass progressive filter to minimize disk I/O:

1. **Pass 1 (Size Grouping)**: Groups files by exact logical byte size. Only sizes with two or more files proceed.
2. **Pass 2 (Head/Tail Pre-hash)**: Reads and hashes the first 16 KB and last 16 KB of each candidate with SHA-256. Files with differing endpoints are pruned immediately without reading intermediate data.
3. **Pass 3 (Streaming SHA-256)**: Files with identical endpoints undergo full bounded streaming SHA-256 hashing to confirm 100% duplicate identity.

---

## 6. Safe Cleanup & Trash Integration

DiskSonar enforces a strict zero-accidental-deletion policy:

- **No Immediate Unlink**: Clicking an item never deletes it immediately. Items must be added to the inspectable **Cleanup Queue**.
- **Interactive Review**: The user reviews the staged queue count, paths, and total reclaimable bytes.
- **System Trash / Recycle Bin**: When confirmed, files are moved to system Trash:
  - **Linux**: FreeDesktop.org Trash specification (`$XDG_DATA_HOME/Trash/files` and `info`).
  - **Windows**: `SHFileOperation` / `IFileOperation` with `FO_DELETE` and `FOF_ALLOWUNDO`.
- **Live Tree Adjustments**: Reclaimed byte sizes propagate upward to parent directories immediately without rescanning the disk.

---

## 7. Export Security & Sanitization

All exported reports include built-in security protections against injection attacks:

- **HTML Reports**: Paths, file names, and category names undergo contextual HTML entity escaping (`<`, `>`, `&`, `"`, `'`) to prevent cross-site scripting (XSS) when viewing reports in a browser.
- **CSV Spreadsheets**: Cells starting with formula trigger characters (`=`, `+`, `-`, `@`, `\t`, `\r`) are automatically prepended with a single quote (`'`) to guard against spreadsheet formula injection (CSV injection).
- **JSON Trees**: All strings are JSON-escaped according to RFC 8259 specifications.
