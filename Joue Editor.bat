@echo off
rem Windows launcher: builds the editor on first use (needs CMake and Visual Studio Build Tools or MinGW) and starts it.
cd /d "%~dp0"
where cmake >nul 2>nul || (echo CMake is missing. Get it from https://cmake.org/download/ & pause & exit /b 1)
if not exist build\Release\joue_editor.exe if not exist build\joue_editor.exe (
  echo Building the editor, the first time this downloads Dear ImGui, GLFW and pugixml...
  cmake -S cpp -B build || (pause & exit /b 1)
  cmake --build build --config Release || (pause & exit /b 1)
)
if exist build\Release\joue_editor.exe (start "" build\Release\joue_editor.exe) else (start "" build\joue_editor.exe)
