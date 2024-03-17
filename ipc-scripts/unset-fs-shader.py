#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

if len(sys.argv) < 2:
    print("Required argument: Output name")
    exit(-1)

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

commands_sock.unset_fs_shader(str(sys.argv[1]))
