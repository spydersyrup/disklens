# DiskDude

DiskDude is a native, ultra-lightweight disk-space analyzer for Linux and Windows written in C11 with zero external runtime library bloat. It builds a flat arena-allocated in-memory tree of every file and folder, then lets you explore storage usage across 9 visualizations, a 3-pass duplicate finder, an app uninstaller with leftover detection, a live system monitor, before/after snapshots, and a Quick Wins junk finder.

Core safety promise: **nothing is ever deleted directly.** Every item goes into the **Cleanup Queue** first. Committing the queue moves items to the system **Trash** (FreeDesktop.org Trash on Linux; Recycle Bin via `SHFileOperationA(FOF_ALLOWUNDO)` on Windows) after an explicit confirmation dialog, and updates all ancestor sizes in place without rescanning.

## Highlights

- **Ultra-low RAM footprint**: Nodes are stored in a flat contiguous `DDFileNode` arena (48 bytes per node + compact folder index tables and a shared string pool) so 1.2M files take ~65 MB of RAM instead of hundreds of megabytes of pointer-heavy heap allocations.
- **Multi-threaded scanner**: Spawns one worker thread per active CPU core pulling from a LIFO directory stack guarded by mutex + condition variable, using `fstatat(..., AT_SYMLINK_NOFOLLOW)` on Linux and `FindFirstFileExA` on Windows.
- **9 interactive visualizations**:
  1. **Treemap** (Bruls et al. squarified layout)
  2. **Folders** (290pt responsive grid cards with file-type bar and top-3 child preview)
  3. **Sunburst** (radial rings with center drill-up circle)
  4. **Flame** (horizontal icicle stack)
  5. **Bubbles** (golden-angle spiral circle packing)
  6. **Mind Map** (3-column radial tree with cubic Bezier links)
  7. **Top Sizes** (top 200 files ranked with relative bar and age badge)
  8. **Age Map** (6 age buckets from `< 1 month` to `3+ years`, plus stale folders)
  9. **List** (sortable table by Size, On Disk, Files, or Modified)
- **Non-mutating synthetic regrouping**: Switching Color Mode to **By type** or **By age** rebuilds a 2-level synthetic tree (Category -> Top 80 files + `N smaller files` remainder node) so Treemap, Sunburst, Flame, Bubbles, and Mind Map physically regroup by category or age without mutating the scanned tree.
- **3-pass Duplicate Finder**: Groups files >= 100 KB by exact byte size, then 16 KB head + 16 KB tail SHA-256 prehash, then full streaming SHA-256 with `POSIX_FADV_DONTNEED` cache hints and **Keep Newest** one-click staging.
- **Applications & Leftovers**: Discovers installed apps and user data/cache/config leftovers (`~/.config`, `~/.cache`, `~/.local/share`, `~/.var/app` on Linux; `%APPDATA%`, `%LOCALAPPDATA%` on Windows).
- **Live System Monitor & Snapshots**: 2-second live CPU, Memory, Disk, and Top 8 RSS process monitor, plus persistent before/after JSON snapshots up to depth 3 (`~/.local/share/diskdude/snapshots.json`).

## Build & Run

```bash
# Build both Linux (dist/diskdude) and Windows (dist/DiskDude.exe) binaries
make all

# Run on Linux
make run

# Package release archives with icon and SHA-256 checksums
make package

# Run automated verification suite
make verify
```

## Developer Environment Hooks

- `DISKDUDE_SCAN=/path/to/folder`: Automatically scan the specified directory on startup.
- `DISKDUDE_MODE=Treemap|Folders|Sunburst|Flame|Bubbles|MindMap|TopSizes|AgeMap|List|Duplicates|Applications|Monitor|Snapshots`: Open directly into the specified visualization or tab.
