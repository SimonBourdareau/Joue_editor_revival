#pragma once

#ifndef JOUE_VERSION
#define JOUE_VERSION "0"
#endif

namespace about {
constexpr const char* AppName = "Joué Revival";
constexpr const char* Author = "Simon Bourdareau";
constexpr const char* Licence = "GNU GPL v3 or later";
constexpr const char* Repository = "SimonBourdareau/Joue_editor_revival";
constexpr const char* Version = JOUE_VERSION;  // set by the build (project version in CMakeLists.txt)
}  // namespace about
