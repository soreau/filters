#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

commands_sock.unset_view_shader(int(sys.argv[1]))
