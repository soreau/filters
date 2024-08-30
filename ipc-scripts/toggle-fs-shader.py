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

has_shader = wpe.fs_has_shader(str(sys.argv[1]))["has-shader"]

print(f'Fullscreen {sys.argv[1]} has shader: {has_shader}')

if has_shader:
    wpe.unset_fs_shader(str(sys.argv[1]))
else:
    wpe.set_fs_shader(str(sys.argv[1]), os.path.abspath(os.path.join(os.path.dirname(__file__), '../shaders/crt')))