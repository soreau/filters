#!/usr/bin/python3

# A simple script to apply a filter to untiled views.

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 2:
    print("Required arguments: </path/to/shader>")
    exit(-1)

sock = WayfireSocket()
wpe = WPE(sock)
sock.watch()

def toggle_filter(view):
    try:
        if view["tiled-edges"] == 0:
            if wpe.view_has_shader(int(view["id"]))["has-shader"] is False:
                wpe.set_view_shader(int(view["id"]), os.path.abspath(str(sys.argv[1])))
        else:
            if wpe.view_has_shader(int(view["id"]))["has-shader"] is True:
                wpe.unset_view_shader(int(view["id"]))
    except:
        pass

views = sock.list_views()
for view in views:
    toggle_filter(view)

while True:
    try:
        msg = sock.read_next_event()
        if "event" in msg and "view" in msg and (msg["view"] is not None):
            toggle_filter(msg["view"])
    except KeyboardInterrupt:
        views = sock.list_views()
        for view in views:
            try:
                wpe.unset_view_shader(int(view["id"]))
            except:
                pass
        exit(0)
