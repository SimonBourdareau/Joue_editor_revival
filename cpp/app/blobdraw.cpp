#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#define IMGUI_DEFINE_MATH_OPERATORS
#include "blobdraw.hpp"

#include <algorithm>

#include "imgui.h"
#include "joue/moduleart.hpp"

ImU32 moduleColorU32(int id, float shade, float alpha) {
    joue::Rgb c = joue::moduleColor(id);
    return IM_COL32(static_cast<int>(c.r * shade), static_cast<int>(c.g * shade), static_cast<int>(c.b * shade), static_cast<int>(255.0f * alpha));
}

void drawDesign(ImDrawList* list, const joue::Design& design, ImVec2 origin, float ppm, bool mirror, float alpha, int noteShift) {
    using joue::Box;
    const float height = static_cast<float>(joue::Design::HeightMm);
    auto toScreen = [&](float x, float y) { return ImVec2(origin.x + x * ppm, origin.y + (mirror ? height - y : y) * ppm); };
    auto rectOf = [&](const Box& b) {
        ImVec2 a = toScreen(static_cast<float>(b.x), static_cast<float>(b.y));
        ImVec2 c = toScreen(static_cast<float>(b.x + b.w), static_cast<float>(b.y + b.h));
        return ImVec4(std::min(a.x, c.x), std::min(a.y, c.y), std::max(a.x, c.x), std::max(a.y, c.y));
    };
    auto fade = [&](ImU32 color) {
        int a = static_cast<int>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
        return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    };
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < design.count(); ++i) {
        if (design.hasBox(i)) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return design.header(a).layer < design.header(b).layer; });
    int color = design.blob().header.color;
    // a pad or key lying on top of a lower one (a black key over white keys) is drawn dark, as on the real modules
    auto onTop = [&](std::size_t i) {
        joue::ElementHeader h = design.header(i);
        Box b = design.box(i);
        for (std::size_t j : order) {
            joue::ElementHeader o = design.header(j);
            if (j == i || o.layer >= h.layer || (o.type != joue::element::Pad && o.type != joue::element::Key)) continue;
            Box ob = design.box(j);
            if (b.x < ob.x + ob.w && ob.x < b.x + b.w && b.y < ob.y + ob.h && ob.y < b.y + b.h) return true;
        }
        return false;
    };
    const ImU32 darkFill = IM_COL32(51, 51, 51, 255);
    for (std::size_t i : order) {
        joue::ElementHeader h = design.header(i);
        ImVec4 r = rectOf(design.box(i));
        ImVec2 a(r.x, r.y), c(r.z, r.w);
        bool dark = (h.type == joue::element::Pad || h.type == joue::element::Key) && onTop(i);
        switch (h.type) {
            case joue::element::Pad: list->AddRectFilled(a, c, fade(dark ? darkFill : moduleColorU32(color)), 1.2f * ppm); break;
            case joue::element::Key: list->AddRectFilled(a, c, fade(dark ? darkFill : moduleColorU32(color, 0.75f)), 1.2f * ppm); break;
            case joue::element::String: list->AddRectFilled(a, c, fade(moduleColorU32(color, 0.9f)), 1.0f * ppm); break;
            case joue::element::Strip: list->AddRectFilled(a, c, fade(IM_COL32(70, 130, 190, 255)), 1.2f * ppm); break;
            case joue::element::Area:
                list->AddRectFilled(a, c, fade(darkFill), 1.5f * ppm);
                list->AddRect(a, c, fade(moduleColorU32(color)), 1.5f * ppm, 0, 2.0f);
                list->AddLine(ImVec2((a.x + c.x) / 2, a.y), ImVec2((a.x + c.x) / 2, c.y), fade(moduleColorU32(color, 0.6f)));
                list->AddLine(ImVec2(a.x, (a.y + c.y) / 2), ImVec2(c.x, (a.y + c.y) / 2), fade(moduleColorU32(color, 0.6f)));
                break;
            case joue::element::Round: list->AddEllipse((a + c) * 0.5f, (c - a) * 0.5f, fade(moduleColorU32(color)), 0.0f, 48, 2.0f); break;
            case joue::element::Glide: break;
            default: list->AddRectFilled(a, c, fade(IM_COL32(110, 110, 110, 255)), 1.0f * ppm); break;
        }
        bool labelled = h.type == joue::element::Pad || h.type == joue::element::Key || h.type == joue::element::String;
        if (labelled && c.x - a.x > 22 && c.y - a.y > 14) {
            std::string label = joue::noteName(design.field(i, joue::bodyFields(h.type, h.version)[0]) + noteShift);
            ImVec2 size = ImGui::CalcTextSize(label.c_str());
            list->AddText(ImVec2((a.x + c.x - size.x) / 2, (a.y + c.y - size.y) / 2), fade(dark ? IM_COL32(230, 230, 235, 230) : IM_COL32(15, 15, 20, 230)), label.c_str());
        }
        if (h.type == joue::element::Button && c.x - a.x > 18 && c.y - a.y > 12) {
            std::string symbol = joue::buttonSymbol(design.buttonSetting(i));
            ImVec2 size = ImGui::CalcTextSize(symbol.c_str());
            if (size.x <= c.x - a.x) list->AddText(ImVec2((a.x + c.x - size.x) / 2, (a.y + c.y - size.y) / 2), fade(IM_COL32(235, 235, 240, 230)), symbol.c_str());
        }
    }
}
