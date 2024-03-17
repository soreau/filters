#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

if len(sys.argv) < 2:
    print("Required argument: Output name")
    exit(-1)

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

has_shader = commands_sock.fs_has_shader(str(sys.argv[1]))["has-shader"]

print(f'Fullscreen {sys.argv[1]} has shader: {has_shader}')

if has_shader:
    commands_sock.unset_fs_shader(str(sys.argv[1]))
else:
    commands_sock.set_fs_shader(str(sys.argv[1]), os.path.abspath(os.path.join(os.path.dirname(__file__), '../shaders/crt')))