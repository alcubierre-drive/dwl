NAME := libawltray.so

CC := gcc
CXX := g++
LD := g++

PKGLIST := gio-2.0 giomm-2.4 gtkmm-3.0 gtk-layer-shell-0 dbusmenu-gtk3-0.4

CFLAGS += -Wall -Wextra -pedantic -fPIC
CXXFLAGS += -Wall -Wextra -pedantic -fPIC

FLAGS += $(shell pkg-config --cflags $(PKGLIST)) #-g -ggdb
LDFLAGS += -shared $(shell pkg-config --libs $(PKGLIST) jsoncpp spdlog wayland-client wayland-cursor pixman-1)

SRC := $(wildcard *.cpp)
OBJ := $(patsubst %.cpp,%.cpp.o,$(SRC))

DBUS_SRC := dbus-menu.c dbus-status-notifier-item.c dbus-status-notifier-watcher.c
DBUS_OBJ := $(patsubst %.c,%.c.o,$(DBUS_SRC))

.PHONY: all clean

all: $(NAME)

$(NAME): $(OBJ) $(DBUS_OBJ)
	$(LD) $^ -o $@ $(LDFLAGS)

dbus-menu.c: protocol/dbus-menu.xml
	gdbus-codegen --c-namespace Sn --generate-c-code $(patsubst %.c,%,$@) $^
dbus-status-notifier-item.c: protocol/dbus-status-notifier-item.xml
	gdbus-codegen --c-namespace Sn --generate-c-code $(patsubst %.c,%,$@) $^
dbus-status-notifier-watcher.c: protocol/dbus-status-notifier-watcher.xml
	gdbus-codegen --c-namespace Sn --generate-c-code $(patsubst %.c,%,$@) $^

dbus-%.c.o: dbus-%.c
	$(CC) -c $< -o $@ $(filter-out -pedantic,$(CFLAGS)) $(FLAGS)

%.c.o: %.c $(DBUS_SRC)
	$(CC) -c $< -o $@ $(CFLAGS) $(FLAGS)
%.cpp.o: %.cpp $(DBUS_SRC)
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(FLAGS)

clean:
	rm -f $(NAME) $(OBJ) $(DBUS_OBJ)
