#pragma once
struct ImFont;

// The large font for headings, loaded at its own size so that big text is drawn from glyphs made for that size and not from the small
// font enlarged, which looks blurred. Null when it was not loaded (the callers then enlarge the usual font).
struct Fonts {
    static inline ImFont* heading = nullptr;
};
