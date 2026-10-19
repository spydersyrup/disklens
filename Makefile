CC ?= gcc
WINCC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation

SRCS = \
	Sources/DiskDude/DiskDudeApp.c \
	Sources/DiskDude/Theme/Theme.c \
	Sources/DiskDude/Utils/Utils.c \
	Sources/DiskDude/Utils/Canvas.c \
	Sources/DiskDude/Models/FileNode.c \
	Sources/DiskDude/Scanning/DiskScanner.c \
	Sources/DiskDude/Views/Views.c

.PHONY: all linux windows run app package icon verify clean

all: linux windows

linux:
	@mkdir -p dist
	./scripts/build_app.sh

windows:
	@mkdir -p dist
	./scripts/build_app.sh

icon:
	./scripts/make_icon.sh

app:
	./scripts/build_app.sh

package:
	./scripts/package_app.sh

run: linux
	./dist/disklens

verify: linux
	./dist/disklens --verify .

clean:
	rm -rf dist Packaging/Icon/disklens.png Packaging/Icon/make_icon_bin
