# SC LAN Patch
#
# The game is a 32 bit Windows program, so the patch is always a 32 bit
# Windows DLL - on Linux it is loaded by Proton/Wine. Build with MinGW-w64:
#   Linux (cross):          make
#   Windows (MSYS2 MINGW32): make CROSS=
#
#   make             build/dplayx.dll + build/dptest.exe
#   make dist        build/SC-LANPatch-<ver>-Windows.zip + build/SC-LANPatch-<ver>-Linux.tar.gz

VERSION := $(shell sed -n 's/^\#define SCLAN_VERSION *"\(.*\)"/\1/p' src/sclan.h)
CROSS   ?= i686-w64-mingw32-
CC      := $(CROSS)gcc
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-format-truncation \
           -std=gnu99 -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -fno-ident
LDFLAGS := -shared -static -static-libgcc -s -Wl,--enable-stdcall-fixup -Wl,--kill-at
LIBS    := -lws2_32 -liphlpapi -lole32 -luuid -lgdi32

SRC     := src/proxy.c src/hooks.c src/discovery.c src/net.c src/config.c src/uiscale.c
OUT     := build
WINPKG  := SC-LANPatch-$(VERSION)-Windows
LINPKG  := SC-LANPatch-$(VERSION)-Linux

all: $(OUT)/dplayx.dll $(OUT)/dptest.exe

$(OUT)/dplayx.dll: $(SRC) src/sclan.h src/dplayx.def | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRC) src/dplayx.def $(LDFLAGS) $(LIBS)

# DirectPlay test client/host that uses the API exactly like the game does
$(OUT)/dptest.exe: tools/dptest.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $< -static -static-libgcc -s -lole32 -luuid -ldplayx

$(OUT):
	mkdir -p $@

dist: dist-windows dist-linux

dist-windows: $(OUT)/dplayx.dll
	rm -rf $(OUT)/$(WINPKG) $(OUT)/$(WINPKG).zip && mkdir -p $(OUT)/$(WINPKG)
	cp $(OUT)/dplayx.dll packaging/common/sclanpatch.ini packaging/windows/*.bat $(OUT)/$(WINPKG)/
	cp docs/ANLEITUNG-Windows.md $(OUT)/$(WINPKG)/ANLEITUNG.md
	cp README.md $(OUT)/$(WINPKG)/LIESMICH.md
	cp LICENSE $(OUT)/$(WINPKG)/LICENSE.txt
	# cmd.exe needs CRLF line endings (goto/labels break with LF)
	sed -i 's/\r*$$/\r/' $(OUT)/$(WINPKG)/*.bat $(OUT)/$(WINPKG)/*.ini $(OUT)/$(WINPKG)/*.md $(OUT)/$(WINPKG)/LICENSE.txt
	cd $(OUT) && zip -q -r $(WINPKG).zip $(WINPKG)

dist-linux: $(OUT)/dplayx.dll
	rm -rf $(OUT)/$(LINPKG) $(OUT)/$(LINPKG).tar.gz && mkdir -p $(OUT)/$(LINPKG)
	cp $(OUT)/dplayx.dll packaging/common/sclanpatch.ini packaging/linux/sclan-linux.sh $(OUT)/$(LINPKG)/
	cp docs/ANLEITUNG-Linux.md $(OUT)/$(LINPKG)/ANLEITUNG.md
	cp README.md $(OUT)/$(LINPKG)/LIESMICH.md
	cp LICENSE $(OUT)/$(LINPKG)/LICENSE
	chmod +x $(OUT)/$(LINPKG)/sclan-linux.sh
	cd $(OUT) && tar -czf $(LINPKG).tar.gz $(LINPKG)

clean:
	rm -rf $(OUT)

.PHONY: all dist dist-windows dist-linux clean
