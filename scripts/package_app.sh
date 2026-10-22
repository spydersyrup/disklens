#!/usr/bin/env bash
set -euo pipefail

source Packaging/AppConfig.env
./scripts/build_app.sh all

LINUX_STAGE="dist/${APP_NAME}-${VERSION}-linux-x86_64"
WIN_STAGE="dist/${APP_NAME}-${VERSION}-windows-x86_64"

rm -rf "${LINUX_STAGE}" "${WIN_STAGE}"
mkdir -p "${LINUX_STAGE}/bin" "${LINUX_STAGE}/share/applications" "${LINUX_STAGE}/share/icons/hicolor/512x512/apps"

cp -f "dist/${APP_EXEC}" "${LINUX_STAGE}/bin/${APP_EXEC}"
cp -f "dist/${APP_EXEC}.desktop" "${LINUX_STAGE}/share/applications/${APP_EXEC}.desktop"
cp -f "Packaging/Icon/disksonar.png" "${LINUX_STAGE}/share/icons/hicolor/512x512/apps/${APP_EXEC}.png"
cp -f "README.md" "${LINUX_STAGE}/README.md"

tar -czf "dist/${APP_NAME}-${VERSION}-linux-x86_64.tar.gz" -C dist "${APP_NAME}-${VERSION}-linux-x86_64"

if [ -f "dist/${APP_WIN_EXEC}" ]; then
    mkdir -p "${WIN_STAGE}"
    cp -f "dist/${APP_WIN_EXEC}" "${WIN_STAGE}/${APP_WIN_EXEC}"
    cp -f "Packaging/Icon/disksonar.png" "${WIN_STAGE}/${APP_EXEC}.png"
    if [ -f "Packaging/Windows/disksonar.ico" ]; then
        cp -f "Packaging/Windows/disksonar.ico" "${WIN_STAGE}/disksonar.ico"
    fi
    if [ -f "Packaging/Windows/preinstall_info.txt" ]; then
        cp -f "Packaging/Windows/preinstall_info.txt" "${WIN_STAGE}/preinstall_info.txt"
    fi
    cp -f "README.md" "${WIN_STAGE}/README.md"
    tar -czf "dist/${APP_NAME}-${VERSION}-windows-x86_64.tar.gz" -C dist "${APP_NAME}-${VERSION}-windows-x86_64"
fi

if command -v dpkg-deb >/dev/null 2>&1 && [ -f "dist/${APP_EXEC}" ]; then
    DEB_STAGE="dist/deb_stage"
    rm -rf "${DEB_STAGE}"
    mkdir -p "${DEB_STAGE}/DEBIAN"
    mkdir -p "${DEB_STAGE}/usr/bin"
    mkdir -p "${DEB_STAGE}/usr/share/applications"
    mkdir -p "${DEB_STAGE}/usr/share/icons/hicolor/512x512/apps"
    mkdir -p "${DEB_STAGE}/usr/share/doc/${APP_EXEC}"

    cp -f "dist/${APP_EXEC}" "${DEB_STAGE}/usr/bin/${APP_EXEC}"
    cp -f "dist/${APP_EXEC}.desktop" "${DEB_STAGE}/usr/share/applications/${APP_EXEC}.desktop"
    cp -f "Packaging/Icon/disksonar.png" "${DEB_STAGE}/usr/share/icons/hicolor/512x512/apps/${APP_EXEC}.png"
    cp -f "README.md" "${DEB_STAGE}/usr/share/doc/${APP_EXEC}/README.md"
    cp -f "LICENSE" "${DEB_STAGE}/usr/share/doc/${APP_EXEC}/copyright"

    cat <<EOF > "${DEB_STAGE}/DEBIAN/control"
Package: ${APP_EXEC}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: amd64
Maintainer: spydersyrup <spydersyrup@gmail.com>
Description: Native disk storage analyzer with multi-view visualizations
 DiskSonar is a fast disk storage visualizer and cleanup tool built in
 Rust and C. It features parallel directory scanning, duplicate detection,
 and interactive treemap, flame, sunburst, and age map views.
Homepage: https://github.com/spydersyrup/disksonar
EOF

    dpkg-deb --build --root-owner-group "${DEB_STAGE}" "dist/${APP_EXEC}_${VERSION}_amd64.deb"
    rm -rf "${DEB_STAGE}"
fi

(
    cd dist
    files=( *.tar.gz )
    for f in *.deb *setup*.exe; do
        if [ -f "$f" ]; then
            files+=( "$f" )
        fi
    done
    sha256sum "${files[@]}"
) > dist/SHA256SUMS.txt
echo "Packaged archives and SHA-256 checksums:"
cat dist/SHA256SUMS.txt
