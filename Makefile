# SC LAN Patch - build with MinGW-w64 (i686, the game is 32 bit)
CC      ?= i686-w64-mingw32-gcc
CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-format-truncation -std=gnu99 \
           -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -fno-ident
LDFLAGS := -shared -static -static-libgcc -s -Wl,--enable-stdcall-fixup -Wl,--kill-at
LIBS    := -lws2_32 -liphlpapi -lole32 -luuid

SRC     := src/proxy.c src/hooks.c src/discovery.c src/net.c src/config.c
OUT     := build

all: $(OUT)/dplayx.dll $(OUT)/dptest.exe

$(OUT)/dplayx.dll: $(SRC) src/sclan.h src/dplayx.def | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SRC) src/dplayx.def $(LDFLAGS) $(LIBS)

# DirectPlay test client/host that uses the API exactly like the game does
$(OUT)/dptest.exe: tools/dptest.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $< -static -static-libgcc -s -lole32 -luuid -ldplayx

$(OUT):
	mkdir -p $@

dist: all
	rm -rf $(OUT)/dist $(OUT)/SC-LANPatch.zip && mkdir -p $(OUT)/dist
	cp $(OUT)/dplayx.dll dist/sclanpatch.ini dist/install.bat dist/uninstall.bat dist/firewall.bat $(OUT)/dist/
	cp README.md $(OUT)/dist/LIESMICH.md
	# cmd.exe needs CRLF line endings (goto/labels break with LF)
	sed -i 's/\r*$$/\r/' $(OUT)/dist/*.bat $(OUT)/dist/*.ini
	cd $(OUT)/dist && zip -q -r ../SC-LANPatch.zip .

clean:
	rm -rf $(OUT)

.PHONY: all dist clean
