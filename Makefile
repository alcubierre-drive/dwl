CC := gcc
LD := gcc

PKGS = wlroots wayland-server xkbcommon libinput xcb xcb-icccm fcft pixman-1 \
       wayland-client wayland-cursor libpulse

CFLAGS += $(shell pkg-config --cflags $(PKGS)) \
	-I. -DWLR_USE_UNSTABLE -DVERSION=\"0.1\" -D_POSIX_C_SOURCE=200809L \
	-fPIC -D_DEFAULT_SOURCE
LDFLAGS += $(shell pkg-config --libs $(PKGS)) $(LIBS)

AWL_SRC := $(shell find . -maxdepth 1 -type f -iname "*.c") \
	   awl-ipc-unstable-v2-protocol.c
PLUGIN_SRC := $(shell find awl_plugin/ -maxdepth 1 -type f -iname "*.c") \
	awl_plugin/xdg-shell-protocol.c \
	awl_plugin/xdg-output-unstable-v1-protocol.c \
	awl_plugin/wlr-layer-shell-unstable-v1-protocol.c \
	awl_plugin/awl-ipc-unstable-v2-protocol.c

AWL_OBJ := $(patsubst %.c,%.c.o,$(AWL_SRC))
PLUGIN_OBJ := $(patsubst %.c,%.c.o,$(PLUGIN_SRC))

DEPS := $(patsubst %.c,%.c.d,$(AWL_SRC) $(PLUGIN_SRC))

AWL_PROTOCOLS := xdg-shell-protocol.h \
		 wlr-layer-shell-unstable-v1-protocol.h \
		 awl-ipc-unstable-v2-protocol.h \
		 awl-ipc-unstable-v2-protocol.c
PLUGIN_PROTOCOLS := awl_plugin/xdg-shell-protocol.h \
		awl_plugin/xdg-shell-protocol.c \
		awl_plugin/xdg-output-unstable-v1-protocol.h \
		awl_plugin/xdg-output-unstable-v1-protocol.c \
		awl_plugin/wlr-layer-shell-unstable-v1-protocol.h \
		awl_plugin/wlr-layer-shell-unstable-v1-protocol.c \
		awl_plugin/awl-ipc-unstable-v2-protocol.h \
		awl_plugin/awl-ipc-unstable-v2-protocol.c \

-include Makefile.inc

.PHONY: all protocols clean

all: protocols awl libawlplugin.so
protocols: $(AWL_PROTOCOLS) $(PLUGIN_PROTOCOLS)

-include $(DEPS)

awl: $(AWL_OBJ)
	$(LD) $^ -o $@ $(LDFLAGS)

libawlplugin.so: $(PLUGIN_OBJ)
	$(LD) $^ -o $@ $(LDFLAGS) -shared

WAYLAND_SCANNER   = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header protocols/wlr-layer-shell-unstable-v1.xml $@
awl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header protocols/awl-ipc-unstable-v2.xml $@
awl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/awl-ipc-unstable-v2.xml $@

awl_plugin/xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
awl_plugin/xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
awl_plugin/xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
awl_plugin/xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
awl_plugin/wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/wlr-layer-shell-unstable-v1.xml $@
awl_plugin/wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/wlr-layer-shell-unstable-v1.xml $@
awl_plugin/awl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/awl-ipc-unstable-v2.xml $@
awl_plugin/awl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/awl-ipc-unstable-v2.xml $@

%.c.o: %.c Makefile
	$(CC) -c $< -o $@ -MMD $(CFLAGS)
%.c.o: %.c %.h Makefile
	$(CC) -c $< -o $@ -MMD $(CFLAGS)

clean:
	rm -f awl *.so *-protocol.c *-protocol.h *.o *.d \
	awl_plugin/*-protocol.c awl_plugin/*-protocol.h awl_plugin/*.o awl_plugin/*.d

