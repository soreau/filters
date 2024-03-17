#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

if len(sys.argv) < 3:
    print("Required arguments: <View ID> </path/to/shader>")
    exit(-1)

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

commands_sock.set_view_shader(int(sys.argv[1]), os.path.abspath(str(sys.argv[2])))
