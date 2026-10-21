CC ?= gcc
WINCC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation

SRCS = \
	Sources/DiskSonar/DiskSonarApp.c \
	Sources/DiskSonar/Theme/Theme.c \
	Sources/DiskSonar/Utils/Utils.c \
	Sources/DiskSonar/Utils/Canvas.c \
	Sources/DiskSonar/Models/FileNode.c \
	Sources/DiskSonar/Scanning/DiskScanner.c \
	Sources/DiskSonar/Views/Views.c

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
	./dist/disksonar

verify: linux
	./dist/disksonar --verify .

clean:
	rm -rf dist Packaging/Icon/disksonar.png Packaging/Icon/make_icon_bin
