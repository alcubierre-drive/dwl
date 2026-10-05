# vptr, the pointer injector live.sh uses
WAYLAND_SCANNER = `pkg-config --variable=wayland_scanner wayland-scanner`
PROTO = wlr-virtual-pointer-unstable-v1

vptr: vptr.c $(PROTO)-client-protocol.h $(PROTO)-protocol.c
	$(CC) -O2 -Wall -Wextra -Wno-unused-parameter -I. -o $@ vptr.c $(PROTO)-protocol.c \
		`pkg-config --cflags --libs wayland-client`
$(PROTO)-client-protocol.h: $(PROTO).xml
	$(WAYLAND_SCANNER) client-header $(PROTO).xml $@
$(PROTO)-protocol.c: $(PROTO).xml
	$(WAYLAND_SCANNER) private-code $(PROTO).xml $@
clean:
	rm -f vptr $(PROTO)-client-protocol.h $(PROTO)-protocol.c
