#!/bin/sh
set -e
cd "$(dirname "$0")"

cmake -S . -B build -DXTEMPLATE_BUILD_GUI="${XTEMPLATE_BUILD_GUI:-ON}"
cmake --build build -j"$(nproc)"

exec ./build/xtemplate "$@"
