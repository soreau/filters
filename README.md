# Filters

## Installation
```
$ meson setup build --prefix=/usr
$ ninja -C build
# ninja -C build install
```

## Requirements

Install [pywayfire](https://github.com/WayfireWM/pywayfire) with pip.

## Usage

To set shader on view with view-id:

`./ipc-scripts/set-view-shader.py <view-id> shaders/<shader>`

To unset shader on view with view-id:

`./ipc-scripts/unset-view-shader.py <view-id>`

To set shader on output with output-name:

`./ipc-scripts/set-fs-shader.py <output-name> shaders/<shader>`

To unset shader on output with output-name:

`./ipc-scripts/unset-fs-shader.py <output-name>`

Hints:

View ID can be obtained with [wf-info](https://github.com/soreau/wf-info).

Requires ipc plugin to function.
