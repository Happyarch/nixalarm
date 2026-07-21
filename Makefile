BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
DESTDIR ?=
PREFIX ?= /usr/local

.PHONY: all configure build install clean distclean appimage deb rpm arch help

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

install: build
	DESTDIR=$(DESTDIR) $(CMAKE) --install $(BUILD_DIR)

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR) build-appimage build-deb build-rpm dist AppDir

appimage:
	packaging/appimage/build-appimage.sh

deb:
	packaging/deb/build-deb.sh

rpm:
	packaging/rpm/build-rpm.sh

arch:
	cd packaging/arch && makepkg -f

help:
	@echo "Targets:"
	@echo "  make             Build nixalarm"
	@echo "  make install     Install with DESTDIR= and/or PREFIX= overrides"
	@echo "  make appimage    Build dist/nixalarm-x86_64.AppImage"
	@echo "  make deb         Build a Debian package with CPack"
	@echo "  make rpm         Build a Fedora/RPM package with CPack"
	@echo "  make arch        Build an Arch package with makepkg"
