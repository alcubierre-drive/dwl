CC := gcc
LD := gcc

PKGS = wlroots wayland-server xkbcommon libinput xcb xcb-icccm fcft pixman-1 \
       wayland-client wayland-cursor

CFLAGS += $(shell pkg-config --cflags $(PKGS)) \
	-I. -DWLR_USE_UNSTABLE -DVERSION=\"0.4\" -D_POSIX_C_SOURCE=200809L \
	-fPIC
LDFLAGS += $(shell pkg-config --libs $(PKGS)) -Wl,-rpath=$(shell pwd) \
	$(LIBS) -rdynamic

DWL_SRC := $(shell find . -maxdepth 1 -type f -iname "*.c") dwl-ipc-unstable-v2-protocol.c
PLUGIN_SRC := $(shell find awl_plugin/ -maxdepth 1 -type f -iname "*.c")

DWL_OBJ := $(patsubst %.c,%.c.o,$(DWL_SRC))
PLUGIN_OBJ := $(patsubst %.c,%.c.o,$(PLUGIN_SRC))

DEPS := $(patsubst %.c,%.c.d,$(DWL_SRC) $(PLUGIN_SRC))

PROTOCOLS := xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	     dwl-ipc-unstable-v2-protocol.h dwl-ipc-unstable-v2-protocol.c

-include Makefile.inc

.PHONY: all protocols clean awl_bar/bar.a

all: awl libawlplugin.so protocols
protocols: $(PROTOCOLS)

-include $(DEPS)

awl: $(DWL_OBJ)
	$(LD) $^ -o $@ $(LDFLAGS)

libawlplugin.so: $(PLUGIN_OBJ) awl_bar/bar.a
	$(LD) $^ -o $@ $(LDFLAGS) -shared
awl_bar/bar.a:
	make -C awl_bar/

WAYLAND_SCANNER   = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
dwl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/dwl-ipc-unstable-v2.xml $@
dwl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/dwl-ipc-unstable-v2.xml $@

%.c.o: %.c Makefile
	$(CC) -c $< -o $@ -MMD $(CFLAGS)
%.c.o: %.c %.h Makefile
	$(CC) -c $< -o $@ -MMD $(CFLAGS)

clean:
	rm -f awl *.o *-protocol.h *.so *.d \
		awl_plugin/*.o awl_plugin/*.d \
		awl_bar/*.d awl_bar/*.a awl_bar/*.o

