#!/bin/bash
# Linux launcher: builds the editor on first use and starts it.
# Needs: cmake, a C++17 compiler, git, and the development packages libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev libasound2-dev
cd "$(dirname "$0")"
if [ ! -x build/joue_editor ] || [ -n "$(find cpp -newer build/joue_editor -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.inc' -o -name CMakeLists.txt \) | head -1)" ]; then
  cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j "$(nproc)" || exit 1
fi
exec build/joue_editor "$@"
