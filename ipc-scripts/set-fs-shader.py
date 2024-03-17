#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

if len(sys.argv) < 3:
    print("Required arguments: <Output name> </path/to/shader>")
    exit(-1)

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

commands_sock.set_fs_shader(str(sys.argv[1]), os.path.abspath(str(sys.argv[2])))
