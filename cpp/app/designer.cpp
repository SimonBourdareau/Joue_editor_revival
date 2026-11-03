#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include "designer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iterator>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_stdlib.h"
#include "blobdraw.hpp"
#include "joue/moduleart.hpp"
#include "joue/printout.hpp"
#include "joue/i18n.hpp"
#include "joue/modulewrite.hpp"
#include "joue/moduleart.hpp"

using joue::i18n::tr;

namespace {

using joue::Box;

const std::uint8_t ToolTypes[] = {0, joue::element::Pad, joue::element::Key, joue::element::Strip, joue::element::String, joue::element::Area, joue::element::Button};
const char* ToolNames[] = {"Select", "Pad", "Key", "Strip", "String", "Area", "Button"};
const int KnownTypes[] = {1, 3, 5, 6, 7, 9, 10, 12, 14, 17, 22, 23, 24, 25, 27};

Box defaultSize(std::uint8_t type, int x, int y) {
    switch (type) {
        case joue::element::Key: return {x, y, 12, 60};
        case joue::element::Strip: return {x, y, 40, 8};
        case joue::element::String: return {x, y, 100, 9};
        case joue::element::Area: return {x, y, 30, 30};
        default: return {x, y, 14, 14};
    }
}

}  // namespace

void Designer::touchSelect(int index) {
    if (index >= 0 && index < static_cast<int>(design_.count()) && design_.hasBox(static_cast<std::size_t>(index))) pickOnly(index);
}

void Designer::clearPicks() {
    picked_.clear();
    selected_ = -1;
}

void Designer::pickOnly(int index) {
    picked_.clear();
    selected_ = index;
    if (index >= 0) picked_.push_back(index);
}

bool Designer::isPicked(int index) const { return std::find(picked_.begin(), picked_.end(), index) != picked_.end(); }

void Designer::addPick(int index) {
    if (index < 0) return;
    if (!isPicked(index)) {
        picked_.push_back(index);
        std::sort(picked_.begin(), picked_.end());
    }
    selected_ = index;
}

void Designer::togglePick(int index) {
    if (index < 0) return;
    auto it = std::find(picked_.begin(), picked_.end(), index);
    if (it == picked_.end()) {
        addPick(index);
        return;
    }
    picked_.erase(it);
    if (selected_ == index) selected_ = picked_.empty() ? -1 : picked_.back();
}

Designer::Snapshot Designer::snapshot() const { return {design_.toBytes(), design_.slots()}; }

void Designer::restore(const Snapshot& s) {
    design_ = joue::Design::fromBytes(s.data);
    design_.setSlots(s.slots);
    picked_.erase(std::remove_if(picked_.begin(), picked_.end(), [&](int i) { return i >= static_cast<int>(design_.count()) || !design_.hasBox(static_cast<std::size_t>(i)); }), picked_.end());
    if (selected_ >= static_cast<int>(design_.count()) || !isPicked(selected_)) selected_ = picked_.empty() ? -1 : picked_.back();
    scriptLoaded_ = false;
}

void Designer::pushUndo() {
    undo_.push_back(snapshot());
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    dirty_ = true;
}

void Designer::undo() {
    if (undo_.empty()) return;
    redo_.push_back(snapshot());
    restore(undo_.back());
    undo_.pop_back();
    dirty_ = true;
}

void Designer::redo() {
    if (redo_.empty()) return;
    undo_.push_back(snapshot());
    restore(redo_.back());
    redo_.pop_back();
    dirty_ = true;
}

void Designer::newDesign(int slots) {
    design_ = joue::Design(slots);
    undo_.clear();
    redo_.clear();
    clearPicks();
    dirty_ = false;
    scriptLoaded_ = false;
    status_ = "New empty module on " + std::to_string(slots) + (slots == 1 ? " slot." : " slots.");
}

bool Designer::openFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        status_ = "Cannot open " + path;
        return false;
    }
    joue::Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        design_ = joue::Design::fromBytes(bytes);
    } catch (const std::exception& e) {
        status_ = std::string("Not a module blob: ") + e.what();
        return false;
    }
    path_ = path;
    undo_.clear();
    redo_.clear();
    clearPicks();
    dirty_ = false;
    scriptLoaded_ = false;
    status_ = "Opened " + path + " (" + std::to_string(bytes.size()) + " bytes, " + std::to_string(design_.count()) + " elements).";
    return true;
}

bool Designer::saveFile(const std::string& path) {
    for (const auto& line : design_.problems()) {
        if (line.rfind("Error", 0) == 0) {
            status_ = "Not saved: " + line;
            return false;
        }
    }
    std::ofstream out(path, std::ios::binary);
    joue::Bytes bytes = design_.toBytes();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        status_ = "Cannot write " + path;
        return false;
    }
    path_ = path;
    dirty_ = false;
    status_ = "Saved " + path + " (" + std::to_string(bytes.size()) + " bytes).";
    return true;
}

int Designer::snapTo(float value) const { return static_cast<int>(std::lround(value / static_cast<float>(snap_))) * snap_; }

void Designer::draw() {
    if (!open) return;
    float scale = ImGui::GetFontSize() / 15.0f;
    ImGui::SetNextWindowSize(ImVec2(1180 * scale, 760 * scale), ImGuiCond_FirstUseEver);
    std::string title = std::string(tr("Module designer")) + (dirty_ ? " *" : "") + "###designer";
    if (!ImGui::Begin(title.c_str(), &open)) {
        ImGui::End();
        return;
    }
    drawToolbar();
    ImGui::Separator();
    float inspectorWidth = 360.0f * scale;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("designerCanvas", ImVec2(-(inspectorWidth + spacing), 0), ImGuiChildFlags_Borders);
    drawCanvas();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("designerInspector", ImVec2(0, 0));
    drawInspector();
    ImGui::EndChild();
    ImGui::End();
}

void Designer::drawToolbar() {
    float scale = ImGui::GetFontSize() / 15.0f;
    if (ImGui::Button(tr("New"))) ImGui::OpenPopup("newdesign");
    if (ImGui::BeginPopup("newdesign")) {
        for (int slots = 1; slots <= 3; ++slots) {
            char label[96];
            std::snprintf(label, sizeof label, slots == 1 ? tr("%d slot (%d mm)") : tr("%d slots (%d mm)"), slots, slots * 90);
            if (ImGui::Selectable(label)) newDesign(slots);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(undo_.empty());
    if (ImGui::Button(tr("Undo"))) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redo_.empty());
    if (ImGui::Button(tr("Redo"))) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", "|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(330 * scale);
    ImGui::InputText(tr("##path"), &path_);
    ImGui::SameLine();
    if (ImGui::Button(tr("Open"))) openFile(path_);
    ImGui::SameLine();
    if (ImGui::Button(tr("Save"))) saveFile(path_);
    if (chooseOpen) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Browse..."))) {
            if (auto picked = chooseOpen()) openFile(*picked);
        }
    }
    if (chooseSave) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Save as..."))) {
            if (auto picked = chooseSave()) saveFile(*picked);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Print..."))) {
        std::filesystem::path pdf(path_);
        pdf.replace_extension(".pdf");
        printPath_ = pdf.string();
        printStatus_.clear();
        ImGui::OpenPopup(tr("Print at real size##printdesign"));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Print the layout at real size: a PDF with the page in exact millimetres, to print at 100 %%."));
    drawPrintWindow();
    ImGui::SameLine();
    ImGui::BeginDisabled(true);
    ImGui::Button(tr("Send to module"));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr("Sending from the designer is not in this version: use Edit on a module in the live window.\nThe file is a module blob, the data a module's tag holds."));
    }
    {
        // a virtual module: the layout goes to the board as a temporary one, with nothing on the device
        bool can = canLoadTemporarily && canLoadTemporarily();
        ImGui::BeginDisabled(!can);
        if (ImGui::Button(tr("Play on the device (temporary)..."))) ImGui::OpenPopup(tr("Play on the device (temporary)##temporaryload"));
        ImGui::EndDisabled();
        ImGui::SameLine();
        {
            bool tagReady = canWriteTag && canWriteTag();
            ImGui::BeginDisabled(!tagReady);
            if (ImGui::Button(tr("Write to a tag..."))) {
                tagConfirmed_ = false;
                ImGui::OpenPopup(tr("Write the layout to a blank tag##writetag"));
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Erase a tag..."))) {
                eraseConfirmed_ = false;
                ImGui::OpenPopup(tr("Erase a tag##erasetag"));
            }
            ImGui::EndDisabled();
            drawTagWindow();
            drawEraseWindow();
        }
        ImGui::SameLine();
        if (!can) ImGui::TextDisabled("%s", tr("Needs the live instrument open and the Joue connected."));
        else if (loadStatus && !loadStatus().empty()) ImGui::TextDisabled("%s", joue::i18n::message(loadStatus()).c_str());
        if (ImGui::BeginPopupModal(tr("Play on the device (temporary)##temporaryload"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(tr("Sends this layout to the Joué as a temporary module."));
            ImGui::BulletText("%s", tr("Nothing is written to a module."));
            ImGui::BulletText("%s", tr("It goes to slot 1 after power-up."));
            ImGui::BulletText("%s", tr("Lost at a reset, or when a module is inserted or removed."));
            ImGui::Spacing();
            bool bad = false;
            for (const std::string& line : design_.problems()) bad = bad || line.rfind("Error", 0) == 0;
            ImGui::BeginDisabled(bad);
            if (ImGui::Button(tr("Send"))) {
                if (loadTemporarily) loadTemporarily(design_.toBytes());
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (bad) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", tr("The layout has an error (see Check)."));
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (!status_.empty()) ImGui::TextDisabled("%s", joue::i18n::message(status_).c_str());
}

void Designer::drawCanvas() {
    const bool mirror = joue::MirrorPictures;
    ImGuiIO& io = ImGui::GetIO();
    float width = static_cast<float>(design_.widthMm()), height = static_cast<float>(joue::Design::HeightMm);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float margin = 6.0f;
    float ppm = std::max(1.0f, std::min(avail.x / (width + 2 * margin), avail.y / (height + 2 * margin)));
    ImVec2 origin = ImGui::GetCursorScreenPos() + ImVec2(margin * ppm, margin * ppm);
    ImGui::InvisibleButton("##surface", avail, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* list = ImGui::GetWindowDrawList();
    // ctrl, or cmd on a Mac where ctrl-click is a right click, adds to the picking
    const bool add = io.KeyCtrl || io.KeySuper;

    auto toScreenY = [&](float y) { return origin.y + (mirror ? (height - y) : y) * ppm; };
    auto toScreen = [&](float x, float y) { return ImVec2(origin.x + x * ppm, toScreenY(y)); };
    auto toMm = [&](ImVec2 p) { float y = (p.y - origin.y) / ppm; return ImVec2((p.x - origin.x) / ppm, mirror ? height - y : y); };
    auto rectOf = [&](const Box& b) {
        ImVec2 a = toScreen(static_cast<float>(b.x), static_cast<float>(b.y));
        ImVec2 c = toScreen(static_cast<float>(b.x + b.w), static_cast<float>(b.y + b.h));
        return ImVec4(std::min(a.x, c.x), std::min(a.y, c.y), std::max(a.x, c.x), std::max(a.y, c.y));
    };

    list->AddRectFilled(origin, origin + ImVec2(width * ppm, height * ppm), IM_COL32(29, 28, 33, 255), 4.5f * ppm);
    if (grid_) {
        for (int x = 10; x < design_.widthMm(); x += 10) {
            bool slotEdge = x % joue::Design::SlotWidthMm == 0;
            list->AddLine(toScreen(static_cast<float>(x), 0), toScreen(static_cast<float>(x), height), slotEdge ? IM_COL32(120, 120, 140, 160) : IM_COL32(60, 60, 70, 140), slotEdge ? 1.5f : 1.0f);
        }
        for (int y = 10; y < joue::Design::HeightMm; y += 10) list->AddLine(toScreen(0, static_cast<float>(y)), toScreen(width, static_cast<float>(y)), IM_COL32(60, 60, 70, 140));
    }

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < design_.count(); ++i) {
        if (design_.hasBox(i)) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return design_.header(a).layer < design_.header(b).layer; });
    drawDesign(list, design_, origin, ppm, mirror, 1.0f);

    // everything picked is outlined; the one the inspector shows also has its handles
    for (int index : picked_) {
        if (index < 0 || !design_.hasBox(static_cast<std::size_t>(index))) continue;
        ImVec4 r = rectOf(design_.box(static_cast<std::size_t>(index)));
        bool primary = index == selected_;
        list->AddRect(ImVec2(r.x, r.y), ImVec2(r.z, r.w), primary ? IM_COL32(15, 190, 255, 255) : IM_COL32(100, 175, 255, 230), 0.0f, 0, primary ? 2.0f : 1.5f);
    }
    if (selected_ >= 0 && design_.hasBox(static_cast<std::size_t>(selected_))) {
        ImVec4 r = rectOf(design_.box(static_cast<std::size_t>(selected_)));
        for (int hnd = 0; hnd < 4; ++hnd) {
            ImVec2 p((hnd & 1) ? r.z : r.x, (hnd & 2) ? r.w : r.y);
            list->AddRectFilled(p - ImVec2(4, 4), p + ImVec2(4, 4), IM_COL32(15, 190, 255, 255));
        }
    }
    if (swapTarget_ >= 0 && swapTarget_ < static_cast<int>(design_.count()) && design_.hasBox(static_cast<std::size_t>(swapTarget_))) {
        // letting go now would swap the dragged element with this one
        ImVec4 r = rectOf(design_.box(static_cast<std::size_t>(swapTarget_)));
        list->AddRect(ImVec2(r.x - 2, r.y - 2), ImVec2(r.z + 2, r.w + 2), IM_COL32(70, 225, 100, 255), 0.0f, 0, 3.0f);
    }

    // pointer handling
    ImVec2 mouse = toMm(io.MousePos);
    struct Zone {
        int handle = -1;
        int axis = 0;
    };
    // a corner of the element the inspector shows stretches both ways, an edge one way
    auto zoneAt = [&]() -> Zone {
        if (selected_ < 0 || !design_.hasBox(static_cast<std::size_t>(selected_))) return {};
        Box b = design_.box(static_cast<std::size_t>(selected_));
        for (int hnd = 0; hnd < 4; ++hnd) {
            ImVec2 p = toScreen(static_cast<float>(b.x + ((hnd & 1) ? b.w : 0)), static_cast<float>(b.y + ((hnd & 2) ? b.h : 0)));
            if (std::fabs(io.MousePos.x - p.x) <= 7 && std::fabs(io.MousePos.y - p.y) <= 7) return {hnd, 0};
        }
        ImVec4 r = rectOf(b);
        const float t = 5.0f;
        bool inX = io.MousePos.x >= r.x - t && io.MousePos.x <= r.z + t, inY = io.MousePos.y >= r.y - t && io.MousePos.y <= r.w + t;
        if (inY && std::fabs(io.MousePos.x - toScreen(static_cast<float>(b.x), 0).x) <= t) return {0, 1};
        if (inY && std::fabs(io.MousePos.x - toScreen(static_cast<float>(b.x + b.w), 0).x) <= t) return {1, 1};
        if (inX && std::fabs(io.MousePos.y - toScreenY(static_cast<float>(b.y))) <= t) return {0, 2};
        if (inX && std::fabs(io.MousePos.y - toScreenY(static_cast<float>(b.y + b.h))) <= t) return {2, 2};
        return {};
    };
    auto elementAt = [&](int skip) -> int {
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            if (static_cast<int>(*it) == skip || design_.blob().elements[*it].type() == joue::element::Glide) continue;
            Box b = design_.box(*it);
            if (mouse.x >= b.x && mouse.x <= b.x + b.w && mouse.y >= b.y && mouse.y <= b.y + b.h) return static_cast<int>(*it);
        }
        return -1;
    };
    if (hovered && tool_ == 0 && drag_ == Drag::None) {
        Zone z = zoneAt();
        if (z.handle >= 0 && (z.axis == 0 || io.KeyShift)) ImGui::SetMouseCursor(z.axis == 1 ? ImGuiMouseCursor_ResizeEW : z.axis == 2 ? ImGuiMouseCursor_ResizeNS : (z.handle == 0 || z.handle == 3) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
        else if (elementAt(-1) >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemActivated()) {
        swapTarget_ = -1;
        travel_ = 0;
        dragAxis_ = 0;
        if (tool_ > 0 || (io.KeyAlt && !add)) {
            // a tool is chosen, or alt is held: the drag draws a new element
            int tool = tool_ > 0 ? tool_ : lastTool_;
            lastTool_ = tool;
            pushUndo();
            int x = std::max(0, snapTo(mouse.x)), y = std::max(0, snapTo(mouse.y));
            std::size_t index = design_.add(ToolTypes[tool], {x, y, 2, 2});
            if (index < design_.count()) {
                select(static_cast<int>(index));
                dragBox_ = design_.box(index);
                dragStartX_ = mouse.x;
                dragStartY_ = mouse.y;
                dragHandle_ = 3;
                drag_ = Drag::Resize;
                created_ = true;
            } else {
                status_ = "No room for another element (255 at most).";
                undo_.pop_back();
            }
        } else if (add) {
            // ctrl-click toggles one element; going on with the button down gathers every element the pointer crosses
            togglePick(elementAt(-1));
            drag_ = Drag::Gather;
        } else {
            Zone z = zoneAt();
            if (z.handle >= 0 && (z.axis == 0 || io.KeyShift)) {
                pushUndo();
                drag_ = Drag::Resize;
                dragHandle_ = z.handle;
                dragAxis_ = z.axis;
                dragBox_ = design_.box(static_cast<std::size_t>(selected_));
                dragStartX_ = mouse.x;
                dragStartY_ = mouse.y;
            } else {
                int hit = elementAt(-1);
                if (io.KeyShift && hit >= 0 && isPicked(hit) && picked_.size() > 1) {
                    // shift-drag on one of several picked elements moves them all, keeping their spacing
                    pushUndo();
                    groupStart_.clear();
                    for (int index : picked_) groupStart_.push_back(design_.box(static_cast<std::size_t>(index)));
                    selected_ = hit;
                    drag_ = Drag::Group;
                    dragBox_ = design_.box(static_cast<std::size_t>(hit));
                    dragStartX_ = mouse.x;
                    dragStartY_ = mouse.y;
                } else {
                    select(hit);
                    if (hit >= 0) {
                        pushUndo();
                        drag_ = Drag::Move;
                        dragBox_ = design_.box(static_cast<std::size_t>(hit));
                        dragStartX_ = mouse.x;
                        dragStartY_ = mouse.y;
                    }
                }
            }
        }
    }
    if (drag_ == Drag::Gather && ImGui::IsItemActive()) {
        int over = elementAt(-1);
        if (over >= 0) addPick(over);
    } else if (drag_ == Drag::Group && ImGui::IsItemActive() && !picked_.empty() && groupStart_.size() == picked_.size()) {
        float dx = mouse.x - dragStartX_, dy = mouse.y - dragStartY_;
        int ddx = snapTo(static_cast<float>(dragBox_.x) + dx) - dragBox_.x, ddy = snapTo(static_cast<float>(dragBox_.y) + dy) - dragBox_.y;
        int minX = 1 << 20, minY = 1 << 20, maxX = -(1 << 20), maxY = -(1 << 20);
        for (const Box& b : groupStart_) {
            minX = std::min(minX, b.x);
            minY = std::min(minY, b.y);
            maxX = std::max(maxX, b.x + b.w);
            maxY = std::max(maxY, b.y + b.h);
        }
        // the group stops at the edge of the surface as one piece
        ddx = std::max(-minX, std::min(design_.widthMm() - maxX, ddx));
        ddy = std::max(-minY, std::min(joue::Design::HeightMm - maxY, ddy));
        for (std::size_t k = 0; k < picked_.size(); ++k) design_.setBox(static_cast<std::size_t>(picked_[k]), groupStart_[k]);
        std::vector<std::size_t> indices(picked_.begin(), picked_.end());
        design_.moveGroup(indices, ddx, ddy);
    } else if ((drag_ == Drag::Move || drag_ == Drag::Resize) && ImGui::IsItemActive() && selected_ >= 0) {
        float dx = mouse.x - dragStartX_, dy = mouse.y - dragStartY_;
        Box b = dragBox_;
        if (drag_ == Drag::Move) {
            b.x = snapTo(static_cast<float>(dragBox_.x) + dx);
            b.y = snapTo(static_cast<float>(dragBox_.y) + dy);
            // a shift-drag that went far enough and is over another element would swap the two when let go
            travel_ = std::sqrt(dx * dx + dy * dy);
            swapTarget_ = (io.KeyShift && travel_ >= 8.0f) ? elementAt(selected_) : -1;
        } else if (created_) {
            // a new element spans from where the button went down to where the pointer is, whichever way it is dragged
            int x0 = snapTo(dragStartX_), y0 = snapTo(dragStartY_), x1 = snapTo(mouse.x), y1 = snapTo(mouse.y);
            b = {std::min(x0, x1), std::min(y0, y1), std::max(2, std::abs(x1 - x0)), std::max(2, std::abs(y1 - y0))};
        } else {
            if (dragAxis_ != 2) {
                if (dragHandle_ & 1) b.w = snapTo(static_cast<float>(dragBox_.x + dragBox_.w) + dx) - dragBox_.x;
                else { b.x = snapTo(static_cast<float>(dragBox_.x) + dx); b.w = dragBox_.x + dragBox_.w - b.x; }
            }
            if (dragAxis_ != 1) {
                if (dragHandle_ & 2) b.h = snapTo(static_cast<float>(dragBox_.y + dragBox_.h) + dy) - dragBox_.y;
                else { b.y = snapTo(static_cast<float>(dragBox_.y) + dy); b.h = dragBox_.y + dragBox_.h - b.y; }
            }
            if (b.w < 2) { if (!(dragHandle_ & 1)) b.x = dragBox_.x + dragBox_.w - 2; b.w = 2; }
            if (b.h < 2) { if (!(dragHandle_ & 2)) b.y = dragBox_.y + dragBox_.h - 2; b.h = 2; }
        }
        design_.setBox(static_cast<std::size_t>(selected_), b);
    }
    if (drag_ != Drag::None && !ImGui::IsItemActive()) {
        if (drag_ == Drag::Move && swapTarget_ >= 0 && selected_ >= 0) {
            // let go over another element after a long enough shift-drag: the two trade places, each keeping its size and settings
            design_.setBox(static_cast<std::size_t>(selected_), dragBox_);
            if (design_.swapPositions(static_cast<std::size_t>(selected_), static_cast<std::size_t>(swapTarget_))) status_ = "Swapped the two elements.";
            else status_ = "Not swapped: one of them would reach off the surface.";
        }
        if (created_ && selected_ >= 0) {
            Box b = design_.box(static_cast<std::size_t>(selected_));
            if (b.w <= 4 && b.h <= 4) {
                joue::ElementHeader h = design_.header(static_cast<std::size_t>(selected_));
                design_.setBox(static_cast<std::size_t>(selected_), defaultSize(h.type, b.x, b.y));
            }
            tool_ = 0;
        }
        created_ = false;
        swapTarget_ = -1;
        drag_ = Drag::None;
    }

    // keys act while the pointer is over the surface or the window has the focus
    if ((hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) && !io.WantTextInput) {
        bool command = io.KeyCtrl || io.KeySuper;
        if (command && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (io.KeyShift) redo(); else undo();
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_A)) {
            clearPicks();
            for (std::size_t i : order) addPick(static_cast<int>(i));
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_C) && !picked_.empty()) {
            clipboard_.clear();
            for (int index : picked_) clipboard_.push_back(design_.blob().elements[static_cast<std::size_t>(index)]);
            pasteCount_ = 0;
            status_ = "Copied " + std::to_string(clipboard_.size()) + (clipboard_.size() == 1 ? " element." : " elements.");
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboard_.empty()) {
            pushUndo();
            ++pasteCount_;
            std::vector<int> added;
            for (const joue::ElementRecord& record : clipboard_) {
                std::size_t index = design_.paste(record, 4 * pasteCount_, 4 * pasteCount_);
                if (index < design_.count()) added.push_back(static_cast<int>(index));
            }
            if (added.empty()) {
                status_ = "No room for the pasted elements (255 at most).";
                undo_.pop_back();
            } else {
                clearPicks();
                for (int index : added) addPick(index);
            }
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_D) && !picked_.empty()) {
            pushUndo();
            std::vector<int> copies;
            for (int index : std::vector<int>(picked_)) {
                std::size_t copy = design_.duplicate(static_cast<std::size_t>(index));
                if (copy < design_.count()) copies.push_back(static_cast<int>(copy));
            }
            if (copies.empty()) {
                undo_.pop_back();
            } else {
                clearPicks();
                for (int index : copies) addPick(index);
            }
        } else if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && !picked_.empty()) {
            pushUndo();
            std::vector<int> doomed(picked_);
            std::sort(doomed.rbegin(), doomed.rend());
            for (int index : doomed) design_.remove(static_cast<std::size_t>(index));
            clearPicks();
            scriptLoaded_ = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            tool_ = 0;
            clearPicks();
        } else if (!picked_.empty()) {
            int step = io.KeyShift ? 10 : 1;
            int dxm = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? step : 0) - (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? step : 0);
            int dym = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? step : 0) - (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? step : 0);
            if (mirror) dym = -dym;
            if (dxm || dym) {
                pushUndo();
                std::vector<std::size_t> indices(picked_.begin(), picked_.end());
                if (!design_.moveGroup(indices, dxm, dym)) undo_.pop_back();
            }
        }
    }
}

bool Designer::savePdf(const std::string& path) {
    joue::PrintOptions options;
    options.letter = printLetter_;
    options.labels = printLabels_;
    options.grid = printGrid_;
    options.marks = printMarks_;
    options.mirror = joue::MirrorPictures;
    options.title = std::filesystem::path(path_).filename().string();
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        printStatus_ = "The file " + path + " cannot be written.";
        return false;
    }
    out << joue::designToPdf(design_, options);
    out.close();
    printStatus_ = "Saved " + path;
    return true;
}

std::vector<std::size_t> Designer::pickedIndices() const {
    std::vector<std::size_t> out;
    for (int i : picked_) {
        if (i >= 0 && i < static_cast<int>(design_.count()) && design_.hasBox(static_cast<std::size_t>(i))) out.push_back(static_cast<std::size_t>(i));
    }
    return out;
}

// Align, distribute, flip and repeat what is picked.
void Designer::drawArrange() {
    float scale = ImGui::GetFontSize() / 15.0f;
    const bool mirror = joue::MirrorPictures;
    std::vector<std::size_t> picked = pickedIndices();
    ImGui::SeparatorText(tr("Arrange"));
    auto row = [&](const char* label, bool enabled, const std::vector<std::pair<const char*, std::function<void()>>>& buttons) {
        const float limit = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        ImGui::BeginDisabled(!enabled);
        ImGui::TextUnformatted(label);
        for (const auto& b : buttons) {
            float width = ImGui::CalcTextSize(b.first).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= limit) ImGui::SameLine();
            if (ImGui::SmallButton(b.first)) {
                pushUndo();
                b.second();
                scriptLoaded_ = false;
            }
        }
        ImGui::EndDisabled();
    };
    row(tr("Align"), !picked.empty(), {{tr("Left"), [&] { design_.alignX(picked, joue::Edge::Low); }}, {tr("Centre"), [&] { design_.alignX(picked, joue::Edge::Centre); }}, {tr("Right"), [&] { design_.alignX(picked, joue::Edge::High); }},
                                  {tr("Top"), [&] { design_.alignY(picked, mirror ? joue::Edge::High : joue::Edge::Low); }}, {tr("Middle"), [&] { design_.alignY(picked, joue::Edge::Centre); }}, {tr("Bottom"), [&] { design_.alignY(picked, mirror ? joue::Edge::Low : joue::Edge::High); }}});
    row(tr("Distribute"), picked.size() >= 3, {{tr("Across"), [&] { design_.distributeX(picked); }}, {tr("Up and down"), [&] { design_.distributeY(picked); }}});
    row(tr("Flip"), !picked.empty(), {{tr("Left-right"), [&] { design_.flipX(picked); }}, {tr("Up-down"), [&] { design_.flipY(picked); }}});
    ImGui::BeginDisabled(picked.empty());
    if (ImGui::Button(tr("Repeat..."))) ImGui::OpenPopup("repeat");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Pick one or more elements first."));
    if (ImGui::BeginPopup("repeat")) {
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Copies"), &repeatCopies_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Across (mm)"), &repeatDx_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(mirror ? tr("Up (mm)") : tr("Down (mm)"), &repeatDy_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Note step"), &repeatNoteStep_);
        if (ImGui::Button(tr("Repeat"))) {
            pushUndo();
            std::vector<std::size_t> made = design_.repeat(picked, std::max(1, repeatCopies_), repeatDx_, repeatDy_, repeatNoteStep_);
            if (made.empty()) {
                status_ = "No copy fits: change the offset or the number.";
                undo_.pop_back();
            } else {
                clearPicks();
                for (std::size_t i : made) addPick(static_cast<int>(i));
                status_ = "Made " + std::to_string(made.size() / std::max<std::size_t>(1, picked.size())) + " copies.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The window of "Print...": the layout as a PDF in real millimetres.
void Designer::drawPrintWindow() {
    float scale = ImGui::GetFontSize() / 15.0f;
    if (!ImGui::BeginPopupModal(tr("Print at real size##printdesign"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(tr("Saves a PDF in real millimetres."));
    ImGui::TextDisabled(tr("Print at 100 %% (actual size), not \"fit to page\"."));
    ImGui::TextDisabled("%s", tr("The bar on the sheet must measure 100 mm."));
    ImGui::Spacing();
    if (ImGui::RadioButton(tr("A4"), !printLetter_)) printLetter_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("US Letter"), printLetter_)) printLetter_ = true;
    ImGui::Checkbox(tr("Note names"), &printLabels_);
    ImGui::Checkbox(tr("10 mm grid"), &printGrid_);
    ImGui::Checkbox(tr("Crop marks and scale bar"), &printMarks_);
    ImGui::SetNextItemWidth(430.0f * scale);
    ImGui::InputText(tr("##pdfpath"), &printPath_);
    if (chooseSave) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Browse..."))) {
            if (auto picked = chooseSave()) {
                std::filesystem::path chosen(*picked);
                if (chosen.extension() != ".pdf") chosen.replace_extension(".pdf");
                printPath_ = chosen.string();
            }
        }
    }
    joue::PrintOptions probe;
    probe.letter = printLetter_;
    double margin = joue::printPage(design_, probe).marginMm;
    if (margin < 8.0) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), tr("Only %.1f mm of margin: use A4."), margin);
    }
    ImGui::Spacing();
    if (ImGui::Button(tr("Save PDF"))) savePdf(printPath_);
    ImGui::SameLine();
    if (ImGui::Button(tr("Save PDF and open it"))) {
        if (savePdf(printPath_) && openExternally) openExternally(printPath_);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
    if (!printStatus_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f * scale);
        ImGui::TextWrapped("%s", joue::i18n::message(printStatus_).c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

// The window of "Write to a tag...": the layout goes to a blank NTAG216 lying in a slot, so that it can become a module of its own.
bool Designer::drawSlotState(int slot, bool erase) {
    const float scale = ImGui::GetFontSize() / 15.0f;
    const ImVec4 red(1.0f, 0.45f, 0.4f, 1.0f), green(0.45f, 0.85f, 0.5f, 1.0f), amber(0.95f, 0.8f, 0.4f, 1.0f);
    std::pair<bool, joue::Bytes> found = slotModule ? slotModule(slot) : std::make_pair(false, joue::Bytes());
    joue::TagKind kind = found.first ? joue::classifyTag(found.second) : joue::TagKind::Empty;
    std::string name = found.first && !found.second.empty() ? joue::moduleName(found.second[0]) : std::string();
    ImVec4 color = green;
    char line[512];
    bool allowed = false;
    if (kind == joue::TagKind::Empty) {
        color = erase ? amber : green;
        std::snprintf(line, sizeof line, erase ? tr("Slot %d has no module: there is nothing to erase.") : tr("Slot %d has no module: a blank tag can be written here."), slot);
        allowed = !erase;
    } else if (kind == joue::TagKind::Manufacturer) {
        color = red;
        std::snprintf(line, sizeof line, erase ? tr("Slot %d holds a %s, a manufacturer's module. It is never erased: its layout could not be put back. To change its settings, use Edit on it in the live window.") : tr("Slot %d holds a %s, a manufacturer's module. It cannot be overwritten: its layout would be lost. Take it out to write a tag here."), slot, name.c_str());
    } else if (kind == joue::TagKind::Custom) {
        color = erase ? green : amber;
        std::snprintf(line, sizeof line, erase ? tr("Slot %d holds a %s with a custom layout. It can be erased: the tag is read twice first.") : tr("Slot %d holds a %s with a custom layout. It cannot be overwritten directly: erase it first with Erase a tag."), slot, name.c_str());
        allowed = erase;
    } else {
        color = red;
        std::snprintf(line, sizeof line, erase ? tr("Slot %d holds a layout that cannot be told from a manufacturer's. It is not erased, since it may be a manufacturer's.") : tr("Slot %d holds a layout that cannot be told from a manufacturer's. It cannot be overwritten: take it out to write a tag here."), slot);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440.0f * scale);
    ImGui::TextColored(color, "%s", line);
    ImGui::PopTextWrapPos();
    return allowed;
}

void Designer::drawOutcome(const std::string& english) {
    const float scale = ImGui::GetFontSize() / 15.0f;
    const bool refused = english.rfind("Not ", 0) == 0 || english.rfind("The instrument did not", 0) == 0 || english.rfind("There is no layout", 0) == 0;
    const bool done = english.rfind("Written and read back", 0) == 0 || english.rfind("Erased:", 0) == 0;
    std::string shown = joue::i18n::message(english);
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440.0f * scale);
    std::size_t start = 0;
    bool first = true;
    while (start <= shown.size()) {
        std::size_t end = shown.find('\n', start);
        std::string part = shown.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (first && refused) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", part.c_str());
        else if (first && done) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), "%s", part.c_str());
        else ImGui::TextWrapped("%s", part.c_str());
        first = false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    ImGui::PopTextWrapPos();
}

void Designer::drawTagWindow() {
    if (!ImGui::BeginPopupModal(tr("Write the layout to a blank tag##writetag"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(tr("Writes this layout to a blank NTAG216 tag."));
    ImGui::BulletText("%s", tr("Lay the tag flat on the slot chosen below."));
    ImGui::BulletText("%s", tr("Take the other modules out of the other slots."));
    ImGui::BulletText("%s", tr("The tag must be blank."));
    ImGui::TextDisabled("%s", tr("Not tried on a real tag yet: try one first."));
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Slot"));
    for (int slot = 1; slot <= 3; ++slot) {
        ImGui::SameLine();
        if (ImGui::RadioButton(std::to_string(slot).c_str(), tagSlot_ == slot)) tagSlot_ = slot;
    }
    bool slotAllows = drawSlotState(tagSlot_, false);
    ImGui::Checkbox(tr("A blank tag is in this slot, and no module"), &tagConfirmed_);
    bool bad = false;
    for (const std::string& line : design_.problems()) bad = bad || line.rfind("Error", 0) == 0;
    bool ready = canWriteTag && canWriteTag();
    ImGui::BeginDisabled(bad || !tagConfirmed_ || !ready || !slotAllows);
    if (ImGui::Button(tr("Write the tag"))) {
        if (writeTag) writeTag(tagSlot_, design_.toBytes());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
    if (bad) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", tr("The layout has an error (see Check)."));
    if (loadStatus && !loadStatus().empty()) drawOutcome(loadStatus());
    ImGui::EndPopup();
}

// The window of "Erase a tag...": a tag with a custom layout is blanked; a manufacturer's layout never is.
void Designer::drawEraseWindow() {
    if (!ImGui::BeginPopupModal(tr("Erase a tag##erasetag"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const ImVec4 red(1.0f, 0.35f, 0.3f, 1.0f);
    ImGui::TextColored(red, "%s", tr("This erases the tag in the chosen slot for good."));
    ImGui::TextColored(red, "%s", tr("A manufacturer layout is never erased."));
    ImGui::Spacing();
    ImGui::BulletText("%s", tr("Take the other modules out of the other slots."));
    ImGui::BulletText("%s", tr("The tag is read first; nothing is erased if it is doubtful."));
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Slot"));
    for (int slot = 1; slot <= 3; ++slot) {
        ImGui::SameLine();
        if (ImGui::RadioButton((std::to_string(slot) + "##erase").c_str(), eraseSlot_ == slot)) eraseSlot_ = slot;
    }
    bool slotAllows = drawSlotState(eraseSlot_, true);
    ImGui::Checkbox(tr("Erase the tag in this slot"), &eraseConfirmed_);
    bool ready = canWriteTag && canWriteTag();
    ImGui::BeginDisabled(!eraseConfirmed_ || !ready || !slotAllows);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.12f, 1.0f));
    if (ImGui::Button(tr("Erase the tag"))) {
        if (eraseTag) eraseTag(eraseSlot_);
        eraseConfirmed_ = false;
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
    if (loadStatus && !loadStatus().empty()) drawOutcome(loadStatus());
    ImGui::EndPopup();
}

void Designer::drawInspector() {
    float scale = ImGui::GetFontSize() / 15.0f;
    joue::InstrumentHeader& header = design_.blob().header;

    ImGui::SeparatorText(tr("Tools"));
    const float toolsLimit = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int t = 0; t < 7; ++t) {
        if (t) {
            float width = ImGui::CalcTextSize(tr(ToolNames[t])).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= toolsLimit) ImGui::SameLine();
        }
        bool active = tool_ == t;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.45f, 0.65f, 1.0f));
        if (ImGui::Button(tr(ToolNames[t]))) tool_ = t;
        if (active) ImGui::PopStyleColor();
    }
    if (tool_ == 0) {
        ImGui::TextDisabled("%s", tr("Click picks. Drag moves."));
        if (ImGui::TreeNode(tr("Mouse and keys"))) {
            ImGui::TextDisabled("%s", tr("Ctrl-click adds or removes a pick."));
            ImGui::TextDisabled("%s", tr("Ctrl-drag picks what the pointer crosses."));
            ImGui::TextDisabled("%s", tr("Shift-drag moves; on an edge or corner it stretches."));
            ImGui::TextDisabled("%s", tr("Shift-drag several picked: they move together."));
            ImGui::TextDisabled("%s", tr("Shift-drag onto another and let go: swap."));
            ImGui::TextDisabled("%s", tr("Alt-drag draws a new element."));
            ImGui::TextDisabled("%s", tr("Delete, Ctrl+D, Ctrl+C / V, arrows, Ctrl+A, Esc."));
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("%s", tr("Drag on the surface to draw it."));
    }
    ImGui::SetNextItemWidth(70 * scale);
    if (ImGui::BeginCombo(tr("Snap"), (std::to_string(snap_) + " mm").c_str())) {
        for (int s : {1, 2, 5, 10}) if (ImGui::Selectable((std::to_string(s) + " mm").c_str(), snap_ == s)) snap_ = s;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("Grid"), &grid_);

    ImGui::SeparatorText(tr("Module"));
    int slots = design_.slots();
    ImGui::SetNextItemWidth(70 * scale);
    if (ImGui::InputInt(tr("Slots"), &slots, 1, 1)) {
        pushUndo();
        design_.setSlots(slots);
    }
    ImGui::SetNextItemWidth(190 * scale);
    if (ImGui::BeginCombo(tr("Type"), joue::moduleName(header.type).c_str())) {
        for (int t : KnownTypes) {
            if (ImGui::Selectable(joue::moduleName(t).c_str(), header.type == t)) {
                pushUndo();
                header.type = static_cast<std::uint8_t>(t);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(70 * scale);
    int colorId = header.color;
    if (ImGui::InputInt(tr("Colour"), &colorId, 1, 1)) {
        colorId = std::max(1, std::min(19, colorId));
        pushUndo();
        header.color = static_cast<std::uint8_t>(colorId);
    }
    ImGui::SameLine();
    joue::Rgb rgb = joue::moduleColor(header.color);
    ImGui::ColorButton("##swatch", ImVec4(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(18 * scale, 18 * scale));
    int revision = header.revision, byteSix = header.reserved0;
    ImGui::SetNextItemWidth(70 * scale);
    if (ImGui::InputInt(tr("Revision"), &revision, 0, 0)) header.revision = static_cast<std::uint8_t>(std::max(0, std::min(255, revision)));
    if (ImGui::IsItemActivated()) pushUndo();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * scale);
    if (ImGui::InputInt(tr("Byte 6"), &byteSix, 0, 0)) header.reserved0 = static_cast<std::uint8_t>(std::max(0, std::min(255, byteSix)));
    if (ImGui::IsItemActivated()) pushUndo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Not understood yet. Read from real modules: 0 on Pads and the Grand Fretboard, 1 on the Scaler and the\nGrand Clavier, 3 in the official Play module blobs. The Windows editor keeps whatever the module had."));
    bool mpe = (header.options & 0x01) != 0;
    if (ImGui::Checkbox(tr("MPE"), &mpe)) {
        pushUndo();
        header.options = static_cast<std::uint8_t>(mpe ? (header.options | 0x01) : (header.options & ~0x01));
    }
    ImGui::BeginDisabled(!mpe);
    {
        static const char* zones[3] = {"1-16", "1-8", "9-16"};
        int zone = design_.mpeChannels();
        ImGui::SetNextItemWidth(110 * scale);
        if (ImGui::BeginCombo(tr("MPE Channels"), zones[zone])) {
            for (int z = 0; z < 3; ++z) {
                if (ImGui::Selectable(zones[z], zone == z)) {
                    pushUndo();
                    design_.setMpeChannels(z);
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndDisabled();
    {
        const char* names[3] = {tr("Low"), tr("High"), tr("Custom")};
        joue::PressureRange all = design_.pressureRangeOfAll();
        ImGui::SetNextItemWidth(110 * scale);
        if (ImGui::BeginCombo(tr("Pressure range"), names[static_cast<int>(all)])) {
            for (int r = 0; r < 2; ++r) {
                if (ImGui::Selectable(names[r], static_cast<int>(all) == r)) {
                    pushUndo();
                    design_.setPressureRangeOfAll(static_cast<joue::PressureRange>(r));
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sets the pressure range of every pad, key, string and area."));
    }
    {
        const char* names[3] = {tr("Off"), tr("Channel"), tr("Polyphonic")};
        int type = static_cast<int>(design_.aftertouch());
        ImGui::SetNextItemWidth(110 * scale);
        if (ImGui::BeginCombo(tr("Aftertouch"), names[type])) {
            for (int a = 0; a < 3; ++a) {
                if (ImGui::Selectable(names[a], type == a)) {
                    pushUndo();
                    design_.setAftertouch(static_cast<joue::AftertouchType>(a));
                    scriptLoaded_ = false;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Written in the start-up script, for the elements it names."));
    }
    {
        bool vibrato = design_.vibrato();
        if (ImGui::Checkbox(tr("Vibrato"), &vibrato)) {
            pushUndo();
            design_.setVibrato(vibrato);
            scriptLoaded_ = false;
        }
        int value = design_.hasVibratoValue() ? design_.vibratoValue() : 1024;
        ImGui::SetNextItemWidth(180 * scale);
        if (ImGui::SliderInt(tr("Vibrato value"), &value, 0, 8192)) {
            design_.setVibratoValue(value);
            scriptLoaded_ = false;
        }
        if (ImGui::IsItemActivated()) pushUndo();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Written in the start-up script, with the target of the other vibrato command, or for every element."));
    }
    {
        bool hasGlide = design_.hasGlide();
        bool glissando = design_.glissando();
        ImGui::BeginDisabled(!hasGlide);
        if (ImGui::Checkbox(tr("Glissando"), &glissando)) {
            pushUndo();
            design_.setGlissando(glissando);
            scriptLoaded_ = false;
        }
        static const int ranges[9] = {2, 3, 4, 5, 7, 12, 24, 48, 96};
        int range = design_.glissandoRange();
        std::string current = joue::i18n::text(std::to_string(range) + " semitones");
        ImGui::SetNextItemWidth(150 * scale);
        if (ImGui::BeginCombo(tr("Glissando range"), hasGlide ? current.c_str() : "")) {
            for (int r : ranges) {
                if (ImGui::Selectable(joue::i18n::text(std::to_string(r) + " semitones").c_str(), r == range)) {
                    pushUndo();
                    design_.setGlissandoRange(r);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        const float glideLimit = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(hasGlide ? tr("Update the glissando keys") : tr("Add glissando"))) {
            pushUndo();
            design_.addGlide(range > 0 ? range : 24);
            scriptLoaded_ = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Glissando needs a Glide element, which covers the pads and keys. Add glissando makes it; update it after adding keys."));
        if (hasGlide) {
            float width = ImGui::CalcTextSize(tr("Remove glissando")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= glideLimit) ImGui::SameLine();
            if (ImGui::Button(tr("Remove glissando"))) {
                pushUndo();
                design_.removeGlide();
                scriptLoaded_ = false;
            }
        }
    }

    ImGui::SeparatorText(tr("Generate"));
    if (ImGui::Button(tr("Pad grid..."))) ImGui::OpenPopup("padgrid");
    ImGui::SameLine();
    if (ImGui::Button(tr("Keyboard..."))) ImGui::OpenPopup("keyboard");
    if (ImGui::Button(tr("Isomorphic grid..."))) ImGui::OpenPopup("isogrid");
    ImGui::SameLine();
    if (ImGui::Button(tr("Fretboard..."))) ImGui::OpenPopup("fretboard");
    if (ImGui::BeginPopup("padgrid")) {
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Rows"), &gridRows_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Columns"), &gridColumns_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Gap (mm)"), &gridGap_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("First note"), &gridFirstNote_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Row step"), &gridRowStep_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Channel"), &gridChannel_);
        if (ImGui::Button(tr("Add"))) {
            pushUndo();
            design_.addPadGrid({4, 4, design_.widthMm() - 8, joue::Design::HeightMm - 8}, gridRows_, gridColumns_, gridGap_, gridFirstNote_, gridRowStep_, gridChannel_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("keyboard")) {
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("White keys"), &keyboardWhite_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("First note"), &keyboardFirst_);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", joue::noteName(keyboardFirst_).c_str());
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Channel"), &keyboardChannel_);
        if (ImGui::Button(tr("Add"))) {
            pushUndo();
            design_.addKeyboard({2, 4, design_.widthMm() - 4, joue::Design::HeightMm - 8}, keyboardFirst_, keyboardWhite_, keyboardChannel_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("isogrid")) {
        struct Preset { const char* name; int column, row; };
        static const Preset presets[] = {{"Fourths (like a Linnstrument)", 1, 5}, {"Fifths", 1, 7}, {"Major thirds", 1, 4}, {"Minor thirds", 1, 3}, {"Sequential (a row after the other)", 1, 0}, {"Custom", 0, 0}};
        ImGui::SetNextItemWidth(220 * scale);
        if (ImGui::BeginCombo(tr("Layout"), tr(presets[isoPreset_].name))) {
            for (int p = 0; p < 6; ++p) {
                if (ImGui::Selectable(tr(presets[p].name), isoPreset_ == p)) {
                    isoPreset_ = p;
                    if (p < 5) {
                        isoColumnInterval_ = presets[p].column;
                        isoRowInterval_ = p == 4 ? isoColumns_ : presets[p].row;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Rows"), &isoRows_);
        ImGui::SetNextItemWidth(90 * scale);
        if (ImGui::InputInt(tr("Columns"), &isoColumns_) && isoPreset_ == 4) isoRowInterval_ = isoColumns_;
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Gap (mm)"), &isoGap_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("First note"), &isoFirstNote_);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", joue::noteName(isoFirstNote_).c_str());
        if (isoPreset_ == 5) {
            ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Along a row"), &isoColumnInterval_);
            ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Up a row"), &isoRowInterval_);
        } else {
            ImGui::TextDisabled(tr("Along a row +%d, up a row +%d"), isoColumnInterval_, isoRowInterval_);
        }
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Channel"), &isoChannel_);
        ImGui::Checkbox(tr("Stagger the rows"), &isoStagger_);
        if (ImGui::Button(tr("Add"))) {
            pushUndo();
            std::size_t before = design_.count();
            design_.addIsomorphicGrid({4, 4, design_.widthMm() - 8, joue::Design::HeightMm - 8}, isoRows_, isoColumns_, isoGap_, isoFirstNote_, isoColumnInterval_, isoRowInterval_, isoChannel_, isoStagger_);
            if (design_.count() == before) { status_ = "The pads would be too small."; undo_.pop_back(); }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("fretboard")) {
        struct Tuning { const char* name; std::vector<int> notes; };
        static const std::vector<Tuning> tunings = {{"Guitar (E A D G B E)", {40, 45, 50, 55, 59, 64}}, {"Bass, 4 strings (E A D G)", {28, 33, 38, 43}}, {"Violin (G D A E)", {55, 62, 69, 76}}, {"Fourths (as the Grand Fretboard)", {36, 41, 46, 51, 55, 60}}, {"Custom", {}}};
        ImGui::SetNextItemWidth(240 * scale);
        if (ImGui::BeginCombo(tr("Tuning"), tr(tunings[static_cast<std::size_t>(fretPreset_)].name))) {
            for (int t = 0; t < static_cast<int>(tunings.size()); ++t) {
                if (ImGui::Selectable(tr(tunings[static_cast<std::size_t>(t)].name), fretPreset_ == t)) fretPreset_ = t;
            }
            ImGui::EndCombo();
        }
        std::vector<int> notes = tunings[static_cast<std::size_t>(fretPreset_)].notes;
        if (notes.empty()) {
            ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Strings"), &fretStrings_);
            ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Lowest note"), &fretFirstNote_);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", joue::noteName(fretFirstNote_).c_str());
            ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Between strings"), &fretInterval_);
            for (int k = 0; k < std::max(1, std::min(12, fretStrings_)); ++k) notes.push_back(fretFirstNote_ + k * fretInterval_);
        } else {
            std::string line = tr("Open strings:");
            for (int n : notes) line += " " + joue::noteName(n);
            ImGui::TextDisabled("%s", line.c_str());
        }
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Frets"), &fretFrets_);
        ImGui::SetNextItemWidth(90 * scale); ImGui::InputInt(tr("Channel"), &fretChannel_);
        if (ImGui::Button(tr("Add"))) {
            pushUndo();
            std::size_t before = design_.count();
            int right = design_.widthMm() - 26;
            design_.addFretboard({3, 8, std::max(10, right - 3), 74}, notes, fretFrets_, 2, fretChannel_);
            if (design_.count() == before) { status_ = "The strings would be too thin."; undo_.pop_back(); }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawArrange();
    ImGui::SeparatorText(tr("Selected element"));
    if (picked_.size() > 1) ImGui::TextDisabled(tr("%d elements are picked; what follows is the last one picked."), static_cast<int>(picked_.size()));
    if (selected_ >= 0 && design_.hasBox(static_cast<std::size_t>(selected_))) {
        std::size_t index = static_cast<std::size_t>(selected_);
        joue::ElementHeader h = design_.header(index);
        ImGui::Text(tr("%s %d (version %d)"), joue::i18n::text(joue::elementName(h.type)).c_str(), h.id, h.version);
        Box b = design_.box(index);
        int xywh[4] = {b.x, b.y, b.w, b.h};
        const char* labels[4] = {tr("X"), tr("Y"), tr("Width"), tr("Height")};
        bool moved = false;
        for (int k = 0; k < 4; ++k) {
            ImGui::SetNextItemWidth(70 * scale);
            if (ImGui::InputInt(labels[k], &xywh[k], 0, 0)) moved = true;
            if (ImGui::IsItemActivated()) pushUndo();
            if (k % 2 == 0) ImGui::SameLine();
        }
        if (moved) design_.setBox(index, {xywh[0], xywh[1], xywh[2], xywh[3]});
        int layer = h.layer, z = h.z, depth = h.depth, options = h.options;
        bool changed = false;
        ImGui::SetNextItemWidth(70 * scale); changed |= ImGui::InputInt(tr("Layer"), &layer, 0, 0); if (ImGui::IsItemActivated()) pushUndo(); ImGui::SameLine();
        ImGui::SetNextItemWidth(70 * scale); changed |= ImGui::InputInt(tr("Z"), &z, 0, 0); if (ImGui::IsItemActivated()) pushUndo();
        ImGui::SetNextItemWidth(70 * scale); changed |= ImGui::InputInt(tr("Depth"), &depth, 0, 0); if (ImGui::IsItemActivated()) pushUndo(); ImGui::SameLine();
        ImGui::SetNextItemWidth(90 * scale); changed |= ImGui::InputInt(tr("Options"), &options, 0, 0, ImGuiInputTextFlags_CharsHexadecimal); if (ImGui::IsItemActivated()) pushUndo();
        if (changed) {
            h.layer = static_cast<std::uint8_t>(std::max(0, std::min(255, layer)));
            h.z = static_cast<std::uint8_t>(std::max(0, std::min(255, z)));
            h.depth = static_cast<std::uint8_t>(std::max(0, std::min(255, depth)));
            h.options = static_cast<std::uint16_t>(std::max(0, std::min(65535, options)));
            design_.setHeader(index, h);
        }
        if (design_.hasButton(index)) {
            joue::ButtonSetting setting = design_.buttonSetting(index);
            static const joue::ButtonAction actions[12] = {joue::ButtonAction::Vibrato, joue::ButtonAction::ChannelSelection, joue::ButtonAction::Sustain, joue::ButtonAction::Bending, joue::ButtonAction::YControl, joue::ButtonAction::AftertouchChannel, joue::ButtonAction::AftertouchPolyphonic, joue::ButtonAction::Glissando, joue::ButtonAction::Fretless, joue::ButtonAction::OctaveDown, joue::ButtonAction::OctaveUp, joue::ButtonAction::Custom};
            const char* actionNames[12] = {tr("Vibrato on/off"), tr("Channel selection"), tr("Sustain on/off"), tr("Bending on/off"), tr("YControl on/off"), tr("Aftertouch on/off"), tr("Aftertouch on/off (polyphonic)"), tr("Glissando on/off"), tr("Fretless on/off"), tr("Octave down"), tr("Octave up"), tr("Custom command")};
            int current = 11;
            for (int a = 0; a < 12; ++a) {
                if (actions[a] == setting.action) current = a;
            }
            ImGui::SetNextItemWidth(230 * scale);
            if (ImGui::BeginCombo(tr("Action"), actionNames[current])) {
                for (int a = 0; a < 12; ++a) {
                    if (ImGui::Selectable(actionNames[a], a == current)) {
                        pushUndo();
                        joue::ButtonSetting next = setting;
                        next.action = actions[a];
                        if (next.action == joue::ButtonAction::Custom) next.command = design_.buttonCommand(index);
                        design_.setButtonSetting(index, next);
                    }
                }
                ImGui::EndCombo();
            }
            if (setting.action != joue::ButtonAction::Custom) {
                char target[64];
                std::snprintf(target, sizeof target, "%s", setting.target.c_str());
                ImGui::SetNextItemWidth(110 * scale);
                if (ImGui::InputText(tr("Target"), target, sizeof target)) {
                    joue::ButtonSetting next = setting;
                    next.target = target;
                    design_.setButtonSetting(index, next);
                }
                if (ImGui::IsItemActivated()) pushUndo();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("*: every element. l3: the elements of layer 3. l{2,3}: layers 2 and 3. c6: the strings, c2: the pads, c5: the keys."));
                if (setting.action == joue::ButtonAction::ChannelSelection) {
                    int channel = setting.value;
                    ImGui::SetNextItemWidth(90 * scale);
                    if (ImGui::InputInt(tr("Channel"), &channel, 1, 4)) {
                        joue::ButtonSetting next = setting;
                        next.value = std::max(1, std::min(16, channel));
                        design_.setButtonSetting(index, next);
                    }
                    if (ImGui::IsItemActivated()) pushUndo();
                }
            } else {
                char command[256];
                std::snprintf(command, sizeof command, "%s", design_.buttonCommand(index).c_str());
                ImGui::SetNextItemWidth(230 * scale);
                if (ImGui::InputText(tr("Command"), command, sizeof command)) design_.setButtonCommand(index, command);
                if (ImGui::IsItemActivated()) pushUndo();
            }
            ImGui::TextDisabled("%s %s", tr("Command:"), design_.buttonCommand(index).c_str());
        }
        if (design_.hasPressureRange(index)) {
            const char* names[3] = {tr("Low"), tr("High"), tr("Custom")};
            joue::PressureRange range = design_.pressureRange(index);
            ImGui::SetNextItemWidth(110 * scale);
            if (ImGui::BeginCombo(tr("Pressure range"), names[static_cast<int>(range)])) {
                for (int r = 0; r < 2; ++r) {
                    if (ImGui::Selectable(names[r], static_cast<int>(range) == r)) {
                        pushUndo();
                        design_.setPressureRange(index, static_cast<joue::PressureRange>(r));
                    }
                }
                ImGui::EndCombo();
            }
        }
        const bool yControlShown = design_.hasYControl(index);
        if (yControlShown) {
            joue::YControl y = design_.yControl(index);
            bool edited = ImGui::Checkbox(tr("Y Control"), &y.enabled);
            if (y.enabled) {
                ImGui::SetNextItemWidth(90 * scale);
                edited |= ImGui::InputInt(tr("CC number"), &y.cc, 1, 10);
                ImGui::SetNextItemWidth(60 * scale);
                edited |= ImGui::InputInt("##ylow", &y.low, 0, 0);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60 * scale);
                edited |= ImGui::InputInt(tr("Range"), &y.high, 0, 0);
                const char* directions[2] = {tr("Natural"), tr("Inverted")};
                ImGui::SetNextItemWidth(110 * scale);
                if (ImGui::BeginCombo(tr("Direction"), directions[y.inverted ? 1 : 0])) {
                    for (int d = 0; d < 2; ++d) {
                        if (ImGui::Selectable(directions[d], (d == 1) == y.inverted)) {
                            y.inverted = d == 1;
                            edited = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                const char* modes[2] = {tr("Absolute"), tr("Relative")};
                ImGui::SetNextItemWidth(110 * scale);
                if (ImGui::BeginCombo(tr("Mode"), modes[y.relative ? 1 : 0])) {
                    for (int m = 0; m < 2; ++m) {
                        if (ImGui::Selectable(modes[m], (m == 1) == y.relative)) {
                            y.relative = m == 1;
                            edited = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            if (edited) {
                pushUndo();
                design_.setYControl(index, y);
            }
        }
        for (const joue::FieldSpec& f : joue::bodyFields(h.type, h.version)) {
            if (yControlShown && (std::string(f.name) == "Y min" || std::string(f.name) == "Y max" || std::string(f.name) == "Y control (CC)")) continue;
            int value = design_.field(index, f);
            ImGui::SetNextItemWidth(90 * scale);
            if (ImGui::InputInt(joue::i18n::text(f.name).c_str(), &value, 1, 10)) design_.setField(index, f, value);
            if (ImGui::IsItemActivated()) pushUndo();
            if (std::string(f.name) == "Note" || std::string(f.name) == "Note ") {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", joue::noteName(value).c_str());
            }
        }
        if (joue::bodyFields(h.type, h.version).empty() && !design_.hasButton(index)) ImGui::TextDisabled("%s", tr("The fields of this record are kept as they are."));
        if (ImGui::Button(tr("Duplicate"))) {
            pushUndo();
            std::size_t copy = design_.duplicate(index);
            if (copy < design_.count()) select(static_cast<int>(copy));
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete"))) {
            pushUndo();
            design_.remove(index);
            clearPicks();
            scriptLoaded_ = false;
        }
    } else {
        ImGui::TextDisabled("%s", tr("Nothing selected."));
    }

    ImGui::SeparatorText(tr("Script"));
    if (!scriptLoaded_) {
        scriptText_ = design_.script();
        scriptLoaded_ = true;
    }
    bool hasScript = design_.hasScript();
    if (ImGui::Checkbox(tr("Start-up script"), &hasScript)) {
        pushUndo();
        if (hasScript) design_.setScript(scriptText_); else design_.removeScript();
    }
    if (design_.hasScript()) {
        if (ImGui::InputText(tr("##script"), &scriptText_)) design_.setScript(scriptText_);
        if (ImGui::IsItemActivated()) pushUndo();
        ImGui::TextDisabled("%s", tr("Official examples: *.5=1024;  l3.5=1024;l3.2=1;c14.4=0;"));
    }

    ImGui::SeparatorText(tr("Check"));
    joue::Bytes bytes = design_.toBytes();
    ImGui::Text(tr("%zu elements, %zu bytes"), design_.count(), bytes.size());
    for (const std::string& line : design_.problems()) {
        bool error = line.rfind("Error", 0) == 0;
        ImGui::PushStyleColor(ImGuiCol_Text, error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.95f, 0.8f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", joue::i18n::message(line).c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::TreeNode(tr("Decoded blob"))) {
        for (const std::string& line : joue::describe(bytes)) ImGui::TextWrapped("%s", line.c_str());
        ImGui::TreePop();
    }
}
