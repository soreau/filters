#!/usr/bin/python3

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 3:
    print("Required arguments: <View ID> </path/to/shader>")
    exit(-1)

sock = WayfireSocket()
wpe = WPE(sock)

wpe.set_view_shader(int(sys.argv[1]), os.path.abspath(str(sys.argv[2])))
