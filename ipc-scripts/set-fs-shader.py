#!/usr/bin/python3

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 3:
    print("Required arguments: <Output name> </path/to/shader>")
    exit(-1)

sock = WayfireSocket()
wpe = WPE(sock)

wpe.set_fs_shader(str(sys.argv[1]), os.path.abspath(str(sys.argv[2])))
