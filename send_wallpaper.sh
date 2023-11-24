#!/bin/bash
arg="left"
# can be "left" "right" "middle" or anything else
dbus-send --dest=org.freedesktop.awl.source \
    /org/freedesktop/awl/Object \
    org.freedesktop.awl.Type.wallpaper \
    string:left
