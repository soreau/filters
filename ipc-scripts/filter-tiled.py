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

views = sock.list_views()
for view in views:
    if view["tiled-edges"] == 0:
        wpe.set_view_shader(int(view["id"]), os.path.abspath(str(sys.argv[1])))
    else:
        wpe.unset_view_shader(int(view["id"]))
    

while True:
    try:
        msg = sock.read_next_event()
        if "event" in msg and "view" in msg and (msg["view"] is not None):
            if msg["view"]["tiled-edges"] == 0:
                wpe.set_view_shader(int(msg["view"]["id"]), os.path.abspath(str(sys.argv[1])))
            else:
                wpe.unset_view_shader(int(msg["view"]["id"]))
    except KeyboardInterrupt:
        views = sock.list_views()
        for view in views:
            wpe.unset_view_shader(int(view["id"]))
        exit(0)
