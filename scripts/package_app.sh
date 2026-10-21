#!/usr/bin/env bash
set -euo pipefail

source Packaging/AppConfig.env
./scripts/build_app.sh

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
    cp -f "README.md" "${WIN_STAGE}/README.md"
    tar -czf "dist/${APP_NAME}-${VERSION}-windows-x86_64.tar.gz" -C dist "${APP_NAME}-${VERSION}-windows-x86_64"
fi

sha256sum dist/*.tar.gz > dist/SHA256SUMS.txt
echo "Packaged archives and SHA-256 checksums:"
cat dist/SHA256SUMS.txt
