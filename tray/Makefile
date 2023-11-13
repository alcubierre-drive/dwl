NAME := awl_tray

CC := gcc
CXX := g++
LD := g++

PKGLIST := gio-2.0 giomm-2.4 gtkmm-3.0 dbusmenu-gtk3-0.4 gtk-layer-shell-0

CFLAGS += -Wall -Wextra -pedantic
CXXFLAGS += -Wall -Wextra -pedantic

FLAGS += $(shell pkg-config --cflags $(PKGLIST)) -g -ggdb
LDFLAGS += $(shell pkg-config --libs $(PKGLIST) jsoncpp spdlog wayland-client wayland-cursor pixman-1)

SRC := $(wildcard *.cpp)
OBJ := $(patsubst %.cpp,%.cpp.o,$(SRC))

DBUS_SRC := dbus-menu.c dbus-status-notifier-item.c dbus-status-notifier-watcher.c
DBUS_OBJ := $(patsubst %.c,%.c.o,$(DBUS_SRC))

.PHONY: $(NAME)

$(NAME): $(OBJ) $(DBUS_OBJ)
	$(LD) $^ -o $@ $(LDFLAGS)

dbus-menu.c: protocol/dbus-menu.xml
	gdbus-codegen --c-namespace Sn --generate-c-code $(patsubst %.c,%,$@) $^
dbus-status-notifier-item.c: protocol/dbus-status-notifier-item.xml
	gdbus-codegen --c-namespace Sn --generate-c-code $(patsubst %.c,%,$@) $^
dbus-status-notifier-watcher.c: protocol/dbus-status-notifier-watcher.xml
	gdbus-codegen --c-namespace Sn --generate-c-code $(patsubst %.c,%,$@) $^

%.c.o: %.c $(DBUS_SRC)
	$(CC) -c $< -o $@ $(CFLAGS) $(FLAGS)
%.cpp.o: %.cpp $(DBUS_SRC)
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(FLAGS)
