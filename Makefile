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
	$(CC) $(CFLAGS) $(SRCS) -lX11 -lpthread -ldl -lm -o dist/diskdude

windows:
	@mkdir -p dist
	$(WINCC) $(CFLAGS) $(SRCS) -lgdi32 -luser32 -lshell32 -lole32 -luuid -lcomdlg32 -lpsapi -lm -mwindows -o dist/DiskDude.exe

icon:
	./scripts/make_icon.sh

app:
	./scripts/build_app.sh

package:
	./scripts/package_app.sh

run: linux
	./dist/diskdude

verify: linux
	./dist/diskdude --verify .

clean:
	rm -rf dist Packaging/Icon/diskdude.png Packaging/Icon/make_icon_bin
