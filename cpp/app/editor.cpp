#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include "editor.hpp"

#include "about.hpp"
#include "fonts.hpp"
#include "logo.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_stdlib.h"
#include "joue/blob.hpp"
#include "joue/device.hpp"
#include "joue/blob.hpp"
#include "joue/moduleart.hpp"
#include "joue/settingsmeta.hpp"
#include "joue/compare.hpp"
#include "joue/i18n.hpp"
#include "joue/update.hpp"

using joue::i18n::tr;

namespace fs = std::filesystem;
using joue::Node;

namespace {

std::vector<Node*> kids(Node& node, const std::string& tag, const char* name = nullptr) {
    std::vector<Node*> out;
    for (auto& child : node.children) {
        if (child->tag == tag && (!name || child->name() == name)) out.push_back(child.get());
    }
    return out;
}

Node* kid(Node& node, const std::string& tag, const char* name) {
    auto found = kids(node, tag, name);
    return found.empty() ? nullptr : found.front();
}

std::string stringValue(Node& node, const char* name) {
    Node* entry = kid(node, "String", name);
    if (!entry) return {};
    const std::string* value = entry->attr("value");
    return value ? *value : std::string();
}

bool flag(Node& node, const char* name) { return stringValue(node, name) == "1"; }

void setString(Node& node, const char* name, const std::string& value) {
    Node* entry = kid(node, "String", name);
    if (!entry) {
        auto created = std::make_unique<Node>();
        created->tag = "String";
        created->attrs = {{"name", name}, {"value", value}};
        node.children.push_back(std::move(created));
        return;
    }
    entry->attrRef("value") = value;
}

std::vector<std::unique_ptr<Node>>& entriesOf(Node& owner) {
    if (owner.tag == "Array") return owner.children;
    Node* entries = kid(owner, "Array", "Entries");
    if (!entries) {
        auto created = std::make_unique<Node>();
        created->tag = "Array";
        created->attrs = {{"name", "Entries"}};
        owner.children.push_back(std::move(created));
        entries = owner.children.back().get();
    }
    return entries->children;
}

Node* moduleList(Node& root) { return kid(root, "Array", "ModuleList"); }

struct Location {
    std::vector<std::unique_ptr<Node>>* list = nullptr;
    std::size_t index = 0;
};

std::optional<Location> locate(Node& owner, const Node* target) {
    auto& list = entriesOf(owner);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].get() == target) return Location{&list, i};
    }
    for (auto& node : list) {
        if (node->tag == "Folder") {
            if (auto found = locate(*node, target)) return found;
        }
    }
    return std::nullopt;
}

void walk(Node& node, const std::function<void(Node&)>& visit) {
    visit(node);
    for (auto& child : node.children) walk(*child, visit);
}

void unlock(Node& node) {
    walk(node, [](Node& n) {
        if ((n.tag == "String" || n.tag == "Setting") && n.name() == "ReadOnly") n.attrRef("value") = "0";
    });
}

bool parseRangeD(const std::string& text, double& low, double& high, bool& decimals) {
    std::size_t semicolon = text.find(';');
    if (semicolon == std::string::npos) return false;
    try {
        std::size_t used = 0;
        std::string a = text.substr(0, semicolon), b = text.substr(semicolon + 1);
        low = std::stod(a, &used);
        if (used != a.size()) return false;
        high = std::stod(b, &used);
        if (used != b.size()) return false;
        decimals = a.find('.') != std::string::npos || b.find('.') != std::string::npos;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseRange(const std::string& text, int& low, int& high) {
    std::size_t semicolon = text.find(';');
    if (semicolon == std::string::npos) return false;
    try {
        std::size_t used = 0;
        low = std::stoi(text.substr(0, semicolon), &used);
        if (used != semicolon) return false;
        std::string rest = text.substr(semicolon + 1);
        high = std::stoi(rest, &used);
        return used == rest.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<int> pathTo(Node& owner, const Node* target) {
    std::vector<int> path;
    std::function<bool(Node&)> search = [&](Node& node) {
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            path.push_back(static_cast<int>(i));
            if (node.children[i].get() == target || search(*node.children[i])) return true;
            path.pop_back();
        }
        return false;
    };
    if (!target || !search(owner)) path.clear();
    return path;
}

Node* nodeAt(Node& root, const std::vector<int>& path) {
    if (path.empty()) return nullptr;
    Node* node = &root;
    for (int index : path) {
        if (index < 0 || static_cast<std::size_t>(index) >= node->children.size()) return nullptr;
        node = node->children[static_cast<std::size_t>(index)].get();
    }
    return node;
}

int intValue(Node& node, const char* name) {
    try {
        return std::stoi(stringValue(node, name));
    } catch (const std::exception&) {
        return 0;
    }
}

// A shape belongs to a selected element when the names match, or when the selection is a group such as ALL_KEYS.
bool belongsTo(const std::string& shapeName, const std::string& selected) {
    if (shapeName.empty() || selected.empty()) return false;
    if (shapeName == selected) return true;
    if (selected.rfind("ALL_", 0) == 0 && selected.size() > 5 && selected.back() == 'S') {
        std::string prefix = selected.substr(4, selected.size() - 5);
        return shapeName.rfind(prefix, 0) == 0;
    }
    return false;
}

std::vector<ImVec2> shapePoints(const joue::ModuleArt& art, const joue::Shape& shape, ImVec2 origin, float pixelsPerMm, bool mirror) {
    std::vector<ImVec2> points;
    for (std::size_t i = 0; i + 1 < shape.points.size(); i += 2) {
        float y = mirror ? art.heightMm - shape.points[i + 1] : shape.points[i + 1];
        points.push_back(origin + ImVec2(shape.points[i], y) * pixelsPerMm);
    }
    return points;
}

bool insidePolygon(const std::vector<ImVec2>& poly, ImVec2 p) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) && (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) inside = !inside;
    }
    return inside;
}

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab = b - a, ap = p - a;
    float length2 = ab.x * ab.x + ab.y * ab.y;
    float t = length2 > 0 ? std::max(0.0f, std::min(1.0f, (ap.x * ab.x + ap.y * ab.y) / length2)) : 0.0f;
    ImVec2 d = a + ab * t - p;
    return std::sqrt(d.x * d.x + d.y * d.y);
}

// The topmost named shape under the mouse, or -1.
int hitShape(const joue::ModuleArt& art, ImVec2 origin, float pixelsPerMm, bool mirror, ImVec2 mouse) {
    for (int i = static_cast<int>(art.shapes.size()) - 1; i >= 0; --i) {
        const joue::Shape& shape = art.shapes[static_cast<std::size_t>(i)];
        if (shape.element.empty()) continue;
        if (shape.kind == joue::ShapeKind::Polygon) {
            auto points = shapePoints(art, shape, origin, pixelsPerMm, mirror);
            if (points.size() > 2 && insidePolygon(points, mouse)) return i;
        } else if (shape.kind == joue::ShapeKind::Polyline) {
            auto points = shapePoints(art, shape, origin, pixelsPerMm, mirror);
            float reach = std::max(4.0f, shape.width * pixelsPerMm);
            for (std::size_t k = 0; k + 1 < points.size(); ++k) {
                if (distanceToSegment(mouse, points[k], points[k + 1]) <= reach) return i;
            }
        } else {
            float y = mirror ? art.heightMm - shape.y - shape.h : shape.y;
            ImVec2 p0 = origin + ImVec2(shape.x, y) * pixelsPerMm;
            ImVec2 p1 = p0 + ImVec2(shape.w, shape.h) * pixelsPerMm;
            if (shape.kind == joue::ShapeKind::Ellipse) {
                ImVec2 c = (p0 + p1) * 0.5f, r = (p1 - p0) * 0.5f;
                float dx = (mouse.x - c.x) / r.x, dy = (mouse.y - c.y) / r.y;
                if (dx * dx + dy * dy <= 1.0f) return i;
            } else if (mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y) {
                return i;
            }
        }
    }
    return -1;
}

// Draws a module the way the original editors do: a dark outline, then every element in the module colour or dark.
// Shapes that belong to the selected or hovered element get a light outline.
void drawArt(ImDrawList* list, ImVec2 origin, float pixelsPerMm, int type, int colorId, bool mirror,
             const std::string& selected = std::string(), const std::string& hovered = std::string()) {
    joue::ModuleArt art = joue::moduleArt(type);
    joue::Rgb rgb = joue::moduleColor(colorId);
    ImU32 moduleColor = IM_COL32(rgb.r, rgb.g, rgb.b, 255);
    ImU32 dark = IM_COL32(51, 51, 51, 255);
    ImU32 outline = IM_COL32(29, 28, 33, 255);
    ImU32 selectColor = IM_COL32(15, 190, 255, 255);
    ImU32 hoverColor = IM_COL32(131, 239, 255, 200);
    ImVec2 size(art.widthMm * pixelsPerMm, art.heightMm * pixelsPerMm);
    list->AddRectFilled(origin, origin + size, outline, 4.5f * pixelsPerMm);
    if (!art.known) {
        ImVec2 inset(2.5f * pixelsPerMm, 2.5f * pixelsPerMm);
        list->AddRect(origin + inset, origin + size - inset, moduleColor, 3.0f * pixelsPerMm, 0, std::max(1.0f, 1.5f * pixelsPerMm));
        return;
    }
    auto drawOne = [&](const joue::Shape& shape, bool filled, ImU32 color, float thickness) {
        if (shape.kind == joue::ShapeKind::Polygon || shape.kind == joue::ShapeKind::Polyline) {
            std::vector<ImVec2> points = shapePoints(art, shape, origin, pixelsPerMm, mirror);
            if (points.size() < 2) return;
            if (shape.kind == joue::ShapeKind::Polygon) {
                if (filled) {
                    // The anti-aliased fill wants one winding order; mirroring flips it.
                    float area = 0.0f;
                    for (std::size_t i = 0; i < points.size(); ++i) {
                        const ImVec2& a = points[i];
                        const ImVec2& b = points[(i + 1) % points.size()];
                        area += a.x * b.y - b.x * a.y;
                    }
                    if (area < 0.0f) std::reverse(points.begin(), points.end());
                    list->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
                } else {
                    list->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_Closed, thickness);
                }
            } else {
                list->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, filled ? std::max(1.0f, shape.width * pixelsPerMm) : thickness + std::max(1.0f, shape.width * pixelsPerMm));
            }
            return;
        }
        float y = mirror ? art.heightMm - shape.y - shape.h : shape.y;
        ImVec2 p0 = origin + ImVec2(shape.x, y) * pixelsPerMm;
        ImVec2 p1 = p0 + ImVec2(shape.w, shape.h) * pixelsPerMm;
        if (shape.kind == joue::ShapeKind::RoundRect && filled) {
            // A rectangle thinner than a pixel and a half (the frets in a small icon) is drawn as exactly one pixel on the pixel grid:
            // left as it is, one that covers no pixel centre would not be drawn at all, and which one depends on where it falls.
            float pixel = 1.0f / std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
            auto onePixel = [&](float& a, float& b) {
                if (b - a < 1.5f * pixel) {
                    a = std::floor((a + b) * 0.5f / pixel) * pixel;
                    b = a + pixel;
                }
            };
            onePixel(p0.x, p1.x);
            onePixel(p0.y, p1.y);
        }
        if (shape.kind == joue::ShapeKind::RoundRect) {
            if (filled) list->AddRectFilled(p0, p1, color, shape.radius * pixelsPerMm);
            else list->AddRect(p0 - ImVec2(1, 1), p1 + ImVec2(1, 1), color, shape.radius * pixelsPerMm, 0, thickness);
        } else {
            if (filled) list->AddEllipseFilled((p0 + p1) * 0.5f, (p1 - p0) * 0.5f, color, 0.0f, 48);
            else list->AddEllipse((p0 + p1) * 0.5f, (p1 - p0) * 0.5f + ImVec2(1, 1), color, 0.0f, 48, thickness);
        }
    };
    for (const joue::Shape& shape : art.shapes) drawOne(shape, true, shape.fill == joue::ShapeFill::Module ? moduleColor : dark, 0.0f);
    for (const joue::Shape& shape : art.shapes) {
        if (belongsTo(shape.element, selected)) drawOne(shape, false, selectColor, 2.0f);
        else if (belongsTo(shape.element, hovered)) drawOne(shape, false, hoverColor, 1.5f);
    }
}

void drawLock(ImDrawList* list, ImVec2 origin, float size, ImU32 color) {
    float thickness = std::max(1.0f, size * 0.12f);
    ImVec2 center = origin + ImVec2(size * 0.5f, size * 0.42f);
    list->PathArcTo(center, size * 0.22f, 3.1415927f, 6.2831853f, 12);
    list->PathStroke(color, 0, thickness);
    list->AddLine(center + ImVec2(-size * 0.22f, 0), center + ImVec2(-size * 0.22f, size * 0.12f), color, thickness);
    list->AddLine(center + ImVec2(size * 0.22f, 0), center + ImVec2(size * 0.22f, size * 0.12f), color, thickness);
    list->AddRectFilled(origin + ImVec2(size * 0.2f, size * 0.5f), origin + ImVec2(size * 0.8f, size * 0.95f), color, size * 0.1f);
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char buffer[40];
    std::tm local = *std::localtime(&seconds);
    std::strftime(buffer, sizeof buffer, "%Y%m%d-%H%M%S", &local);
    char out[64];
    std::snprintf(out, sizeof out, "%s-%03d", buffer, static_cast<int>(millis));
    return out;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

#ifdef _WIN32
}  // namespace
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
namespace {

// Opens a file with the program the system uses for it (a PDF viewer for a PDF).
void openWithSystem(const std::string& path) { ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::string utf8(const wchar_t* text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

bool dialogsAvailable() { return true; }

std::optional<std::string> chooseOpenFile() {
    wchar_t file[4096] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFilter = L"Joue library (*.xml)\0*.xml\0All files\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = 4096;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return utf8(file);
}

std::optional<std::string> chooseSaveFile() {
    wchar_t file[4096] = L"library.xml";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFilter = L"Joue library (*.xml)\0*.xml\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = 4096;
    dialog.lpstrDefExt = L"xml";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return std::nullopt;
    return utf8(file);
}
#else
// Native file dialogs through the system's own helpers; returns nothing when cancelled or unavailable.
std::optional<std::string> runDialog(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return std::nullopt;
    std::string out;
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe)) out += buffer;
    int status = pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (status != 0 || out.empty()) return std::nullopt;
    return out;
}

// Opens a file with the program the system uses for it (a PDF viewer for a PDF); the path is put between single quotes for the shell.
void openWithSystem(const std::string& path) {
    std::string quoted = "'";
    for (char c : path) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
#ifdef __APPLE__
    std::string command = "open " + quoted + " >/dev/null 2>&1 &";
#else
    std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
#endif
    if (std::system(command.c_str()) != 0) {
    }
}

bool dialogsAvailable() {
#ifdef __APPLE__
    return true;
#else
    return std::system("command -v zenity >/dev/null 2>&1") == 0;
#endif
}

std::optional<std::string> chooseOpenFile() {
#ifdef __APPLE__
    return runDialog("osascript -e 'POSIX path of (choose file with prompt \"Open a Joue library (library.xml)\")' 2>/dev/null");
#else
    return runDialog("zenity --file-selection --title='Open a Joue library' 2>/dev/null");
#endif
}

std::optional<std::string> chooseSaveFile() {
#ifdef __APPLE__
    return runDialog("osascript -e 'POSIX path of (choose file name with prompt \"Export library as\" default name \"library.xml\")' 2>/dev/null");
#else
    return runDialog("zenity --file-selection --save --confirm-overwrite --filename=library.xml 2>/dev/null");
#endif
}
#endif

}  // namespace

std::string findPresets() {
    std::string base = executableDirectory();
    const char* relatives[] = {"/presets", "/../Resources/presets", "/../share/joue-revival/presets", "/../presets", "/../../presets", "/../../../presets"};
    for (const char* relative : relatives) {
        fs::path candidate = fs::path(base + relative) / "factoryLibrary.xml";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();
    }
    for (const char* directory : {"presets", "."}) {
        std::error_code ec;
        fs::path candidate = fs::path(directory) / "factoryLibrary.xml";
        if (fs::exists(candidate, ec)) return fs::absolute(candidate, ec).string();
    }
    return {};
}

Editor::Editor(EditorOptions options) : options_(std::move(options)) {
    fs::create_directories(options_.dataDir);
    libraryPath_ = (fs::path(options_.dataDir) / "library.xml").string();
    pendingElement_ = options_.element;
    if (dialogsAvailable()) {
        designer_.chooseOpen = [] { return chooseOpenFile(); };
        designer_.chooseSave = [] { return chooseSaveFile(); };
    }
    designer_.openExternally = [](const std::string& path) { openWithSystem(path); };
    designer_.canLoadTemporarily = [this] { return live_.open && live_.canLoadTemporarily(); };
    designer_.loadTemporarily = [this](const joue::Bytes& blob) { live_.requestTemporaryLoad(blob); };
    designer_.loadStatus = [this] { return live_.pushStatus(); };
    designer_.canWriteTag = [this] { return live_.open && live_.canWriteTag(); };
    designer_.writeTag = [this](int slot, const joue::Bytes& layout) { live_.requestWriteTag(slot, layout); };
    designer_.eraseTag = [this](int slot) { live_.requestEraseTag(slot); };
    designer_.slotModule = [this](int slot) {
        for (const LiveView::LiveModule& m : live_.modules()) {
            if (m.raw.empty()) continue;
            int span = std::max(1, joue::moduleSlots(m.raw[0]));
            if (m.slot <= slot && slot < m.slot + span) return std::make_pair(true, m.raw);
        }
        return std::make_pair(false, joue::Bytes());
    };
    live_.onEdit = [this](int slot, const joue::Bytes& blob) { openModuleFromInstrument(slot, blob); };
    aboutPopup_ = options_.about;
    live_.safeGraphics = options_.safeGraphics;
    loadUiSettings();
    if (options_.noTouches) live_.listenToPlay = false;
    live_.open = true;  // the live instrument is part of the window; Hide in it or View > Live instrument closes it
    if (!options_.designer.empty()) {
        designer_.open = true;
        if (options_.designer != "new") designer_.openFile(options_.designer);
        designer_.selectElement(options_.designerElement);
    }
    if (!options_.library.empty()) {
        openFromPath(options_.library);
        dirty_ = false;
    } else if (fs::exists(libraryPath_)) {
        try {
            loadRoot(joue::parseLibrary(readFile(libraryPath_)), "Loaded your saved library.");
        } catch (const std::exception& error) {
            setStatus(std::string(libraryPath_) + " could not be read: " + error.what(), true);
        }
    } else {
        std::string presets = findPresets();
        if (!presets.empty()) {
            try {
                loadRoot(joue::parseLibrary(readFile(presets)), "Factory presets loaded. Save keeps them, with your changes, in your own library.");
            } catch (const std::exception& error) {
                setStatus(presets + " could not be read: " + error.what(), true);
            }
        }
    }
    if (root_ && !options_.select.empty()) {
        walk(*root_, [&](Node& n) {
            if (n.tag == "Module" && n.name() == options_.select) selected_ = &n;
        });
    }
}

std::string Editor::title() const { return std::string(about::AppName) + (dirty_ ? " *" : ""); }

void Editor::setStatus(const std::string& message, bool error) {
    status_ = joue::i18n::message(message);
    statusError_ = error;
}

std::string Editor::snapshot() const { return joue::serializeLibrary(*root_); }

void Editor::rebuildObserved() {
    observed_.clear();
    if (!root_) return;
    walk(*root_, [&](Node& n) {
        if (n.tag != "Setting" || n.name().empty()) return;
        const std::string* value = n.attr("value");
        int low = 0, high = 0;
        if (!value || parseRange(*value, low, high)) return;
        observed_[n.name()].insert(*value);
    });
}

void Editor::loadRoot(std::unique_ptr<Node> root, const std::string& message) {
    root_ = std::move(root);
    joue::lockFactoryPresets(*root_);
    selected_ = nullptr;
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    pending_.reset();
    lastSnapshot_ = snapshotState();
    rebuildObserved();
    setStatus(message);
}

void Editor::openFromPath(const std::string& path) {
    try {
        loadRoot(joue::parseLibrary(readFile(path)), "Opened " + fs::path(path).filename().string() + ". Use Save to keep it in your library.");
        dirty_ = true;
    } catch (const std::exception& error) {
        setStatus(error.what(), true);
    }
}

// Every save keeps the previous file as a timestamped backup, so no version is ever lost.
bool Editor::saveLibrary() {
    if (!root_) return false;
    try {
        std::string xml = snapshot();
        fs::path target = libraryPath_;
        std::string backupNote;
        if (fs::exists(target)) {
            fs::path backups = target.parent_path() / "backups";
            fs::create_directories(backups);
            fs::copy_file(target, backups / ("library-" + timestamp() + ".xml"));
            std::vector<fs::path> old;
            for (const auto& entry : fs::directory_iterator(backups)) {
                if (entry.path().filename().string().rfind("library-", 0) == 0) old.push_back(entry.path());
            }
            std::sort(old.begin(), old.end(), std::greater<>());
            for (std::size_t i = 200; i < old.size(); ++i) fs::remove(old[i]);
            backupNote = " (previous version kept in backups)";
        }
        fs::path temporary = target;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary);
            out << xml;
            if (!out) throw std::runtime_error("Cannot write " + temporary.string());
        }
        fs::rename(temporary, target);
        dirty_ = false;
        setStatus("Saved to " + target.string() + backupNote);
        return true;
    } catch (const std::exception& error) {
        setStatus(error.what(), true);
        return false;
    }
}

void Editor::exportCopy(const std::string& path) {
    if (!root_) return;
    try {
        std::ofstream out(path, std::ios::binary);
        out << snapshot();
        if (!out) throw std::runtime_error("Cannot write " + path);
        setStatus("Exported a copy to " + path);
    } catch (const std::exception& error) {
        setStatus(error.what(), true);
    }
}

// ---- undo: snapshots of the serialized library; text edits are grouped until the field is released

void Editor::markChanged() {
    changedThisFrame_ = true;
    dirty_ = true;
}

Editor::State Editor::snapshotState() const {
    State state;
    state.xml = snapshot();
    if (root_ && selected_) state.selection = pathTo(*root_, selected_);
    return state;
}

void Editor::flushPendingUndo() {
    if (!pending_ || !root_) return;
    undo_.push_back(*pending_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
    pending_.reset();
    lastSnapshot_ = snapshotState();
}

void Editor::beginStructural() {
    flushPendingUndo();
    lastSnapshot_.selection = root_ && selected_ ? pathTo(*root_, selected_) : std::vector<int>();
}

void Editor::endStructural() {
    undo_.push_back(lastSnapshot_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
    lastSnapshot_ = snapshotState();
    dirty_ = true;
}

void Editor::restore(const State& state) {
    root_ = joue::parseLibrary(state.xml);
    selected_ = nodeAt(*root_, state.selection);
    rebuildObserved();
    lastSnapshot_ = state;
    dirty_ = true;
}

void Editor::undo() {
    flushPendingUndo();
    if (undo_.empty() || !root_) return;
    redo_.push_back(snapshotState());
    State previous = undo_.back();
    undo_.pop_back();
    restore(previous);
}

void Editor::redo() {
    flushPendingUndo();
    if (redo_.empty() || !root_) return;
    undo_.push_back(snapshotState());
    State next = redo_.back();
    redo_.pop_back();
    restore(next);
}

// ---- library actions

void Editor::newFolder() {
    beginStructural();
    auto folder = std::make_unique<Node>();
    folder->tag = "Folder";
    folder->attrs = {{"name", "New folder"}};
    setString(*folder, "IsExpanded", "1");
    Node* target = selected_ && selected_->tag == "Folder" ? selected_ : moduleList(*root_);
    Node* created = folder.get();
    entriesOf(*target).push_back(std::move(folder));
    entriesOf(*created);
    selected_ = created;
    endStructural();
}

void Editor::duplicateSelected() {
    auto where = selected_ ? locate(*moduleList(*root_), selected_) : std::nullopt;
    if (!where) return;
    beginStructural();
    auto copy = selected_->clone();
    copy->attrRef("name") = selected_->name() + " copy";
    unlock(*copy);
    Node* created = copy.get();
    where->list->insert(where->list->begin() + static_cast<long>(where->index) + 1, std::move(copy));
    selected_ = created;
    endStructural();
}

void Editor::moveSelected(int direction) {
    auto where = selected_ ? locate(*moduleList(*root_), selected_) : std::nullopt;
    if (!where) return;
    long target = static_cast<long>(where->index) + direction;
    if (target < 0 || target >= static_cast<long>(where->list->size())) return;
    beginStructural();
    std::swap((*where->list)[where->index], (*where->list)[static_cast<std::size_t>(target)]);
    endStructural();
}

void Editor::deleteSelected() {
    auto where = selected_ ? locate(*moduleList(*root_), selected_) : std::nullopt;
    if (!where) return;
    if (joue::containsLockedModule(*selected_)) {
        setStatus("Locked factory presets cannot be deleted.", true);
        return;
    }
    beginStructural();
    where->list->erase(where->list->begin() + static_cast<long>(where->index));
    selected_ = nullptr;
    endStructural();
}

// ---- drawing

namespace {

// A disabled control is drawn in plain grey, not in the dimmed blue of the theme: the colours of the control are replaced while it is
// disabled. Begin and end come in pairs, like ImGui::BeginDisabled, which they call.
std::vector<bool>& greyStack() {
    static std::vector<bool> stack;
    return stack;
}

void beginGrey(bool disabled) {
    ImGui::BeginDisabled(disabled);
    greyStack().push_back(disabled);
    if (!disabled) return;
    const ImVec4 frame(0.20f, 0.20f, 0.20f, 1.0f), text(0.50f, 0.50f, 0.50f, 1.0f), mark(0.45f, 0.45f, 0.45f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frame);
    ImGui::PushStyleColor(ImGuiCol_Button, frame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, frame);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, frame);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, mark);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, mark);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, mark);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.30f, 0.30f, 0.6f));
}

void endGrey() {
    bool disabled = !greyStack().empty() && greyStack().back();
    if (!greyStack().empty()) greyStack().pop_back();
    if (disabled) ImGui::PopStyleColor(11);
    ImGui::EndDisabled();
}

}  // namespace

bool Editor::editText(const char* id, std::string& value, bool disabled, float width) {
    beginGrey(disabled);
    if (width > 0) ImGui::SetNextItemWidth(width);
    bool changed = ImGui::InputText(id, &value);
    endGrey();
    if (changed) markChanged();
    return changed;
}

void Editor::draw() {
    applyTouchSelection();
    pollUpdateCheck();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) helpOpen_ = true;
    if (quitRequested_) {
        if (dirty_) quitPopup_ = true;
        else quit_ = true;
        quitRequested_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin(tr("root"), nullptr, flags);
    ImGui::PopStyleVar();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) saveLibrary();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) redo();
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z) && !ImGui::GetIO().WantTextInput) undo();

    drawMenuBar();

    float footer = ImGui::GetFrameHeightWithSpacing();
    float uiScale = ImGui::GetFontSize() / 15.0f;
    if (live_.open) {
        // the instrument sits in a panel above the columns; the strip under it is a handle to change its height
        if (liveHeight_ <= 0) liveHeight_ = 250.0f * uiScale;
        liveHeight_ = std::max(150.0f * uiScale, std::min(liveHeight_, ImGui::GetContentRegionAvail().y * 0.7f));
        live_.drawPanel(liveHeight_);
        ImVec2 handle = ImGui::GetCursorScreenPos();
        float handleWidth = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##instrumentsplit", ImVec2(handleWidth, 9.0f * uiScale));
        bool hot = ImGui::IsItemHovered(), dragging = ImGui::IsItemActive();
        if (dragging) liveHeight_ += ImGui::GetIO().MouseDelta.y;
        if (hot && ImGui::IsMouseDoubleClicked(0)) liveHeight_ = 250.0f * uiScale;
        if (hot || dragging) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ImGui::SetTooltip("%s", tr("Drag up or down to change the height of the live instrument. Double-click for the usual height."));
        }
        ImU32 grip = dragging ? IM_COL32(150, 170, 220, 255) : hot ? IM_COL32(120, 135, 175, 255) : IM_COL32(75, 80, 95, 255);
        ImDrawList* list = ImGui::GetWindowDrawList();
        float cx = handle.x + handleWidth * 0.5f, cy = handle.y + 4.5f * uiScale;
        list->AddRectFilled(ImVec2(cx - 36.0f * uiScale, cy - 2.0f * uiScale), ImVec2(cx + 36.0f * uiScale, cy + 2.0f * uiScale), grip, 2.0f * uiScale);
        for (int k = -2; k <= 2; ++k) list->AddCircleFilled(ImVec2(cx + k * 9.0f * uiScale, cy), 1.1f * uiScale, IM_COL32(20, 20, 24, 255));
    }
    // one column: the buttons on top, which do not scroll, and under them the list, which does
    ImGui::BeginChild("treecolumn", ImVec2(340.0f * (ImGui::GetFontSize() / 15.0f), -footer), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawTreeToolbar();
    ImGui::BeginChild("tree", ImVec2(0, 0));
    drawTree();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
    drawContent();
    ImGui::EndChild();

    {
        // the message at the left, clipped so that it never runs under the version, which is at the right
        std::string version = std::string("v") + about::Version;
        float versionWidth = ImGui::CalcTextSize(version.c_str()).x;
        float windowWidth = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(at, ImVec2(at.x + windowWidth - versionWidth - 24.0f * uiScale, at.y + ImGui::GetFontSize() * 2.0f), true);
        if (statusError_) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextUnformatted(((dirty_ ? "Unsaved changes. " : "") + status_).c_str());
        if (statusError_) ImGui::PopStyleColor();
        ImGui::PopClipRect();
        ImGui::SameLine(windowWidth - versionWidth);
        ImGui::TextDisabled("%s", version.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("About %s"), about::AppName);
        if (ImGui::IsItemClicked()) aboutPopup_ = true;
    }

    drawPopups();
    drawAbout();
    drawDeviceInfoWindow();
    drawHelp();
    drawComparePopup();
    ImGui::End();
    designer_.draw();

    // Group in-place edits: take the snapshot from before the first change and commit once the field is released.
    if (changedThisFrame_ && !pending_) {
        pending_ = lastSnapshot_;
        pending_->selection = root_ && selected_ ? pathTo(*root_, selected_) : std::vector<int>();
    }
    if (pending_ && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0)) flushPendingUndo();
    changedThisFrame_ = false;
}

// The layout of a module saved as a file of the data folder before anything is changed; the path, or nothing when it failed.
std::string Editor::backupLayout(int slot, const joue::Bytes& blob) {
    if (blob.empty()) return {};
    fs::path directory = fs::path(options_.dataDir) / "module-backups";
    std::error_code ignored;
    fs::create_directories(directory, ignored);
    std::string name = joue::moduleName(blob[0]);
    for (char& c : name) {
        if (c == ' ') c = '_';
    }
    std::string path = (directory / ("slot" + std::to_string(slot) + "-" + name + "-" + timestamp() + "-original.bin")).string();
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    return out ? path : std::string();
}

// The module in a slot becomes a library module that looks like the factory preset of its type, filled with what the module holds.
// Its layout is saved as a file first, before anything is changed.
void Editor::openModuleFromInstrument(int slot, const joue::Bytes& blob) {
    if (!root_ || blob.size() < joue::InstrumentHeader::Length) return;
    int type = blob[0];
    std::string typeName = joue::moduleName(type);
    const joue::ModuleBindings* bindings = joue::moduleBindings(type);
    std::string backup = backupLayout(slot, blob);
    if (backup.empty()) {
        setStatus("The layout of the module could not be saved, so it is not opened.", true);
        return;
    }
    if (!bindings || !joue::structureMatches(*bindings, blob)) {
        std::string edited = backup;
        std::size_t at = edited.rfind("-original.bin");
        if (at != std::string::npos) edited.replace(at, 13, "-edited.bin");
        if (designer_.openBlob(blob, edited, "the layout of slot " + std::to_string(slot))) {
            setStatus("Slot " + std::to_string(slot) + ": opened in the module designer, as a custom layout. The original is saved as " + backup + "; Save writes " + edited + ".");
            return;
        }
    }
    if (!bindings) {
        setStatus("The settings of " + typeName + " modules cannot be read from a module yet. Its layout is saved as " + backup + ".", true);
        return;
    }
    if (!joue::structureMatches(*bindings, blob)) {
        setStatus("This " + typeName + " module does not have the records of the factory layout, so its settings cannot be shown. Its layout is saved as " + backup + ".", true);
        return;
    }
    Node* templateModule = nullptr;
    walk(*root_, [&](Node& n) {
        if (n.tag != "Module" || templateModule || links_.count(n.name())) return;
        Node* typeEntry = kid(n, "String", "Type");
        if (typeEntry && typeEntry->attr("value") && std::atoi(typeEntry->attr("value")->c_str()) == type) templateModule = &n;
    });
    if (!templateModule) {
        setStatus("The library has no " + typeName + " preset to show this module with. Open a library that holds the factory presets (File > Open...). Its layout is saved as " + backup + ".", true);
        return;
    }
    auto created = joue::moduleFromBlob(*templateModule, *bindings, blob);
    std::string name = "Slot " + std::to_string(slot) + ": " + typeName;
    created->attrRef("name") = name;
    InstrumentLink link;
    link.slot = slot;
    link.original = link.current = blob;
    link.backup = backup;
    link.bindings = bindings;
    link.initial = joue::collectValues(*created);
    link.bound = joue::boundSettings(*bindings);

    beginStructural();
    Node* list = moduleList(*root_);
    Node* folder = nullptr;
    for (auto& entry : entriesOf(*list)) {
        if (entry->tag == "Folder" && entry->name() == "From the instrument") folder = entry.get();
    }
    if (!folder) {
        auto made = std::make_unique<Node>();
        made->tag = "Folder";
        made->attrs = {{"name", "From the instrument"}};
        setString(*made, "IsExpanded", "1");
        folder = made.get();
        entriesOf(*list).push_back(std::move(made));
        entriesOf(*folder);
    }
    auto& entries = entriesOf(*folder);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::unique_ptr<Node>& n) { return n->tag == "Module" && n->name() == name; }), entries.end());
    Node* shown = created.get();
    entries.push_back(std::move(created));
    selected_ = shown;
    endStructural();
    links_[name] = std::move(link);
    setStatus("Read slot " + std::to_string(slot) + ". The original layout is saved as " + backup + ".");
}

// What was edited is written into the blob that was read; the module is then filled again from the result, so that a group
// setting and the settings of its elements agree.
void Editor::pushLinked(Node& module, InstrumentLink& link, bool restore) {
    joue::Bytes edited = link.original;
    if (!restore) {
        joue::SettingValues now = joue::collectValues(module), changes;
        for (const auto& kv : now) {
            auto before = link.initial.find(kv.first);
            if (before == link.initial.end() || before->second != kv.second) changes[kv.first] = kv.second;
        }
        changes = joue::completeActionChanges(*link.bindings, now, changes);  // a new Action is written with the values that are shown
        edited = joue::encodeSettings(*link.bindings, link.current, changes);
        if (edited == link.current) {
            setStatus("Nothing to push: the settings that can be written are the same as on the module.");
            return;
        }
        std::string base = link.backup;
        std::size_t at = base.rfind("-original.bin");
        std::string copy = (at == std::string::npos ? base : base.substr(0, at)) + "-edited-" + timestamp() + ".bin";
        std::ofstream out(copy, std::ios::binary);
        out.write(reinterpret_cast<const char*>(edited.data()), static_cast<std::streamsize>(edited.size()));
        if (!out) {
            setStatus("Not pushed: the copy of the edited layout could not be saved as " + copy, true);
            return;
        }
        setStatus("The edited layout is saved as " + copy + " and is being written to slot " + std::to_string(link.slot) + ".");
    }
    beginStructural();
    joue::applyValues(module, joue::decodeSettings(*link.bindings, edited));
    endStructural();
    link.initial = joue::collectValues(module);
    link.current = edited;
    live_.requestPush(link.slot, link.original, edited);
}

// A library module (a preset from the file, or a module read earlier) is written into the module of the same type that is on the
// instrument: what the table covers is taken from the library module, the rest of the module stays as it is.
void Editor::uploadPreset(Node& module, int slot) {
    Node* typeEntry = kid(module, "String", "Type");
    int type = typeEntry && typeEntry->attr("value") ? std::atoi(typeEntry->attr("value")->c_str()) : 0;
    const joue::ModuleBindings* bindings = joue::moduleBindings(type);
    joue::Bytes base;
    for (const LiveView::LiveModule& m : live_.modules()) {
        if (m.slot == slot) base = m.raw;
    }
    if (!bindings || base.empty()) {
        setStatus("Nothing was uploaded: the module of slot " + std::to_string(slot) + " is not there any more.", true);
        return;
    }
    std::string why = joue::presetMismatch(module, base);
    if (!why.empty()) {
        setStatus(why, true);
        return;
    }
    joue::Bytes edited = joue::presetToBlob(*bindings, base, module);
    if (edited == base) {
        setStatus("Nothing to upload: the module in slot " + std::to_string(slot) + " already holds the settings of this preset.");
        return;
    }
    std::string backup = backupLayout(slot, base);
    if (backup.empty()) {
        setStatus("Not uploaded: the layout of the module could not be saved first.", true);
        return;
    }
    std::string copy = backup;
    std::size_t at = copy.rfind("-original.bin");
    copy = (at == std::string::npos ? copy : copy.substr(0, at)) + "-uploaded-" + timestamp() + ".bin";
    {
        std::ofstream out(copy, std::ios::binary);
        out.write(reinterpret_cast<const char*>(edited.data()), static_cast<std::streamsize>(edited.size()));
        if (!out) {
            setStatus("Not uploaded: the copy of the new layout could not be saved as " + copy, true);
            return;
        }
    }
    lastUpload_ = LastUpload{slot, base, backup};
    live_.requestPush(slot, base, edited);
    setStatus("Uploading to slot " + std::to_string(slot) + ". The previous layout is saved as " + backup + ".");
}

void Editor::drawUploadPanel(Node& module) {
    Node* typeEntry = kid(module, "String", "Type");
    if (!typeEntry || !typeEntry->attr("value")) return;
    int type = std::atoi(typeEntry->attr("value")->c_str());
    const joue::ModuleBindings* bindings = joue::moduleBindings(type);
    if (!bindings) return;
    std::string typeName = joue::moduleName(type);
    ImGui::SeparatorText(tr("Upload to Joué"));
    ImGui::TextDisabled(tr("Sends the settings of this preset to a %s."), typeName.c_str());
    std::vector<int> slots;
    std::string detected, reason;
    if (live_.open) {
        for (const LiveView::LiveModule& m : live_.modules()) {
            if (m.raw.empty()) continue;
            detected += std::string(detected.empty() ? "" : ", ") + tr("slot") + " " + std::to_string(m.slot) + " " + joue::moduleName(m.raw[0]);
            std::string why = joue::presetMismatch(module, m.raw);
            if (why.empty()) slots.push_back(m.slot);
            else if (m.raw[0] == type && reason.empty()) reason = why;  // the right type, but not the right version, revision or layout
        }
    }
    bool busy = live_.pushBusy();
    beginGrey(slots.empty() || busy);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i > 0) ImGui::SameLine();
        char uploadLabel[128];
        std::snprintf(uploadLabel, sizeof uploadLabel, tr("Upload to Joué (slot %d)..."), slots[i]);
        if (ImGui::Button(uploadLabel)) {
            uploadSlot_ = slots[i];
            ImGui::OpenPopup(tr("Upload to the Joué##uploadconfirm"));
        }
    }
    if (slots.empty()) ImGui::Button(tr("Upload to Joué..."));
    endGrey();
    if (!slots.empty()) {
        ImGui::SameLine();
        char compareLabel[128];
        std::snprintf(compareLabel, sizeof compareLabel, tr("Compare with slot %d..."), slots[0]);
        if (ImGui::Button(compareLabel)) {
            joue::Bytes inSlot;
            for (const LiveView::LiveModule& m : live_.modules()) {
                if (m.slot == slots[0]) inSlot = m.raw;
            }
            compareLines_.clear();
            if (!inSlot.empty()) compareLines_ = joue::describeChanges(*bindings, inSlot, joue::presetChanges(*bindings, inSlot, module));
            char title[160];
            std::snprintf(title, sizeof title, tr("Compare with the module in slot %d"), slots[0]);
            compareTitle_ = title;
            compareChoices_.clear();
            compareFirst_ = nullptr;
            compareChoice_ = -1;
            compareReady_ = true;
            comparePopup_ = true;
        }
    }
    if (slots.empty()) {
        ImGui::SameLine();
        if (!live_.open) ImGui::TextDisabled("%s", tr("Open the live instrument."));
        else if (!reason.empty()) ImGui::TextWrapped("%s", reason.c_str());
        else ImGui::TextDisabled(tr("No %s found%s%s."), typeName.c_str(), detected.empty() ? "" : tr(" - found: "), detected.c_str());
    }
    if (lastUpload_ && live_.open && live_.canPush(lastUpload_->slot)) {
        ImGui::TextDisabled(tr("Last upload: slot %d."), lastUpload_->slot);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("The previous layout is saved as %s."), lastUpload_->backup.c_str());
        if (ImGui::Button(tr("Restore the previous layout..."))) ImGui::OpenPopup(tr("Restore the previous layout##restoreupload"));
    }
    std::string result = joue::i18n::message(live_.pushStatus());
    if (!result.empty()) ImGui::TextWrapped("%s", result.c_str());
    if (ImGui::BeginPopupModal(tr("Upload to the Joué##uploadconfirm"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        float scale = ImGui::GetFontSize() / 15.0f;
        joue::Bytes base;
        for (const LiveView::LiveModule& m : live_.modules()) {
            if (m.slot == uploadSlot_) base = m.raw;
        }
        std::string why = base.empty() ? "The module is not there any more. Nothing is written." : joue::presetMismatch(module, base);
        ImGui::Text(tr("Upload \"%s\" to slot %d?"), module.name().c_str(), uploadSlot_);
        ImGui::Spacing();
        if (why.empty()) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), "%s", tr("This preset fits the module."));
        else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", joue::i18n::message(why).c_str());
        std::vector<std::string> lines;
        if (why.empty()) lines = joue::describeChanges(*bindings, base, joue::presetChanges(*bindings, base, module));
        if (why.empty()) {
            ImGui::Spacing();
            if (lines.empty()) {
                ImGui::TextUnformatted(tr("Nothing to change: the module has these settings."));
            } else {
                ImGui::Text(lines.size() == 1 ? tr("%zu setting will change:") : tr("%zu settings will change:"), lines.size());
                ImGui::BeginChild("uploadchanges", ImVec2(560.0f * scale, std::min<float>(lines.size(), 9.0f) * ImGui::GetTextLineHeightWithSpacing() + 8.0f * scale), ImGuiChildFlags_Borders);
                for (const std::string& line : lines) ImGui::TextUnformatted(line.c_str());
                ImGui::EndChild();
            }
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("The previous layout is saved first; Restore brings it back."));
        }
        ImGui::Spacing();
        beginGrey(!why.empty() || lines.empty());
        if (ImGui::Button(tr("Upload"))) {
            uploadPreset(module, uploadSlot_);
            ImGui::CloseCurrentPopup();
        }
        endGrey();
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal(tr("Restore the previous layout##restoreupload"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(tr("Write the previous layout back to slot %d?"), lastUpload_ ? lastUpload_->slot : 0);
        ImGui::Spacing();
        if (ImGui::Button(tr("Restore")) && lastUpload_) {
            live_.requestPush(lastUpload_->slot, lastUpload_->original, lastUpload_->original);
            setStatus("Restoring the previous layout of slot " + std::to_string(lastUpload_->slot) + ".");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::drawInstrumentPanel(Node& module, InstrumentLink& link) {
    ImGui::SeparatorText(tr("On the instrument"));
    ImGui::TextDisabled(tr("Read from slot %d; the original is saved."), link.slot);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Saved as %s."), link.backup.c_str());
    bool busy = live_.pushBusy();
    bool can = live_.open && live_.canPush(link.slot) && !busy;
    beginGrey(!can);
    if (ImGui::Button(tr("Upload to Joué..."))) ImGui::OpenPopup(tr("Upload the changes##pushconfirm"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Restore the original..."))) ImGui::OpenPopup(tr("Restore the original##restoreconfirm"));
    endGrey();
    ImGui::SameLine();
    if (ImGui::Button(tr("Revert my edits"))) {
        beginStructural();
        joue::applyValues(module, joue::decodeSettings(*link.bindings, link.current));
        endStructural();
        link.initial = joue::collectValues(module);
    }
    if (!can && !busy) ImGui::TextDisabled("%s", tr("Needs the live instrument open, with this module in it."));
    std::string result = joue::i18n::message(live_.pushStatus());
    if (!result.empty()) ImGui::TextWrapped("%s", result.c_str());
    if (ImGui::BeginPopupModal(tr("Upload the changes##pushconfirm"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(tr("Upload the changes to the %s in slot %d?"), joue::moduleName(link.original.empty() ? 0 : link.original[0]).c_str(), link.slot);
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("Only the settings you changed are written."));
        ImGui::TextDisabled("%s", tr("The original is saved; Restore the original brings it back."));
        ImGui::Spacing();
        if (ImGui::Button(tr("Upload"))) {
            pushLinked(module, link, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal(tr("Restore the original##restoreconfirm"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(tr("Write the original layout back to slot %d?"), link.slot);
        ImGui::Spacing();
        ImGui::TextDisabled(tr("The layout as it was read (%zu bytes)."), link.original.size());
        ImGui::Spacing();
        if (ImGui::Button(tr("Restore"))) {
            pushLinked(module, link, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The few choices that are kept from one start to the next, one per line in the data folder.
void Editor::loadUiSettings() {
    std::ifstream in(fs::path(options_.dataDir) / "ui.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line == "listenToPlay=0") live_.listenToPlay = false;
        if (line == "listenToPlay=1") live_.listenToPlay = true;
        if (line == "touchSelect=0") touchSelect_ = false;
        if (line == "touchSelect=1") touchSelect_ = true;
        if (line == "welcome=1") welcomeSeen_ = true;
        if (line == "checkUpdates=0") checkUpdates_ = false;
        if (line.rfind("updateChecked=", 0) == 0) lastUpdateCheck_ = std::atoll(line.c_str() + 14);
        if (line.rfind("language=", 0) == 0) {
            joue::i18n::setLanguage(joue::i18n::languageFromCode(line.substr(9)));
            languageChosen_ = true;
        }
    }
    if (!options_.language.empty()) joue::i18n::setLanguage(joue::i18n::languageFromCode(options_.language));
    else if (!languageChosen_) joue::i18n::setLanguage(joue::i18n::detectSystemLanguage());
    if (!welcomeSeen_ && options_.library.empty() && options_.designer.empty() && !options_.about && !options_.noUpdateCheck) helpOpen_ = true;
}

void Editor::saveUiSettings() const {
    try {
        fs::create_directories(options_.dataDir);
        std::ofstream out(fs::path(options_.dataDir) / "ui.txt");
        out << "listenToPlay=" << (live_.listenToPlay ? 1 : 0) << "\n";
        out << "touchSelect=" << (touchSelect_ ? 1 : 0) << "\n";
        out << "welcome=" << (welcomeSeen_ ? 1 : 0) << "\n";
        out << "checkUpdates=" << (checkUpdates_ ? 1 : 0) << "\n";
        out << "updateChecked=" << lastUpdateCheck_ << "\n";
        if (languageChosen_) out << "language=" << joue::i18n::languageCode(joue::i18n::language()) << "\n";
    } catch (const std::exception&) {
    }
}

// A finger on the instrument selects the element in the editor, as a click on it would: in the module that is open when it is of the
// type of the module touched, and in the designer when the temporary layout is touched.
void Editor::applyTouchSelection() {
    auto touched = live_.takeTouched();
    if (!touched || !touchSelect_) return;
    if (touched->temporary) {
        if (designer_.open) designer_.touchSelect(static_cast<int>(touched->record));
        return;
    }
    if (!selected_ || selected_->tag != "Module") return;
    std::string name = joue::touchedElementName(*selected_, touched->type, touched->record);
    if (name.empty()) return;
    selectedShape_ = name;
    selectedElement_ = joue::elementForShape(*selected_, name);
}

void Editor::setGraphicsInfo(const std::string& info) {
    graphicsInfo_ = info;
    std::string lower = info;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    graphicsSoftware_ = false;
    for (const char* word : {"gdi generic", "llvmpipe", "softpipe", "software", "microsoft basic", "d3d12", "swiftshader", "vmware", "virtualbox", "svga3d", "virgl", "parallels"}) {
        if (lower.find(word) != std::string::npos) graphicsSoftware_ = true;
    }
    try {
        fs::create_directories(options_.dataDir);
        std::ofstream out(fs::path(options_.dataDir) / "graphics.log");
        out << about::AppName << " " << about::Version << (options_.safeGraphics ? " (safe graphics)" : "") << "\n" << info << "\n";
        if (graphicsSoftware_) out << "Software, translated or virtual OpenGL detected.\n";
    } catch (const std::exception&) {
    }
}

std::string findDocument(const std::string& name) {
    std::string base = executableDirectory();
    const char* relatives[] = {"", "/docs", "/..", "/../docs", "/../..", "/../../docs", "/../../.."};
    for (const char* relative : relatives) {
        fs::path candidate = fs::path(base + relative) / name;
        std::error_code ec;
        if (fs::exists(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();
    }
    for (const char* directory : {".", "docs"}) {
        std::error_code ec;
        if (fs::exists(fs::path(directory) / name, ec)) return fs::absolute(fs::path(directory) / name, ec).string();
    }
    return {};
}

void Editor::openGuide(bool designer) {
    bool french = joue::i18n::language() == joue::i18n::Language::French;
    std::string name = std::string(designer ? "module-designer" : "user-guide") + (french ? ".fr.md" : ".md");
    std::string path = findDocument(name);
    openExternal(path.empty() ? std::string("https://github.com/") + about::Repository + "/blob/main/docs/" + name : path);
}

void Editor::pollUpdateCheck() {
    if (!updateStarted_) {
        updateStarted_ = true;
        long long now = static_cast<long long>(std::time(nullptr));
        if (checkUpdates_ && !options_.noUpdateCheck && now - lastUpdateCheck_ > 86400) {
            update_.start(about::Repository);
            lastUpdateCheck_ = now;
            saveUiSettings();
        }
    }
    bool ok = false;
    std::string tag, url;
    if (!update_.takeResult(ok, tag, url)) return;
    if (ok && joue::isNewerVersion(tag, about::Version)) {
        updateTag_ = tag;
        updateUrl_ = url;
    } else if (updateManual_) {
        setStatus(ok ? "You have the latest version." : "Could not check for updates.", !ok);
    }
    updateManual_ = false;
}

void Editor::drawDeviceInfoWindow() {
    if (!deviceInfoOpen_) return;
    const float scale = ImGui::GetFontSize() / 15.0f;
    ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 440.0f * scale), ImGuiCond_FirstUseEver);
    std::string title = std::string(tr("Device information")) + "###deviceinfo";
    if (!ImGui::Begin(title.c_str(), &deviceInfoOpen_)) {
        ImGui::End();
        return;
    }
    std::vector<std::pair<std::string, std::string>> lines = live_.deviceInfo();
    bool busy = live_.deviceInfoBusy();
    bool can = live_.canWriteTag();
    ImGui::BeginDisabled(!can || busy);
    if (ImGui::Button(tr("Read again"))) live_.requestDeviceInfo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(lines.empty());
    if (ImGui::Button(tr("Copy"))) {
#if defined(_WIN32)
        const char* system = "Windows";
#elif defined(__APPLE__)
        const char* system = "macOS";
#else
        const char* system = "Linux";
#endif
        std::string text = std::string("Joue Revival ") + about::Version + " (" + system + ")\n";
        for (const auto& line : lines) text += line.first + ": " + line.second + "\n";
        ImGui::SetClipboardText(text.c_str());
        setStatus("Copied to the clipboard.");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("The text is copied in English, for support."));
    ImGui::SameLine();
    if (busy) ImGui::TextDisabled("%s", tr("Reading..."));
    else if (!can && lines.empty()) ImGui::TextDisabled("%s", tr("Connect the Joué first."));
    ImGui::Separator();
    if (ImGui::BeginTable("deviceinfo", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 200.0f * scale);
        ImGui::TableSetupColumn("value");
        auto row = [&](const std::string& label, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(joue::i18n::message(label).c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", joue::i18n::message(value).c_str());
        };
        row("Program", std::string("Joué Revival ") + about::Version);
        for (const auto& line : lines) row(line.first, line.second);
        ImGui::EndTable();
    }
    ImGui::End();
}

void Editor::drawHelp() {
    if (!helpOpen_) return;
    const float scale = ImGui::GetFontSize() / 15.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->GetCenter().y), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    std::string title = std::string(welcomeSeen_ ? tr("Quick start") : tr("Welcome")) + "###quickstart";
    if (!ImGui::Begin(title.c_str(), &helpOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f * scale);
    if (!welcomeSeen_) {
        ImGui::TextWrapped("%s", tr("Joué Revival edits the libraries of the original Joué Editor, shows the instrument live, and has a module designer."));
        ImGui::Spacing();
    }
    const char* steps[6] = {tr("Quit the original Joué Editor: it holds the instrument's MIDI ports."),
                            tr("Connect the Joué by USB. The Live instrument panel says Connected and shows the modules."),
                            tr("The factory presets load by themselves the first time. File > Open... opens another library, and Save keeps your own."),
                            tr("Click a preset on the left, then an element in its picture: its settings are on the right."),
                            tr("Edit opens a module of the instrument as a copy; Upload to Joué writes only what you changed."),
                            tr("File > Module designer draws new layouts: print them, try them, write them to a blank tag.")};
    for (int i = 0; i < 6; ++i) ImGui::TextWrapped("%d. %s", i + 1, steps[i]);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Press F1 to see this again."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(tr("User guide"))) openGuide(false);
    ImGui::SameLine();
    if (ImGui::Button(tr("Open library file..."))) {
        if (dialogsAvailable()) {
            if (auto path = chooseOpenFile()) openFromPath(*path);
        } else {
            openPathPopup_ = true;
        }
        helpOpen_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Close"))) helpOpen_ = false;
    bool showAtStart = !welcomeSeen_;
    if (ImGui::Checkbox(tr("Show this window at start"), &showAtStart)) {
        welcomeSeen_ = !showAtStart;
        saveUiSettings();
    }
    ImGui::End();
    if (!helpOpen_ && !welcomeSeen_) {
        welcomeSeen_ = true;
        saveUiSettings();
    }
}

void Editor::drawComparePopup() {
    if (comparePopup_) {
        ImGui::OpenPopup("###compare");
        comparePopup_ = false;
    }
    const float scale = ImGui::GetFontSize() / 15.0f;
    std::string title = compareTitle_ + "###compare";
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (compareFirst_) {
        ImGui::TextDisabled(tr("%s compared with:"), compareFirst_->name().c_str());
        const std::string current = compareChoice_ >= 0 && compareChoice_ < static_cast<int>(compareChoices_.size()) ? compareChoices_[static_cast<std::size_t>(compareChoice_)]->name() : std::string(tr("Choose a preset"));
        ImGui::SetNextItemWidth(300.0f * scale);
        if (ImGui::BeginCombo("##comparewith", current.c_str())) {
            for (int i = 0; i < static_cast<int>(compareChoices_.size()); ++i) {
                if (ImGui::Selectable((compareChoices_[static_cast<std::size_t>(i)]->name() + "##" + std::to_string(i)).c_str(), i == compareChoice_)) {
                    compareChoice_ = i;
                    compareLines_ = joue::compareModules(*compareFirst_, *compareChoices_[static_cast<std::size_t>(i)]);
                    compareReady_ = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("%s", tr("What an upload would change. Nothing is written."));
    }
    if (compareReady_) {
        ImGui::Spacing();
        if (compareLines_.empty()) {
            ImGui::TextUnformatted(tr("No difference."));
        } else {
            ImGui::Text(compareLines_.size() == 1 ? tr("%zu difference:") : tr("%zu differences:"), compareLines_.size());
            ImGui::BeginChild("comparelines", ImVec2(640.0f * scale, std::min<float>(static_cast<float>(compareLines_.size()), 14.0f) * ImGui::GetTextLineHeightWithSpacing() + 8.0f * scale), ImGuiChildFlags_Borders);
            for (const std::string& line : compareLines_) ImGui::TextUnformatted(joue::i18n::message(line).c_str());
            ImGui::EndChild();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Editor::drawAbout() {
    if (aboutPopup_) {
        ImGui::OpenPopup(tr("About"));
        aboutPopup_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(tr("About"), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    float scale = ImGui::GetFontSize() / 15.0f;
    if (logo_.id != 0) {
        ImGui::Image(static_cast<ImTextureID>(logo_.id), ImVec2(104.0f * scale, 104.0f * scale));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    if (Fonts::heading) {
        ImGui::PushFont(Fonts::heading);
        ImGui::TextUnformatted(about::AppName);
        ImGui::PopFont();
    } else {
        ImGui::SetWindowFontScale(1.7f);
        ImGui::TextUnformatted(about::AppName);
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::Text(tr("Version %s"), about::Version);
    ImGui::Text(tr("Author: %s"), about::Author);
    ImGui::Text(tr("Licence: %s, with no warranty."), about::Licence);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("An independent editor for the Joué instrument,"));
    ImGui::TextDisabled("%s", tr("for Windows, macOS and Linux."));
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Based on the reverse engineering of:"));
    ImGui::BulletText("%s", tr("the Joué Editor for Windows (decompiled .NET code);"));
    ImGui::BulletText("%s", tr("the Joué Editor for macOS (Objective-C code, run in an emulator to measure what it does);"));
    ImGui::BulletText("%s", tr("the firmware of the Joué board (versions 1.11 and 1.13, ARM code);"));
    ImGui::BulletText("%s", tr("the open source J-Play application (GPL-3), for the protocol and the module formats."));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Built with Dear ImGui, GLFW, pugixml and stb_image."));
    if (!graphicsInfo_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 640.0f * scale);
        ImGui::TextDisabled(tr("Graphics: %s"), graphicsInfo_.c_str());
        if (graphicsSoftware_ && !options_.safeGraphics) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1.0f), "%s", tr("This computer draws with software, translated or virtual OpenGL. If some areas are drawn black, start the program with --safe-graphics (the Windows folder has a shortcut for it)."));
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::TextDisabled("%s", tr("Joué is a trademark of its owner. This program is independent: it is not made, endorsed or supported by Joué."));
    ImGui::Spacing();
    if (ImGui::Button(tr("Close"), ImVec2(120.0f * scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Editor::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (logo_.id == 0) {
        LogoImage logo = logoPng(256);
        logo_ = createTexture(logo.data, logo.size);
    }
    if (logo_.id != 0) {
        // the logo in the corner; a click opens the About window
        float side = ImGui::GetFrameHeight() - 4.0f * (ImGui::GetFontSize() / 15.0f);
        ImGui::Image(static_cast<ImTextureID>(logo_.id), ImVec2(side, side));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("About %s"), about::AppName);
        if (ImGui::IsItemClicked()) aboutPopup_ = true;
    }
    if (ImGui::BeginMenu(tr("File"))) {
        if (ImGui::MenuItem(tr("New library"))) { loadRoot(joue::emptyLibrary(), "New empty library."); dirty_ = true; }
        if (ImGui::MenuItem(tr("Open..."))) {
            if (dialogsAvailable()) {
                if (auto path = chooseOpenFile()) openFromPath(*path);
            } else {
                openPathPopup_ = true;
            }
        }
        if (ImGui::MenuItem(tr("Open from path..."))) openPathPopup_ = true;
        if (ImGui::MenuItem(tr("Open the factory presets"))) {
            std::string presets = findPresets();
            if (presets.empty()) setStatus("The factory presets were not found next to the program.", true);
            else openFromPath(presets);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Module designer..."))) designer_.open = true;
        if (ImGui::MenuItem(tr("Live instrument"), nullptr, &live_.open)) {}
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Save"), "Ctrl+S", false, root_ != nullptr)) saveLibrary();
        if (ImGui::MenuItem(tr("Export a copy..."), nullptr, false, root_ != nullptr)) {
            if (dialogsAvailable()) {
                if (auto path = chooseSaveFile()) exportCopy(*path);
            } else {
                exportPopup_ = true;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Quit"))) quitRequested_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Edit"))) {
        if (ImGui::MenuItem(tr("Undo"), "Ctrl+Z", false, !undo_.empty() || pending_)) undo();
        if (ImGui::MenuItem(tr("Redo"), "Shift+Ctrl+Z", false, !redo_.empty())) redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("View"))) {
        if (ImGui::MenuItem(tr("Device information..."))) {
            deviceInfoOpen_ = true;
            live_.requestDeviceInfo();
        }
        ImGui::MenuItem(tr("Module designer"), nullptr, &designer_.open);
        ImGui::MenuItem(tr("Live instrument"), nullptr, &live_.open);
        if (ImGui::BeginMenu("Language / Langue")) {
            bool english = joue::i18n::language() == joue::i18n::Language::English;
            if (ImGui::MenuItem("English", nullptr, english) && !english) {
                joue::i18n::setLanguage(joue::i18n::Language::English);
                languageChosen_ = true;
                saveUiSettings();
            }
            if (ImGui::MenuItem("Français", nullptr, !english) && english) {
                joue::i18n::setLanguage(joue::i18n::Language::French);
                languageChosen_ = true;
                saveUiSettings();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("Touch to select"), nullptr, &touchSelect_)) saveUiSettings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("A finger on the instrument selects that element in the open module."));
        bool listen = live_.listenToPlay;
        if (ImGui::MenuItem(tr("Listen to the Play port (touches)"), nullptr, &listen)) {
            live_.listenToPlay = listen;
            saveUiSettings();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Help"))) {
        if (ImGui::MenuItem(tr("Quick start"), "F1")) helpOpen_ = true;
        if (ImGui::MenuItem(tr("User guide"))) openGuide(false);
        if (ImGui::MenuItem(tr("Module designer guide"))) openGuide(true);
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Check for updates at start"), nullptr, &checkUpdates_)) saveUiSettings();
        if (ImGui::MenuItem(tr("Check now"))) {
            updateManual_ = true;
            update_.start(about::Repository);
        }
        if (ImGui::MenuItem(tr("Releases page"))) openExternal(std::string("https://github.com/") + about::Repository + "/releases");
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(tr("About"))) aboutPopup_ = true;
    if (!updateTag_.empty()) {
        char notice[160];
        std::snprintf(notice, sizeof notice, tr("Version %s is available"), updateTag_.c_str());
        float width = ImGui::CalcTextSize(notice).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - width - 24.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.5f, 1.0f));
        if (ImGui::SmallButton(notice)) openExternal(updateUrl_.empty() ? std::string("https://github.com/") + about::Repository + "/releases" : updateUrl_);
        ImGui::PopStyleColor();
    }
    ImGui::EndMenuBar();
}

// The buttons of the library list stay where they are when the list scrolls.
void Editor::drawTreeToolbar() {
    if (!root_ || !moduleList(*root_)) return;
    bool has = selected_ != nullptr;
    bool protectedItem = has && joue::containsLockedModule(*selected_);
    // the buttons go on to a second line when the next one does not fit (French labels are longer)
    const float limit = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    auto sameLineIfFits = [&](const char* nextLabel) {
        float width = ImGui::CalcTextSize(nextLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= limit) ImGui::SameLine();
    };
    if (ImGui::Button(tr("New folder"))) newFolder();
    sameLineIfFits(tr("Duplicate"));
    beginGrey(!has);
    if (ImGui::Button(tr("Duplicate"))) duplicateSelected();
    sameLineIfFits(tr("Up"));
    if (ImGui::Button(tr("Up"))) moveSelected(-1);
    sameLineIfFits(tr("Down"));
    if (ImGui::Button(tr("Down"))) moveSelected(1);
    endGrey();
    sameLineIfFits(tr("Delete"));
    beginGrey(!has || protectedItem);
    if (ImGui::Button(tr("Delete"))) deletePopup_ = true;
    endGrey();
    if (protectedItem && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Locked factory presets cannot be deleted."));
    ImGui::Separator();
}

void Editor::drawTree() {
    if (!root_ || !moduleList(*root_)) {
        ImGui::TextDisabled("%s", tr("No library loaded."));
        return;
    }
    drawEntries(*moduleList(*root_));
}

void Editor::drawEntries(Node& owner) {
    auto& list = entriesOf(owner);
    for (auto& entry : list) {
        Node* node = entry.get();
        if (node->tag != "Folder" && node->tag != "Module") continue;
        ImGui::PushID(node);
        bool isFolder = node->tag == "Folder";
        std::string label = node->name().empty() ? "(unnamed)" : node->name();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (node == selected_) flags |= ImGuiTreeNodeFlags_Selected;
        if (isFolder) {
            bool expanded = flag(*node, "IsExpanded");
            ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
            bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            if (open != expanded) {
                setString(*node, "IsExpanded", open ? "1" : "0");
                dirty_ = true;
            }
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected_ = node;
            if (open) {
                drawEntries(*node);
                ImGui::TreePop();
            }
        } else {
            float scale = ImGui::GetFontSize() / 15.0f;
            float thumbnailHeight = 40.0f * scale;
            float rowHeight = thumbnailHeight + 8.0f * scale;
            ImVec2 rowPos = ImGui::GetCursorScreenPos();
            float rowWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable(tr("##row"), node == selected_, 0, ImVec2(0, rowHeight))) selected_ = node;
            ImDrawList* list = ImGui::GetWindowDrawList();
            float pictureX = rowPos.x + std::max(rowWidth - 150.0f * scale, rowWidth * 0.45f);
            list->PushClipRect(rowPos, ImVec2(pictureX - 4.0f * scale, rowPos.y + rowHeight), true);
            list->AddText(ImVec2(rowPos.x + 6.0f * scale, rowPos.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            list->PopClipRect();
            float pixelsPerMm = thumbnailHeight / 90.0f;
            drawArt(list, ImVec2(pictureX, rowPos.y + 4.0f * scale), pixelsPerMm, intValue(*node, "Type"), intValue(*node, "Color"), joue::MirrorPictures);
            if (flag(*node, "ReadOnly")) drawLock(list, ImVec2(rowPos.x + rowWidth - 22.0f * scale, rowPos.y + (rowHeight - 16.0f * scale) * 0.5f), 16.0f * scale, IM_COL32(190, 190, 190, 255));
        }
        ImGui::PopID();
    }
}

void Editor::drawContent() {
    if (!root_) {
        ImGui::SeparatorText(tr("Welcome"));
        ImGui::TextWrapped("%s", tr("Open a library file, or start an empty one."));
        ImGui::Spacing();
        if (ImGui::Button(tr("Open library file..."))) {
            if (dialogsAvailable()) {
                if (auto path = chooseOpenFile()) openFromPath(*path);
            } else {
                openPathPopup_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("New empty library"))) { loadRoot(joue::emptyLibrary(), "New empty library."); dirty_ = true; }
        return;
    }
    if (!selected_) {
        ImGui::TextDisabled("%s", tr("Select a module or folder on the left."));
        return;
    }
    ImGui::PushID(selected_);
    if (selected_->tag == "Folder") {
        ImGui::SeparatorText(tr("Folder"));
        std::string name = selected_->name();
        ImGui::TextUnformatted(tr("Name"));
        ImGui::SameLine(110);
        if (editText("##name", name, false, 260)) selected_->attrRef("name") = name;
    } else {
        drawModule(*selected_);
    }
    ImGui::PopID();
}

namespace {

// Grey title bar like the panels of the original application.
void drawBar(const char* text) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() * 0.9f;
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(122, 128, 130, 255));
    ImVec2 size = ImGui::CalcTextSize(text);
    list->AddText(p + ImVec2((w - size.x) * 0.5f, (h - size.y) * 0.5f), IM_COL32(24, 24, 24, 255), text);
    ImGui::Dummy(ImVec2(w, h + 4.0f));
}

// Current value of a setting by name inside a module or element, searching nested settings too.
std::string valueIn(Node& owner, const std::string& name) {
    std::string found;
    bool done = false;
    std::function<void(Node&)> search = [&](Node& node) {
        for (auto& child : node.children) {
            if (done) return;
            if (child->tag == "Setting" && child->name() == name) {
                const std::string* v = child->attr("value");
                found = v ? *v : std::string();
                done = true;
                return;
            }
            search(*child);
        }
    };
    for (Node* array : kids(owner, "Array", "SettingsList")) search(*array);
    return found;
}

// 1, 2 or 3 for a per-axis setting such as ChannelX, ModeY or RangeZ, else 0.
int axisOf(const std::string& name) {
    if (name.size() < 2 || !std::islower(static_cast<unsigned char>(name[name.size() - 2]))) return 0;
    switch (name.back()) {
        case 'X': return 1;
        case 'Y': return 2;
        case 'Z': return 3;
        default: return 0;
    }
}

bool hasAxisSettings(const Node& node) {
    for (const auto& child : node.children) {
        if (child->tag == "Setting" && axisOf(child->name()) != 0) return true;
        if (hasAxisSettings(*child)) return true;
    }
    return false;
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace

// Applies the program's dynamic rules for the setting being drawn; returns whether any rule fired.
bool Editor::ruleFlags(const std::string& settingName, bool& hidden, bool& disabled) {
    const auto* rules = joue::moduleRules(ctxType_);
    if (!rules || !ctxModule_) return false;
    bool fired = false;
    for (const joue::SettingRule& r : *rules) {
        if (r.targetName != settingName) continue;
        bool applies = false;
        if (r.targetScope == "M") applies = ctxScope_ == "M";
        else if (r.targetScope == "E*") applies = ctxScope_ != "M";
        else if (r.targetScope == ".") applies = ctxScope_ != "M" && contains(r.sourceScope, ctxScope_);
        else applies = contains(r.targetElements, ctxScope_);
        if (!applies) continue;
        std::string current;
        if (r.sourceScope.size() == 1 && r.sourceScope[0] == "M") current = valueIn(*ctxModule_, r.sourceName);
        else if (ctxElement_ && contains(r.sourceScope, ctxScope_)) current = valueIn(*ctxElement_, r.sourceName);
        else continue;
        if (current == r.value) {
            hidden = r.hidden;
            disabled = r.disabled || r.readOnly;
            fired = true;
        }
    }
    return fired;
}

void Editor::drawLeftOfModule(Node& module, bool locked, float width) {
    float scale = ImGui::GetFontSize() / 15.0f;
    if (activeLink_) drawInstrumentPanel(module, links_[module.name()]);
    else drawUploadPanel(module);
    int type = intValue(module, "Type");
    if (locked) {
        ImGui::TextColored(ImVec4(0.75f, 0.65f, 1.0f, 1.0f), "%s", tr("Locked factory preset."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Unlock to edit"))) {
            beginStructural();
            unlock(module);
            endStructural();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("or Duplicate it to keep the original."));
    }
    std::string name = module.name();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Name"));
    ImGui::SameLine(70.0f * scale);
    if (editText("##name", name, locked, 260.0f * scale)) module.attrRef("name") = name;
    ImGui::SameLine();
    if (ImGui::Button(tr("Compare..."))) {
        compareFirst_ = &module;
        compareChoices_.clear();
        std::function<void(Node&)> collect = [&](Node& n) {
            if (n.tag == "Module") {
                if (&n != &module) compareChoices_.push_back(&n);
                return;
            }
            for (auto& c : n.children) collect(*c);
        };
        collect(*root_);
        compareChoice_ = -1;
        compareLines_.clear();
        compareReady_ = false;
        compareTitle_ = tr("Compare with another preset");
        comparePopup_ = true;
    }
    ImGui::Spacing();

    joue::ModuleArt art = joue::moduleArt(type);
    float height = 150.0f * scale;
    float pixelsPerMm = std::min(height / art.heightMm, (width - 8.0f * scale) / art.widthMm);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##picture", ImVec2(art.widthMm * pixelsPerMm, art.heightMm * pixelsPerMm));
    bool hovered = ImGui::IsItemHovered();
    int hit = hovered ? hitShape(art, origin, pixelsPerMm, joue::MirrorPictures, ImGui::GetMousePos()) : -1;
    std::string hoverName = hit >= 0 ? art.shapes[static_cast<std::size_t>(hit)].element : std::string();
    std::string highlight = selectedShape_.empty() ? selectedElement_ : selectedShape_;
    drawArt(ImGui::GetWindowDrawList(), origin, pixelsPerMm, type, intValue(module, "Color"), joue::MirrorPictures, highlight, hoverName);
    if (hit >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", hoverName.c_str());
        if (ImGui::IsItemClicked(0)) {
            selectedShape_ = hoverName;
            selectedElement_ = hoverName;
            // a key or a pad is edited on its own or through its group, as the original application does it (see elementForShape)
            selectedElement_ = joue::elementForShape(module, hoverName);
        }
    }
    if (!selectedShape_.empty()) ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), tr("Selected: %s"), selectedShape_.c_str());
    else ImGui::TextDisabled("%s", tr("Click an element in the picture to edit its MIDI mapping."));
    if (!art.known) ImGui::TextDisabled("%s", tr("Picture not drawn yet for this module type."));
    ImGui::Spacing();

    if (ImGui::TreeNodeEx(tr("Details"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (const char* key : {"Type", "Version", "Revision", "Color", "Options"}) {
            Node* entry = kid(module, "String", key);
            if (!entry) continue;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(key);
            ImGui::SameLine(110);
            ImGui::PushID(key);
            std::string value = entry->attr("value") ? *entry->attr("value") : std::string();
            if (editText("##value", value, locked, 120)) entry->attrRef("value") = value;
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    Node* elementList = kid(module, "Array", "ElementList");
    std::vector<Node*> elements;
    if (elementList) {
        for (auto& child : elementList->children) {
            if (child->tag == "Element") elements.push_back(child.get());
        }
    }
    if (ImGui::TreeNodeEx((std::string(tr("All elements")) + " (" + std::to_string(elements.size()) + ")").c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (Node* element : elements) {
            ImGui::PushID(element);
            bool isSelected = element->name() == selectedElement_;
            if (ImGui::Selectable(element->name().c_str(), isSelected)) {
                selectedElement_ = element->name();
                selectedShape_ = element->name();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

void Editor::drawRightOfModule(Node& module, bool locked) {
    int type = intValue(module, "Type");
    ctxModule_ = &module;
    ctxType_ = type;
    ctxScope_ = "M";
    ctxElement_ = nullptr;
    drawBar(tr("SETTINGS"));
    for (Node* array : kids(module, "Array", "SettingsList")) drawSettings(*array, locked, joue::moduleSettingsMeta(type), std::string());
    ImGui::Spacing();
    drawBar(tr("MIDI MAPPING"));
    Node* list = kid(module, "Array", "ElementList");
    Node* element = nullptr;
    if (list) {
        for (auto& child : list->children) {
            if (child->tag == "Element" && child->name() == selectedElement_) element = child.get();
        }
    }
    if (!element) {
        ImGui::TextDisabled("%s", tr("Select an element in the picture."));
        return;
    }
    ImGui::TextUnformatted(element->name().c_str());
    ImGui::Spacing();
    ctxScope_ = element->name();
    ctxElement_ = element;
    const auto* meta = joue::elementSettingsMeta(type, element->name());
    if (hasAxisSettings(*element)) {
        ImGui::TextDisabled("%s", tr("Axis"));
        ImGui::SameLine();
        ImGui::RadioButton(tr("All"), &axisFilter_, 0);
        ImGui::SameLine();
        ImGui::RadioButton(tr("X"), &axisFilter_, 1);
        ImGui::SameLine();
        ImGui::RadioButton(tr("Y"), &axisFilter_, 2);
        ImGui::SameLine();
        ImGui::RadioButton(tr("Z"), &axisFilter_, 3);
    } else {
        axisFilter_ = 0;
    }
    for (Node* array : kids(*element, "Array", "SettingsList")) drawSettings(*array, locked, meta, std::string());
    ctxScope_ = "M";
    ctxElement_ = nullptr;
}

void Editor::drawModule(Node& module) {
    bool locked = flag(module, "ReadOnly");
    auto link = links_.find(module.name());
    activeLink_ = link == links_.end() ? nullptr : &link->second;
    if (selectionOwner_ != &module) {
        selectionOwner_ = &module;
        selectedElement_.clear();
        selectedShape_.clear();
        if (!pendingElement_.empty()) {
            selectedElement_ = selectedShape_ = pendingElement_;
            pendingElement_.clear();
        }
    }
    {
        // ticking or unticking Custom mapping changes what a selected key edits: its own settings, or those of the group
        std::string expertNow = joue::collectValues(module)[{"M", "Expert"}];
        if (lastExpertModule_ != &module) {
            lastExpertModule_ = &module;
            lastExpert_ = expertNow;
        } else if (expertNow != lastExpert_) {
            lastExpert_ = expertNow;
            if (!selectedShape_.empty()) selectedElement_ = joue::elementForShape(module, selectedShape_);
        }
    }
    float scale = ImGui::GetFontSize() / 15.0f;
    float available = ImGui::GetContentRegionAvail().x;
    float rightWidth = 400.0f * scale;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    if (available > 760.0f * scale) {
        float leftWidth = available - rightWidth - spacing;
        ImGui::BeginChild("moduleLeft", ImVec2(leftWidth, 0));
        drawLeftOfModule(module, locked, leftWidth);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("moduleRight", ImVec2(0, 0));
        drawRightOfModule(module, locked);
        ImGui::EndChild();
    } else {
        drawLeftOfModule(module, locked, available);
        ImGui::Spacing();
        drawRightOfModule(module, locked);
    }
    activeLink_ = nullptr;
}

// Whether drawSettings would draw anything for these settings: a group (the advanced settings) with nothing to show is not drawn.
bool Editor::anyVisibleSetting(Node& array, const std::vector<joue::SettingMeta>* meta, const std::string& parentValue) {
    for (auto& child : array.children) {
        if (child->tag != "Setting") continue;
        Node& setting = *child;
        const joue::SettingMeta* m = meta ? joue::findSettingMeta(*meta, setting.name()) : nullptr;
        if (m && !joue::shownFor(*m, parentValue)) continue;
        bool hidden = m && m->baseHidden;
        bool disabled = false;
        if (m) ruleFlags(setting.name(), hidden, disabled);
        bool category = m && m->type == joue::settingtype::Category;
        if (activeLink_ && !category && !activeLink_->bound.count({ctxScope_, setting.name()})) continue;
        if (hidden) continue;
        if (axisFilter_ != 0 && axisOf(setting.name()) != 0 && axisOf(setting.name()) != axisFilter_) continue;
        if (!category) return true;
        const std::string* current = setting.attr("value");
        for (auto& sub : setting.children) {
            if (sub->tag == "Array" && anyVisibleSetting(*sub, &m->children, current ? *current : std::string())) return true;
        }
    }
    return false;
}

void Editor::drawSettings(Node& array, bool locked, const std::vector<joue::SettingMeta>* meta, const std::string& parentValue) {
    for (auto& child : array.children) {
        if (child->tag != "Setting") continue;
        Node& setting = *child;
        const joue::SettingMeta* m = meta ? joue::findSettingMeta(*meta, setting.name()) : nullptr;
        // a setting that the original application hides for the parent's current value is not shown
        if (m && !joue::shownFor(*m, parentValue)) continue;
        bool hidden = m && m->baseHidden;
        bool disabled = m && (m->baseDisabled || m->baseReadOnly);
        if (m) ruleFlags(setting.name(), hidden, disabled);
        // a module read from the device shows only the settings that can be written into it; nothing is shown greyed out
        if (activeLink_ && !(m && m->type == joue::settingtype::Category) && !activeLink_->bound.count({ctxScope_, setting.name()})) continue;
        if (hidden) continue;
        if (axisFilter_ != 0 && axisOf(setting.name()) != 0 && axisOf(setting.name()) != axisFilter_) continue;
        ImGui::PushID(&setting);
        std::string title = joue::i18n::text(m && !m->title.empty() ? m->title : setting.name());
        const std::string* current = setting.attr("value");
        if (m && m->type == joue::settingtype::Category) {
            bool any = false;
            for (auto& sub : setting.children) {
                if (sub->tag == "Array" && anyVisibleSetting(*sub, &m->children, current ? *current : std::string())) any = true;
            }
            if (!any) {
                ImGui::PopID();
                continue;
            }
            bool open = ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (open) {
                for (auto& sub : setting.children) {
                    if (sub->tag == "Array") drawSettings(*sub, locked, &m->children, current ? *current : std::string());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            continue;
        }
        float start = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", title.c_str());
        ImGui::SameLine(start + 150.0f * (ImGui::GetFontSize() / 15.0f));
        drawValue(setting, locked || disabled, m);
        for (auto& sub : setting.children) {
            if (sub->tag == "Array") {
                ImGui::Indent(20.0f);
                drawSettings(*sub, locked, m ? &m->children : nullptr, current ? *current : std::string());
                ImGui::Unindent(20.0f);
            }
        }
        ImGui::PopID();
    }
}

void Editor::drawValue(Node& setting, bool locked, const joue::SettingMeta* meta) {
    std::string& value = setting.attrRef("value");
    double dLow = 0, dHigh = 0;
    bool decimals = false;
    bool isRange = parseRangeD(value, dLow, dHigh, decimals);
    beginGrey(locked);
    if (meta && meta->type == joue::settingtype::Combobox && !meta->values.empty()) {
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##combo", joue::i18n::text(joue::fieldFor(*meta, value)).c_str())) {
            for (std::size_t i = 0; i < meta->values.size(); ++i) {
                bool selected = meta->values[i] == value;
                std::string label = joue::i18n::text(i < meta->fields.size() ? meta->fields[i] : meta->values[i]);
                if (label.compare(0, 2, "--") == 0) {
                    ImGui::TextDisabled("%s", label.c_str());
                    continue;
                }
                if (ImGui::Selectable(label.c_str(), selected)) {
                    value = meta->values[i];
                    markChanged();
                }
            }
            ImGui::EndCombo();
        }
    } else if (meta && meta->type == joue::settingtype::Checkbox) {
        bool checked = value == "1";
        if (ImGui::Checkbox(tr("##check"), &checked)) {
            value = checked ? "1" : "0";
            markChanged();
        }
    } else if (isRange && meta && meta->type == joue::settingtype::Slider) {
        // a slider keeps a placeholder before its value ("-10000;1024"): only the value is shown, from 0 to the limit
        int shown = static_cast<int>(dHigh), limit = meta->hasLimitMax ? meta->limitMax : 127;
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderInt(tr("##slider"), &shown, 0, limit)) {
            char buffer[96];
            std::snprintf(buffer, sizeof buffer, decimals ? "%.6f;%.6f" : "%.0f;%.0f", dLow, static_cast<double>(shown));
            value = buffer;
            markChanged();
        }
    } else if (isRange && !decimals && meta && meta->type == joue::settingtype::DoubleSlider && meta->hasLimitMax) {
        int lo = static_cast<int>(dLow), hi = static_cast<int>(dHigh);
        ImGui::SetNextItemWidth(180);
        if (ImGui::DragIntRange2("##range", &lo, &hi, 1.0f, meta->hasLimitMin ? meta->limitMin : 0, meta->limitMax)) {
            value = std::to_string(lo) + ";" + std::to_string(hi);
            markChanged();
        }
    } else if (isRange) {
        bool changed = false;
        ImGui::SetNextItemWidth(90);
        changed |= ImGui::InputDouble(tr("##low"), &dLow, 0, 0, decimals ? "%.6f" : "%.0f");
        ImGui::SameLine();
        ImGui::TextUnformatted(tr("to"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        changed |= ImGui::InputDouble(tr("##high"), &dHigh, 0, 0, decimals ? "%.6f" : "%.0f");
        if (changed) {
            if (meta && meta->hasLimitMax) { dLow = std::min<double>(dLow, meta->limitMax); dHigh = std::min<double>(dHigh, meta->limitMax); }
            if (meta && meta->hasLimitMin) { dLow = std::max<double>(dLow, meta->limitMin); dHigh = std::max<double>(dHigh, meta->limitMin); }
            char buffer[96];
            if (decimals) std::snprintf(buffer, sizeof buffer, "%.6f;%.6f", dLow, dHigh);
            else std::snprintf(buffer, sizeof buffer, "%.0f;%.0f", dLow, dHigh);
            value = buffer;
            markChanged();
        }
    } else {
        auto seen = observed_.find(setting.name());
        bool boolean = !meta && seen != observed_.end() && !seen->second.empty() &&
                       std::all_of(seen->second.begin(), seen->second.end(), [](const std::string& v) { return v == "0" || v == "1"; });
        if (boolean) {
            bool checked = value == "1";
            if (ImGui::Checkbox(tr("##check"), &checked)) {
                value = checked ? "1" : "0";
                markChanged();
            }
        } else {
            ImGui::SetNextItemWidth(220);
            if (ImGui::InputText(tr("##text"), &value)) markChanged();
            if (!meta && seen != observed_.end() && seen->second.size() > 1) {
                ImGui::SameLine(0, 2);
                if (ImGui::ArrowButton("##more", ImGuiDir_Down)) ImGui::OpenPopup("suggestions");
                if (ImGui::BeginPopup("suggestions")) {
                    for (const std::string& option : seen->second) {
                        if (ImGui::Selectable(option.c_str())) {
                            value = option;
                            markChanged();
                        }
                    }
                    ImGui::EndPopup();
                }
            }
        }
    }
    endGrey();
}

void Editor::drawPopups() {
    if (openPathPopup_) { ImGui::OpenPopup(tr("Open library")); openPathPopup_ = false; }
    if (exportPopup_) { ImGui::OpenPopup(tr("Export a copy")); exportPopup_ = false; }
    if (deletePopup_) { ImGui::OpenPopup(tr("Delete")); deletePopup_ = false; }
    if (quitPopup_) { ImGui::OpenPopup(tr("Unsaved changes")); quitPopup_ = false; }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(tr("Open library"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("Path of a library.xml file:"));
        ImGui::SetNextItemWidth(520);
        ImGui::InputText(tr("##path"), &pathInput_);
        if (ImGui::Button(tr("Open"))) { openFromPath(pathInput_); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(tr("Export a copy"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("Write a copy of the library to:"));
        ImGui::SetNextItemWidth(520);
        ImGui::InputText(tr("##export"), &pathInput_);
        if (ImGui::Button(tr("Export"))) { exportCopy(pathInput_); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(tr("Delete"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(tr("Delete \"%s\"? Your previous saved version stays in the backups folder."), selected_ ? selected_->name().c_str() : "this item");
        if (ImGui::Button(tr("Delete"))) { deleteSelected(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(tr("Unsaved changes"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("You have unsaved changes."));
        if (ImGui::Button(tr("Save and quit"))) { if (saveLibrary()) quit_ = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(tr("Quit without saving"))) { quit_ = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
