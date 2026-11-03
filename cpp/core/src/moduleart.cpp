#include "joue/moduleart.hpp"

namespace joue {

namespace {

struct RawShape {
    int kind;
    float x, y, w, h, radius;
    int fill;
    int pointStart, pointCount;
    float width;
    const char* element;
};

struct RawModule {
    int type, slots;
    const RawShape* shapes;
    int count;
};

#include "moduleart_data.inc"

const RawModule* find(int type) {
    for (const RawModule& module : kModules) {
        if (module.type == type) return &module;
    }
    return nullptr;
}

}  // namespace

int moduleSlots(int type) {
    const RawModule* module = find(type);
    return module ? module->slots : 1;
}

Rgb moduleColor(int colorId) {
    switch (colorId) {
        case 1: return {203, 11, 28};     // Red
        case 2: return {37, 124, 194};    // Blue
        case 3: return {235, 198, 33};    // Yellow
        case 4: return {137, 42, 214};    // Purple
        case 5: return {250, 62, 90};     // Pink
        case 6: return {5, 135, 115};     // Green
        case 7: return {245, 128, 43};    // Orange
        case 8: return {100, 100, 100};   // Gray
        case 9: return {0, 118, 204};     // Play Keys
        case 10: return {0, 178, 222};    // Play Guitar
        case 11: return {54, 204, 218};   // Play Piano
        case 12: return {123, 221, 216};  // Play Drum
        case 13: return {255, 206, 0};    // Pantone 116 C
        case 14: return {255, 159, 0};    // Pantone 137 C
        case 15: return {255, 105, 0};    // Pantone 1585 C
        case 16: return {235, 71, 3};     // Pantone 172 C
        case 17: return {129, 223, 156};  // Play Audio, water
        case 18: return {238, 39, 55};    // Play Audio, fire (Pantone 1788 C)
        case 19: return {247, 167, 44};   // sampler, dark yellow
        default: return {104, 104, 104};  // Unknown
    }
}

ModuleArt moduleArt(int type) {
    ModuleArt art;
    art.heightMm = 90.0f;
    const RawModule* module = find(type);
    if (!module) {
        art.widthMm = 90.0f;
        return art;
    }
    art.known = true;
    art.widthMm = 90.0f * static_cast<float>(module->slots);
    for (int i = 0; i < module->count; ++i) {
        const RawShape& raw = module->shapes[i];
        Shape shape;
        shape.kind = static_cast<ShapeKind>(raw.kind);
        shape.x = raw.x;
        shape.y = raw.y;
        shape.w = raw.w;
        shape.h = raw.h;
        shape.radius = raw.radius;
        shape.fill = static_cast<ShapeFill>(raw.fill);
        shape.width = raw.width;
        shape.element = raw.element;
        for (int p = 0; p < raw.pointCount * 2; ++p) shape.points.push_back(kPoints[raw.pointStart * 2 + p]);
        art.shapes.push_back(std::move(shape));
    }
    return art;
}

}  // namespace joue
