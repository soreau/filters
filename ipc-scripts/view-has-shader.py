#!/usr/bin/python3

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

sock = WayfireSocket()
wpe = WPE(sock)

print(f'View {sys.argv[1]} has shader: {wpe.view_has_shader(int(sys.argv[1]))["has-shader"]}')
