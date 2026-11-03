#!/bin/bash
# Double-click on a Mac: builds the editor on first use (needs cmake and the Xcode command line tools) and starts it.
cd "$(dirname "$0")"
fail() {
  echo "$1"
  read -n 1 -s -r -p "Press any key to close"
  exit 1
}
command -v cmake >/dev/null 2>&1 || fail "cmake is missing. Install it with: brew install cmake   (or from https://cmake.org/download/)"
command -v git >/dev/null 2>&1 || fail "git is missing. Install the Xcode command line tools with: xcode-select --install"
if [ ! -x build/joue_editor ] || [ -n "$(find cpp -newer build/joue_editor -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.inc' -o -name CMakeLists.txt \) | head -1)" ]; then
  echo "Building the editor (the first time this downloads Dear ImGui, GLFW and pugixml and takes a few minutes)..."
  cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release || fail "Configuring failed. Please send the messages above."
  cmake --build build -j 4 || fail "Building failed. Please send the messages above."
fi
exec build/joue_editor
