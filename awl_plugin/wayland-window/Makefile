CC := gcc
LD := gcc
WLCLIENT_C := wlr-layer-shell-unstable-v1-protocol.c xdg-shell-protocol.c
WLCLIENT_H := wlr-layer-shell-unstable-v1-protocol.h xdg-shell-protocol.h
CFLAGS += -I/usr/include/pixman-1 -Wall -Wextra -pedantic
LDFLAGS += $(shell pkg-config --libs wayland-client wlroots wayland-cursor pixman-1)

.PHONY: all clean

all: minimal

SRC := $(WLCLIENT_C) minimal.c
OBJ := $(patsubst %.c,%.c.o,$(SRC))

-include Makefile.inc

WAYLAND_SCANNER   = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/wlr-layer-shell-unstable-v1.xml $@

minimal: $(OBJ)
	$(LD) $^ -o $@ $(LDFLAGS)

%.c.o: %.c %.h
	$(CC) -c $< -o $@ $(CFLAGS)

%.c.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	-@rm -rf minimal $(WLCLIENT_C) $(WLCLIENT_H) $(OBJ)
