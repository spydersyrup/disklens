CARGO ?= cargo
CARGOFLAGS ?= --release

.PHONY: all linux windows run app package icon verify clean

all: linux windows

linux:
	@mkdir -p dist
	$(CARGO) build $(CARGOFLAGS)
	@if [ -f target/release/diskdude ]; then \
		cp -f target/release/diskdude dist/diskdude; \
	elif [ -f target/release/diskdude.exe ]; then \
		cp -f target/release/diskdude.exe dist/diskdude.exe; \
	fi

windows:
	@mkdir -p dist
	$(CARGO) build $(CARGOFLAGS) --target x86_64-pc-windows-gnu
	@if [ -f target/x86_64-pc-windows-gnu/release/diskdude.exe ]; then \
		cp -f target/x86_64-pc-windows-gnu/release/diskdude.exe dist/DiskDude.exe; \
	elif [ -f target/release/diskdude.exe ]; then \
		cp -f target/release/diskdude.exe dist/DiskDude.exe; \
	fi

icon:
	./scripts/make_icon.sh

app:
	./scripts/build_app.sh

package:
	./scripts/package_app.sh

run: linux
	@if [ -f dist/diskdude ]; then ./dist/diskdude; else ./dist/diskdude.exe; fi

verify: linux
	@if [ -f dist/diskdude ]; then ./dist/diskdude --verify .; else ./dist/diskdude.exe --verify .; fi

clean:
	rm -rf dist target/release target/x86_64-pc-windows-gnu/release Packaging/Icon/diskdude.png Packaging/Icon/make_icon_bin
