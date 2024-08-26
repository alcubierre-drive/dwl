_VERSION = 0.8-dev
VERSION  = `git describe --tags --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

# Uncomment to build XWayland support
XWAYLAND = -DXWAYLAND
XLIBS = xcb xcb-icccm

# dwl itself only uses C99 features, but wlroots' headers use anonymous unions (C11).
# To avoid warnings about them, we do not use -std=c99 and instead of using the
# gmake default 'CC=c99', we use cc.
CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -pedantic -Wno-unused-parameter

# CFLAGS += -fsanitize=address
# LDFLAGS += -fsanitize=address
CFLAGS += -g -ggdb
# CFLAGS += -Ofast -march=native -mtune=native -DNDEBUG -flto
# LDFLAGS += -flto=12
CFLAGS += -DUSLEEP_NOT_DEFINED \
 	  -DAWL_PULSEWIDGET_SINK=\"combine_sink\" -DAWL_PULSEWIDGET_HEAD
