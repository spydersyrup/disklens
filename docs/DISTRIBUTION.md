# Distribution Guide (Linux & Windows)

DiskLens builds self-contained native executables for both Linux (`x86_64-linux-gnu`) and Windows (`x86_64-w64-mingw32`) from a single C11 codebase.

## 1. Building Release Archives

Run the packaging script from the repository root:

```bash
./scripts/package_app.sh
```

This produces:
- `dist/DiskLens-1.0.0-linux-x86_64.tar.gz` containing `bin/disklens`, `share/applications/disklens.desktop`, and `share/icons/hicolor/512x512/apps/disklens.png`.
- `dist/DiskLens-1.0.0-windows-x86_64.tar.gz` containing `DiskLens.exe` and `disklens.png`.
- `dist/SHA256SUMS.txt` containing SHA-256 checksums for all archives.

## 2. Linux Installation & Permissions

To install locally on a FreeDesktop.org desktop environment:

```bash
tar -xzf dist/DiskLens-1.0.0-linux-x86_64.tar.gz
cp dist/DiskLens-1.0.0-linux-x86_64/bin/disklens ~/.local/bin/
cp dist/DiskLens-1.0.0-linux-x86_64/share/applications/disklens.desktop ~/.local/share/applications/
cp dist/DiskLens-1.0.0-linux-x86_64/share/icons/hicolor/512x512/apps/disklens.png ~/.local/share/icons/hicolor/512x512/apps/
```

DiskLens runs unprivileged by default so it only reads directories accessible to your user account and counts `EACCES`/`EPERM` directories in the unreadable-folders banner.

## 3. Windows Installation & Code Signing

`dist/DiskLens.exe` links exclusively against standard Win32 system DLLs (`user32.dll`, `gdi32.dll`, `shell32.dll`, `ole32.dll`, `comdlg32.dll`, `psapi.dll`) present on every Windows 10 and Windows 11 installation.

To sign the executable for SmartScreen reputation with `osslsigncode` or `signtool.exe`:

```bash
osslsigncode sign -certs cert.pem -key key.pem \
  -n "DiskLens" -i "https://github.com/spydersyrup/disklens" \
  -t "http://timestamp.digicert.com" \
  -in dist/DiskLens.exe -out dist/DiskLens-signed.exe
```
