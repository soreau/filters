#!/usr/bin/python3

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

sock = WayfireSocket()
wpe = WPE(sock)

print(f'Output {sys.argv[1]} has shader: {wpe.fs_has_shader(sys.argv[1])["has-shader"]}')
