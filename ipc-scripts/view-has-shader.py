#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

print(f'View {sys.argv[1]} has shader: {commands_sock.view_has_shader(int(sys.argv[1]))["has-shader"]}')
