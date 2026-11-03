#pragma once
#include <string>

#include "joue/designer.hpp"

namespace joue {

// What goes on the sheet when a layout is printed at real size.
struct PrintOptions {
    bool letter = false;  // US Letter, else A4; the sheet is landscape, so that three slots (270 mm) fit
    bool labels = true;   // the note name in pads, keys and strings that are big enough
    bool grid = false;    // a light 10 mm grid
    bool marks = true;    // crop marks at the corners of the module, and a scale bar of exactly 100 mm
    bool mirror = true;   // as the designer draws the module (mirrored); false: as the layout is numbered
    std::string title;    // a line at the top of the sheet
};

// Where the module lies on the sheet, in millimetres from its bottom left corner.
struct PrintPage {
    double widthMm = 0, heightMm = 0;
    double moduleLeftMm = 0, moduleBottomMm = 0;
    double marginMm = 0;  // between the module and the nearer side of the sheet
};

PrintPage printPage(const Design& design, const PrintOptions& options);

// A PDF of one page whose units are exact: a length of 1 mm on the module is 1 mm on the paper, when it is printed at 100 % ("actual size").
std::string designToPdf(const Design& design, const PrintOptions& options);

}  // namespace joue
