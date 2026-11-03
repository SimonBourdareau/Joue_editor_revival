#pragma once
#include "imgui.h"
#include "joue/designer.hpp"

// Draws the elements of a module layout the way the designer and the live view show them: pads and keys in the module
// colour (a pad lying on top of lower ones, such as a black key, dark), strips, strings, areas, with the note names.
// origin is the top-left corner of the module on screen; mirror flips the y axis as the macOS pictures do.
// noteShift (semitones) is added to the note names written on the elements, for a module whose notes are shifted by its octave buttons.
void drawDesign(ImDrawList* list, const joue::Design& design, ImVec2 origin, float pixelsPerMm, bool mirror, float alpha, int noteShift = 0);

// The colour of a module colour id as an ImGui colour.
ImU32 moduleColorU32(int id, float shade = 1.0f, float alpha = 1.0f);
