#!/bin/bash
arg="left"
# can be "left" "right" "middle" or anything else
dbus-send --dest=org.freedesktop.awl / org.freedesktop.awl.wallpaper string:$arg
