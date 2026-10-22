# Changelog

All notable changes to DiskSonar will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.1] - 2026-10-23

### Added
- **Debian / Ubuntu Packaging**: Automated generation of `disksonar_0.1.1_amd64.deb` with system desktop entry, hicolor `512x512` icon, copyright notice, and documentation.
- **Windows Icon & PE Version Resources**: High-resolution 6-layer icon (`disksonar.ico`) and PE version table embedded directly into `dist/DiskSonar.exe` via `windres`.
- **Inno Setup Installer (`.iss`)**: Modern 64-bit Windows setup wizard supporting dual installation modes (all users vs. current user), pre-install permission advisories, Windows Explorer context menu integration ("Scan with DiskSonar"), and system PATH registration.
- **CI Artifact Uploads**: Automated GitHub Actions artifact uploads for `disksonar-linux-x86_64` and `DiskSonar-windows-x86_64` binaries on every push and pull request.

### Changed
- Deduplicated SHA-256 checksum generation in release packaging script.
- Added seccomp unconfined option and fallback handling for Docker-based Inno Setup compilation.

---

## [0.1.0] - 2026-10-22

### Added
- **Nine Visualization Modes**: Treemap (squarified), Folders card grid, Sunburst rings, Flame icicle graph, Bubbles circle packing, Mind Map, Top Sizes rank list, Age Map, and List table.
- **Parallel Filesystem Scanner**: Multi-threaded work-stealing directory crawler with `.disksonarignore` rules and kernel pseudo-filesystem guards.
- **Duplicate Detection**: Three-pass progressive duplicate detection pipeline with exact size filtering and bounded streaming SHA-256 hashing.
- **Safe Cleanup Queue**: Staged item inspection with confirmation safeguards and native system Trash integration (FreeDesktop.org Trash on Linux, Recycle Bin on Windows).
- **Headless CLI Reports**: Terminal space summaries (`--summary`), top consumer rankings (`--top`), and standalone HTML, CSV, and JSON exports.
- **Drive Auto-Discovery**: Automatic physical storage volume detection and quick-scan cards on start screen.
- **System Monitor & Snapshots**: Live CPU, memory, and volume tracking, plus JSON storage snapshot comparisons.
- **Self-Test Verification**: Automated 38-check self-test suite (`--verify .`).
