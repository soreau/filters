#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

commands_sock.set_fs_shader(str(sys.argv[1]), str(sys.argv[2]))
