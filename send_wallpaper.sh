#!/bin/bash
arg="left"
# can be "left" "right" "middle" or anything else
dbus-send --dest=org.freedesktop.awl.sink \
    /org/freedesktop/awl/Object \
    org.freedesktop.awl.Type.wallpaper \
    string:$arg
