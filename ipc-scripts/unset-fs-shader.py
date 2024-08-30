#!/usr/bin/python3

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 2:
    print("Required argument: Output name")
    exit(-1)

sock = WayfireSocket()
wpe = WPE(sock)

wpe.unset_fs_shader(str(sys.argv[1]))
