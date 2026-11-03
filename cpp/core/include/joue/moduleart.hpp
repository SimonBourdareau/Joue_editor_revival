#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace joue {

// The macOS application shows every module picture mirrored top to bottom relative to the layout and blob coordinates,
// and so does this program, always.
constexpr bool MirrorPictures = true;

enum class ShapeKind { RoundRect, Ellipse, Polygon, Polyline };
enum class ShapeFill { Module, Dark };

// One drawn element in millimetres, y pointing down. Rectangles and ellipses use x, y, w, h; polygons (filled, convex)
// and polylines (stroked, width in millimetres) use points as x, y pairs.
struct Shape {
    ShapeKind kind;
    float x, y, w, h, radius;
    ShapeFill fill;
    std::vector<float> points;
    float width;
    std::string element;  // library element this shape belongs to; empty for backgrounds and marks
};

struct ModuleArt {
    float widthMm = 90.0f;
    float heightMm = 90.0f;
    bool known = false;  // false for module types without a layout in the generated table
    std::vector<Shape> shapes;  // in drawing order
};

struct Rgb {
    std::uint8_t r, g, b;
};

// Width of a module in 90 mm slots.
int moduleSlots(int type);

// Layouts of the ten Pro and five Play modules, generated from the macOS application (see tools/module_art).
ModuleArt moduleArt(int type);

// Colour ids 1 to 8 come from the original editor; 9 to 12 and 17 (Play modules) are measured from the macOS application.
Rgb moduleColor(int colorId);

}  // namespace joue
