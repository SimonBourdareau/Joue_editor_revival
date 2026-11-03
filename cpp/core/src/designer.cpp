#include "joue/designer.hpp"
#include "joue/i18n.hpp"

#include <algorithm>
#include <stdexcept>

#include "joue/moduleart.hpp"

namespace joue {

namespace {

std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

void put16(std::uint8_t* p, int value) {
    p[0] = static_cast<std::uint8_t>(value & 0xFF);
    p[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

const std::vector<FieldSpec> NoFields;

const std::vector<FieldSpec> PadFields = {{"Note", 16, 1, false, 0, 127}, {"Channel", 17, 1, false, 1, 16}, {"Velocity min", 18, 1, false, 0, 127}, {"Velocity max", 19, 1, false, 0, 127}};
const std::vector<FieldSpec> KeyFields = {{"Note", 16, 1, false, 0, 127}, {"Channel", 17, 1, false, 1, 16}, {"Velocity min", 18, 1, false, 0, 127}, {"Velocity max", 19, 1, false, 0, 127},
                                          {"Y min", 20, 2, true, -32768, 32767}, {"Y max", 22, 2, true, -32768, 32767}, {"Y origin", 24, 2, true, -32768, 32767}, {"Y control (CC)", 26, 1, false, 0, 127},
                                          {"Y control fine", 27, 1, false, 0, 127}};
const std::vector<FieldSpec> StripFields = {{"Control (CC)", 16, 1, false, 0, 127}, {"Channel", 17, 1, false, 1, 16}, {"Options", 18, 1, false, 0, 255}, {"Demultiplier", 19, 1, false, 0, 255},
                                            {"Min", 20, 2, true, -32768, 32767}, {"Max", 22, 2, true, -32768, 32767}, {"Control fine", 24, 1, false, 0, 127}};
const std::vector<FieldSpec> StringFields = {{"Note", 16, 1, false, 0, 127}, {"Channel", 17, 1, false, 1, 16}, {"Fret count", 18, 1, false, 0, 255}, {"Options", 19, 1, false, 0, 255},
                                             {"Velocity min", 20, 1, false, 0, 127}, {"Velocity max", 21, 1, false, 0, 127}};
const std::vector<FieldSpec> RoundFields = {{"Control (CC)", 16, 1, false, 0, 127}, {"Channel", 17, 1, false, 1, 16}, {"Demultiplier", 18, 1, false, 0, 255}, {"Control fine", 19, 1, false, 0, 127},
                                            {"Min", 20, 2, true, -32768, 32767}, {"Max", 22, 2, true, -32768, 32767}, {"Length", 24, 2, false, 0, 65535}, {"Options", 26, 1, false, 0, 255}};
const std::vector<FieldSpec> ControlFields = {{"Channel", 16, 1, false, 1, 16}, {"Control or pitch bend MSB", 17, 1, false, 0, 127}, {"Value or pitch bend LSB", 18, 1, false, 0, 127}, {"Options", 19, 1, false, 0, 255},
                                              {"Control fine", 20, 1, false, 0, 127}, {"Min", 24, 2, true, -32768, 32767}, {"Max", 26, 2, true, -32768, 32767}};
std::vector<FieldSpec> makeAreaFields() {
    std::vector<FieldSpec> fields;
    static const char* axes[3] = {"X", "Y", "Z"};
    static std::vector<std::string> names;
    names.reserve(64);
    struct Group { const char* label; int start; int size; bool isSigned; int low, high; };
    const Group groups[] = {{"control", 16, 1, false, 0, 127}, {"channel", 19, 1, false, 1, 16}, {"options", 22, 1, false, 0, 255}, {"demultiplier", 25, 1, false, 0, 255},
                            {"control fine", 28, 1, false, 0, 127}, {"origin", 31, 1, false, 0, 255}, {"min", 34, 2, true, -32768, 32767}, {"max", 40, 2, true, -32768, 32767}};
    for (const Group& g : groups) {
        for (int a = 0; a < 3; ++a) {
            names.push_back(std::string(axes[a]) + " " + g.label);
            fields.push_back({names.back().c_str(), g.start + a * g.size, g.size, g.isSigned, g.low, g.high});
        }
    }
    return fields;
}
const std::vector<FieldSpec> AreaFields = makeAreaFields();

Bytes defaultRecord(std::uint8_t type, int id, Box box) {
    ElementHeader h;
    h.type = type;
    h.id = static_cast<std::uint8_t>(id);
    h.x = static_cast<std::uint16_t>(box.x);
    h.y = static_cast<std::uint16_t>(box.y);
    h.width = static_cast<std::uint16_t>(box.w);
    h.height = static_cast<std::uint16_t>(box.h);
    Bytes body;
    switch (type) {
        case element::Pad:
            h.version = 1; h.layer = 3; h.depth = 40; h.options = 0x0001;
            body = {60, 1, 50, 127};
            break;
        case element::Key:
            h.version = 2; h.layer = 3; h.depth = 40; h.options = 0x0021;
            body = {60, 1, 50, 127, 0, 0, 127, 0, 0, 0, 74, 0};
            break;
        case element::Strip:
            h.version = 2; h.layer = 4; h.depth = 127; h.options = 0x0081;
            body = {1, 1, 9, 1, 0, 0, 127, 0, 0, 0, 0, 0};
            break;
        case element::String:
            h.version = 1; h.layer = 1; h.depth = 30; h.options = 0x0100;
            body = {40, 1, 8, 1, 50, 127, 0, 0};
            break;
        case element::Button:
            h.version = 1; h.layer = 4; h.depth = 40; h.options = 0x0081;
            body = {8, 0, 0, 0, '*', '.', '2', '^', '1', ';', 0, 0};
            break;
        case element::Area:
            h.version = 2; h.layer = 4; h.z = 20; h.depth = 80; h.options = 0x0089;
            body = {0, 0, 1, 0, 0, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0};
            break;
        default:
            return {};
    }
    Bytes record(ElementHeader::Length + body.size());
    writeElementHeader(h, record.data());
    std::copy(body.begin(), body.end(), record.begin() + ElementHeader::Length);
    return record;
}

}  // namespace

const std::vector<FieldSpec>& bodyFields(std::uint8_t type, std::uint8_t version) {
    switch (type) {
        case element::Pad: return version == 1 ? PadFields : NoFields;
        case element::Key: return KeyFields;
        case element::Strip: return version == 2 ? StripFields : NoFields;
        case element::String: return StringFields;
        case element::Round: return version == 2 ? RoundFields : NoFields;
        case element::Control: return version == 2 ? ControlFields : NoFields;
        case element::Area: return version == 2 ? AreaFields : NoFields;
        default: return NoFields;
    }
}

Design::Design(int slots) {
    blob_.header.type = 1;
    blob_.header.version = 3;
    blob_.header.color = 2;
    blob_.header.revision = 1;
    setSlots(slots);
}

Design Design::fromBytes(const Bytes& data) {
    Design design;
    design.blob_ = ModuleBlob::parse(data);
    design.slots_ = design.inferSlots();
    return design;
}

Bytes Design::toBytes() const {
    Bytes out = blob_.toBytes();
    // the checksum byte is the security byte XOR every byte from the element count on; the same rule holds on every real module
    std::uint8_t x = 0;
    for (std::size_t i = 8; i < out.size(); ++i) x ^= out[i];
    out[4] = static_cast<std::uint8_t>(out[3] ^ x);
    return out;
}

void Design::setSlots(int slots) { slots_ = std::max(1, std::min(3, slots)); }

int Design::inferSlots() const {
    int right = 0;
    for (std::size_t i = 0; i < count(); ++i) {
        if (hasBox(i)) right = std::max(right, box(i).x + box(i).w);
    }
    int fromElements = (right + SlotWidthMm - 1) / SlotWidthMm;
    ModuleArt art = moduleArt(blob_.header.type);
    int fromType = art.known ? static_cast<int>(art.widthMm / SlotWidthMm + 0.5f) : 1;
    return std::max(1, std::min(3, std::max(fromElements, fromType)));
}

bool Design::hasBox(std::size_t index) const { return index < count() && !blob_.elements[index].isScript() && blob_.elements[index].type() != element::PiePart; }

Box Design::box(std::size_t index) const {
    ElementHeader h = readElementHeader(blob_.elements.at(index).bytes.data());
    return {h.x, h.y, h.width, h.height};
}

void Design::setBox(std::size_t index, Box box) {
    if (!hasBox(index)) return;
    box.w = std::max(2, std::min(box.w, widthMm()));
    box.h = std::max(2, std::min(box.h, HeightMm));
    box.x = std::max(0, std::min(box.x, widthMm() - box.w));
    box.y = std::max(0, std::min(box.y, HeightMm - box.h));
    std::uint8_t* b = blob_.elements[index].bytes.data();
    put16(b + 4, box.x);
    put16(b + 6, box.w);
    put16(b + 8, box.y);
    put16(b + 10, box.h);
}

ElementHeader Design::header(std::size_t index) const { return readElementHeader(blob_.elements.at(index).bytes.data()); }

void Design::setHeader(std::size_t index, const ElementHeader& header) {
    if (!hasBox(index)) return;
    std::uint8_t* b = blob_.elements[index].bytes.data();
    b[3] = header.layer;
    b[12] = header.z;
    b[13] = header.depth;
    put16(b + 14, header.options);
}

int Design::field(std::size_t index, const FieldSpec& spec) const {
    const Bytes& bytes = blob_.elements.at(index).bytes;
    if (static_cast<std::size_t>(spec.offset + spec.size) > bytes.size()) return 0;
    if (spec.size == 1) return bytes[static_cast<std::size_t>(spec.offset)];
    int value = u16(bytes.data() + spec.offset);
    return spec.isSigned ? static_cast<std::int16_t>(value) : value;
}

void Design::setField(std::size_t index, const FieldSpec& spec, int value) {
    Bytes& bytes = blob_.elements.at(index).bytes;
    if (static_cast<std::size_t>(spec.offset + spec.size) > bytes.size()) return;
    value = std::max(spec.minimum, std::min(spec.maximum, value));
    if (spec.size == 1) bytes[static_cast<std::size_t>(spec.offset)] = static_cast<std::uint8_t>(value);
    else put16(bytes.data() + spec.offset, value);
}

int Design::nextId() const {
    int highest = 0;
    for (const ElementRecord& e : blob_.elements) {
        if (!e.isScript()) highest = std::max<int>(highest, e.bytes[2]);
    }
    return highest + 1;
}

std::size_t Design::add(std::uint8_t type, Box box) {
    if (count() >= static_cast<std::size_t>(MaxElements) || nextId() > 255) return count();
    PressureRange module = pressureRangeOfAll();
    Bytes record = defaultRecord(type, nextId(), box);
    if (record.empty()) return count();
    // the script, when there is one, stays last
    std::size_t at = count();
    if (hasScript()) at = count() - 1;
    blob_.elements.insert(blob_.elements.begin() + static_cast<std::ptrdiff_t>(at), ElementRecord{record});
    setBox(at, box);
    if (type == element::Button && module == PressureRange::High) {
        ElementHeader h = header(at);
        h.depth = 70;
        setHeader(at, h);
    }
    return at;
}

std::size_t Design::duplicate(std::size_t index) {
    if (!hasBox(index) || count() >= static_cast<std::size_t>(MaxElements) || nextId() > 255) return count();
    ElementRecord copy = blob_.elements[index];
    copy.bytes[2] = static_cast<std::uint8_t>(nextId());
    std::size_t at = hasScript() ? count() - 1 : count();
    blob_.elements.insert(blob_.elements.begin() + static_cast<std::ptrdiff_t>(at), copy);
    Box b = box(at);
    b.x += 4;
    b.y += 4;
    setBox(at, b);
    return at;
}

Box Design::bounds(const std::vector<std::size_t>& indices) const {
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (std::size_t index : indices) {
        if (!hasBox(index)) continue;
        Box b = box(index);
        x0 = std::min(x0, b.x);
        y0 = std::min(y0, b.y);
        x1 = std::max(x1, b.x + b.w);
        y1 = std::max(y1, b.y + b.h);
    }
    if (x1 < x0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

bool Design::moveGroup(const std::vector<std::size_t>& indices, int dx, int dy) {
    for (std::size_t index : indices) {
        if (!hasBox(index)) continue;
        Box b = box(index);
        if (b.x + dx < 0 || b.y + dy < 0 || b.x + dx + b.w > widthMm() || b.y + dy + b.h > HeightMm) return false;
    }
    for (std::size_t index : indices) {
        if (!hasBox(index)) continue;
        Box b = box(index);
        b.x += dx;
        b.y += dy;
        setBox(index, b);
    }
    return true;
}

bool Design::swapPositions(std::size_t a, std::size_t b) {
    if (a == b || !hasBox(a) || !hasBox(b)) return false;
    Box first = box(a), second = box(b);
    Box movedFirst{second.x, second.y, first.w, first.h}, movedSecond{first.x, first.y, second.w, second.h};
    for (const Box& m : {movedFirst, movedSecond}) {
        if (m.x < 0 || m.y < 0 || m.x + m.w > widthMm() || m.y + m.h > HeightMm) return false;
    }
    setBox(a, movedFirst);
    setBox(b, movedSecond);
    return true;
}

std::size_t Design::paste(const ElementRecord& record, int dx, int dy) {
    if (record.isScript() || count() >= static_cast<std::size_t>(MaxElements) || nextId() > 255) return count();
    ElementRecord copy = record;
    copy.bytes[2] = static_cast<std::uint8_t>(nextId());
    std::size_t at = hasScript() ? count() - 1 : count();
    blob_.elements.insert(blob_.elements.begin() + static_cast<std::ptrdiff_t>(at), copy);
    Box b = box(at);
    b.x += dx;
    b.y += dy;
    setBox(at, b);
    return at;
}

void Design::remove(std::size_t index) {
    if (index < count()) blob_.elements.erase(blob_.elements.begin() + static_cast<std::ptrdiff_t>(index));
}

namespace {

std::vector<std::size_t> withPlace(const Design& d, const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> out;
    for (std::size_t i : indices) {
        if (i < d.count() && d.hasBox(i)) out.push_back(i);
    }
    return out;
}

}  // namespace

void Design::alignX(const std::vector<std::size_t>& indices, Edge edge) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    if (picked.empty()) return;
    Box ref = picked.size() == 1 ? Box{0, 0, widthMm(), HeightMm} : bounds(picked);
    for (std::size_t i : picked) {
        Box b = box(i);
        b.x = edge == Edge::Low ? ref.x : edge == Edge::High ? ref.x + ref.w - b.w : ref.x + (ref.w - b.w) / 2;
        setBox(i, b);
    }
}

void Design::alignY(const std::vector<std::size_t>& indices, Edge edge) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    if (picked.empty()) return;
    Box ref = picked.size() == 1 ? Box{0, 0, widthMm(), HeightMm} : bounds(picked);
    for (std::size_t i : picked) {
        Box b = box(i);
        b.y = edge == Edge::Low ? ref.y : edge == Edge::High ? ref.y + ref.h - b.h : ref.y + (ref.h - b.h) / 2;
        setBox(i, b);
    }
}

bool Design::distributeX(const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    if (picked.size() < 3) return false;
    std::stable_sort(picked.begin(), picked.end(), [&](std::size_t a, std::size_t b) { return box(a).x < box(b).x; });
    Box first = box(picked.front()), last = box(picked.back());
    int widths = 0;
    for (std::size_t i : picked) widths += box(i).w;
    int gaps = (last.x + last.w - first.x) - widths;  // all the free space, shared between the n - 1 gaps
    int n = static_cast<int>(picked.size());
    int used = 0;
    for (int k = 0; k < n; ++k) {
        Box b = box(picked[static_cast<std::size_t>(k)]);
        b.x = first.x + used + gaps * k / (n - 1);
        setBox(picked[static_cast<std::size_t>(k)], b);
        used += b.w;
    }
    return true;
}

bool Design::distributeY(const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    if (picked.size() < 3) return false;
    std::stable_sort(picked.begin(), picked.end(), [&](std::size_t a, std::size_t b) { return box(a).y < box(b).y; });
    Box first = box(picked.front()), last = box(picked.back());
    int heights = 0;
    for (std::size_t i : picked) heights += box(i).h;
    int gaps = (last.y + last.h - first.y) - heights;
    int n = static_cast<int>(picked.size());
    int used = 0;
    for (int k = 0; k < n; ++k) {
        Box b = box(picked[static_cast<std::size_t>(k)]);
        b.y = first.y + used + gaps * k / (n - 1);
        setBox(picked[static_cast<std::size_t>(k)], b);
        used += b.h;
    }
    return true;
}

void Design::flipX(const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    if (picked.empty()) return;
    Box ref = bounds(picked);
    for (std::size_t i : picked) {
        Box b = box(i);
        b.x = 2 * ref.x + ref.w - b.x - b.w;
        setBox(i, b);
    }
}

void Design::flipY(const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    if (picked.empty()) return;
    Box ref = bounds(picked);
    for (std::size_t i : picked) {
        Box b = box(i);
        b.y = 2 * ref.y + ref.h - b.y - b.h;
        setBox(i, b);
    }
}

std::vector<std::size_t> Design::repeat(const std::vector<std::size_t>& indices, int copies, int dx, int dy, int noteStep) {
    std::vector<std::size_t> picked = withPlace(*this, indices);
    std::vector<std::size_t> created;
    if (picked.empty() || (dx == 0 && dy == 0)) return created;
    std::vector<ElementRecord> originals;
    for (std::size_t i : picked) originals.push_back(blob_.elements[i]);
    for (int k = 1; k <= copies; ++k) {
        bool fits = count() + picked.size() <= static_cast<std::size_t>(MaxElements);
        for (std::size_t i : picked) {
            Box b = box(i);
            fits = fits && b.x + k * dx >= 0 && b.y + k * dy >= 0 && b.x + b.w + k * dx <= widthMm() && b.y + b.h + k * dy <= HeightMm;
        }
        if (!fits) break;
        std::vector<std::size_t> copy;
        for (const ElementRecord& record : originals) {
            std::size_t added = paste(record, k * dx, k * dy);
            if (added >= count()) break;
            copy.push_back(added);
            ElementHeader h = header(added);
            const std::vector<FieldSpec>& fields = bodyFields(h.type, h.version);
            if (noteStep != 0 && !fields.empty() && std::string(fields[0].name) == "Note") {
                setField(added, fields[0], std::max(0, std::min(127, field(added, fields[0]) + k * noteStep)));
            }
        }
        if (copy.size() != originals.size() || toBytes().size() > MaxBlobBytes) {
            // not all of the copy could be made, or the blob would be too big: the copy is taken back
            for (auto it = copy.rbegin(); it != copy.rend(); ++it) remove(*it);
            break;
        }
        created.insert(created.end(), copy.begin(), copy.end());
    }
    return created;
}

std::size_t Design::addIsomorphicGrid(Box area, int rows, int columns, int gap, int firstNote, int columnInterval, int rowInterval, int channel, bool stagger) {
    rows = std::max(1, rows);
    columns = std::max(1, columns);
    int free = area.w - gap * (columns - 1);
    int cellW = stagger ? (2 * free) / (2 * columns + 1) : free / columns;
    int cellH = (area.h - gap * (rows - 1)) / rows;
    if (cellW < 2 || cellH < 2) return count();
    std::size_t last = count();
    for (int r = 0; r < rows; ++r) {
        int shift = stagger && (r % 2 == 1) ? (cellW + gap) / 2 : 0;
        for (int c = 0; c < columns; ++c) {
            std::size_t i = add(element::Pad, {area.x + shift + c * (cellW + gap), area.y + r * (cellH + gap), cellW, cellH});
            if (i == count()) return last;
            for (const FieldSpec& f : PadFields) {
                if (std::string(f.name) == "Note") setField(i, f, std::max(0, std::min(127, firstNote + c * columnInterval + r * rowInterval)));
                if (std::string(f.name) == "Channel") setField(i, f, channel);
            }
            last = i;
        }
    }
    return last;
}

std::size_t Design::addFretboard(Box area, const std::vector<int>& openNotes, int frets, int gap, int channel) {
    int strings = static_cast<int>(openNotes.size());
    if (strings < 1) return count();
    int stringH = (area.h - gap * (strings - 1)) / strings;
    if (stringH < 3 || area.w < 10) return count();
    std::size_t last = count();
    for (int s = 0; s < strings; ++s) {
        std::size_t i = add(element::String, {area.x, area.y + s * (stringH + gap), area.w, stringH});
        if (i == count()) return last;
        for (const FieldSpec& f : StringFields) {
            std::string name = f.name;
            if (name == "Note") setField(i, f, std::max(0, std::min(127, openNotes[static_cast<std::size_t>(s)])));
            if (name == "Channel") setField(i, f, channel);
            if (name == "Fret count") setField(i, f, frets);
        }
        last = i;
    }
    return last;
}

namespace {

bool depthLimits(const ElementHeader& h, int& low, int& high) {
    switch (h.type) {
        case element::Pad: low = 40; high = 70; return true;
        case element::Key: low = 40; high = 60; return true;
        case element::String: low = 30; high = 50; return true;
        case element::Button:
            if (h.depth != 40 && h.depth != 70) return false;
            low = 40;
            high = 70;
            return true;
        case element::Area:
            if (h.depth == 80 || h.depth == 107 || h.depth == 110) {
                low = 80;
                high = h.depth == 110 ? 110 : 107;
            } else {
                low = 40;
                high = 70;
            }
            return true;
        default: return false;
    }
}

std::vector<std::string> splitCommands(const std::string& script) {
    std::vector<std::string> commands;
    std::string current;
    for (char c : script) {
        if (c == ';') {
            if (!current.empty()) commands.push_back(current);
            current.clear();
        } else if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            current += c;
        }
    }
    if (!current.empty()) commands.push_back(current);
    return commands;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const FieldSpec* fieldNamed(std::uint8_t type, std::uint8_t version, const std::string& name) {
    for (const FieldSpec& f : bodyFields(type, version)) {
        if (name == f.name) return &f;
    }
    return nullptr;
}

std::string joinCommands(const std::vector<std::string>& commands) {
    std::string text;
    for (const std::string& command : commands) text += command + ";";
    return text;
}

bool isValueCommand(const std::string& command, const std::string& marker) {
    std::size_t at = command.rfind(marker);
    return at != std::string::npos && at + marker.size() < command.size() && command.find_first_not_of("0123456789", at + marker.size()) == std::string::npos;
}

constexpr std::uint16_t GlissandoOn = 0x80;
constexpr std::uint16_t YControlOn = 0x20;
constexpr std::uint16_t YControlRelative = 0x08;

}  // namespace

bool Design::hasPressureRange(std::size_t index) const {
    int low, high;
    return index < count() && hasBox(index) && depthLimits(header(index), low, high);
}

PressureRange Design::pressureRange(std::size_t index) const {
    int low, high;
    if (!hasPressureRange(index) || !depthLimits(header(index), low, high)) return PressureRange::Custom;
    int depth = header(index).depth;
    return depth == low ? PressureRange::Low : depth == high ? PressureRange::High : PressureRange::Custom;
}

void Design::setPressureRange(std::size_t index, PressureRange range) {
    int low, high;
    if (range == PressureRange::Custom || !hasPressureRange(index) || !depthLimits(header(index), low, high)) return;
    ElementHeader h = header(index);
    h.depth = static_cast<std::uint8_t>(range == PressureRange::Low ? low : high);
    setHeader(index, h);
}

PressureRange Design::pressureRangeOfAll() const {
    bool any = false;
    PressureRange common = PressureRange::Custom;
    for (std::size_t i = 0; i < count(); ++i) {
        if (!hasPressureRange(i)) continue;
        PressureRange r = pressureRange(i);
        if (r == PressureRange::Custom) return PressureRange::Custom;
        if (any && r != common) return PressureRange::Custom;
        common = r;
        any = true;
    }
    return any ? common : PressureRange::Custom;
}

void Design::setPressureRangeOfAll(PressureRange range) {
    for (std::size_t i = 0; i < count(); ++i) setPressureRange(i, range);
}

bool Design::mpe() const { return (blob_.header.options & 0x01) != 0; }

void Design::setMpe(bool on) {
    blob_.header.options = static_cast<std::uint8_t>(on ? (blob_.header.options | 0x01) : (blob_.header.options & ~0x01));
}

int Design::mpeChannels() const {
    switch (blob_.header.options & 0x30) {
        case 0x10: return 1;
        case 0x20: return 2;
        default: return 0;
    }
}

void Design::setMpeChannels(int zone) {
    std::uint8_t bits = zone == 1 ? 0x10 : zone == 2 ? 0x20 : 0x00;
    blob_.header.options = static_cast<std::uint8_t>((blob_.header.options & ~0x30) | bits);
}

AftertouchType Design::aftertouch() const {
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (endsWith(command, ".22=1")) return AftertouchType::Polyphonic;
        if (endsWith(command, ".23=1")) return AftertouchType::Channel;
    }
    return AftertouchType::Off;
}

void Design::setAftertouch(AftertouchType type) {
    if (aftertouch() == type) return;
    std::vector<std::string> kept;
    std::string prefix = "*";
    std::size_t at = 0;
    bool found = false;
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (endsWith(command, ".22=1") || endsWith(command, ".23=1")) {
            if (!found) {
                found = true;
                prefix = command.substr(0, command.size() - 5);
                at = kept.size();
            }
            continue;
        }
        kept.push_back(command);
    }
    if (!found) at = kept.size();
    if (type != AftertouchType::Off) kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(at), prefix + (type == AftertouchType::Polyphonic ? ".22=1" : ".23=1"));
    std::string text;
    for (const std::string& command : kept) text += command + ";";
    if (text.empty()) {
        if (hasScript()) removeScript();
    } else {
        setScript(text);
    }
}

bool Design::vibrato() const {
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (endsWith(command, ".2=1")) return true;
    }
    return false;
}

void Design::setVibrato(bool on) {
    if (vibrato() == on) return;
    std::vector<std::string> kept;
    std::string prefix = "*";
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (endsWith(command, ".2=1")) continue;
        if (isValueCommand(command, ".5=")) prefix = command.substr(0, command.rfind(".5="));
        kept.push_back(command);
    }
    if (on) kept.push_back(prefix + ".2=1");
    std::string text = joinCommands(kept);
    if (text.empty()) {
        if (hasScript()) removeScript();
    } else {
        setScript(text);
    }
}

bool Design::hasVibratoValue() const {
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (isValueCommand(command, ".5=")) return true;
    }
    return false;
}

int Design::vibratoValue() const {
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (isValueCommand(command, ".5=")) return std::atoi(command.c_str() + command.rfind(".5=") + 3);
    }
    return 0;
}

void Design::setVibratoValue(int value) {
    value = std::max(0, std::min(8192, value));
    std::vector<std::string> kept;
    std::string prefix = "*";
    std::size_t at = 0;
    bool found = false;
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (isValueCommand(command, ".5=")) {
            if (!found) {
                found = true;
                prefix = command.substr(0, command.rfind(".5="));
                at = kept.size();
            }
            continue;
        }
        kept.push_back(command);
    }
    if (!found) {
        for (const std::string& command : kept) {
            if (endsWith(command, ".2=1")) prefix = command.substr(0, command.size() - 4);
        }
        at = kept.size();
    }
    kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(at), prefix + ".5=" + std::to_string(value));
    setScript(joinCommands(kept));
}

bool Design::hasGlide() const {
    for (std::size_t i = 0; i < count(); ++i) {
        if (blob_.elements[i].type() == element::Glide && blob_.elements[i].bytes.size() >= 20) return true;
    }
    return false;
}

bool Design::glissando() const {
    if (!hasGlide()) return false;
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (command == "c14.4=1") return true;
    }
    return false;
}

void Design::setGlissando(bool on) {
    if (!hasGlide() || glissando() == on) return;
    for (std::size_t g = 0; g < count(); ++g) {
        const Bytes& glide = blob_.elements[g].bytes;
        if (blob_.elements[g].type() != element::Glide || glide.size() < 20) continue;
        std::size_t listed = std::min<std::size_t>(glide[17], glide.size() - 20);
        for (std::size_t k = 0; k < listed; ++k) {
            for (std::size_t i = 0; i < count(); ++i) {
                if (!hasBox(i) || header(i).id != glide[20 + k]) continue;
                ElementHeader h = header(i);
                h.options = static_cast<std::uint16_t>(on ? (h.options | GlissandoOn) : (h.options & ~GlissandoOn));
                setHeader(i, h);
            }
        }
    }
    std::vector<std::string> kept;
    std::size_t at = 0;
    bool found = false;
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (command == "c14.4=0" || command == "c14.4=1") {
            if (!found) {
                found = true;
                at = kept.size();
            }
            continue;
        }
        kept.push_back(command);
    }
    if (!found) at = kept.size();
    kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(at), on ? "c14.4=1" : "c14.4=0");
    setScript(joinCommands(kept));
}

int Design::glissandoRange() const {
    for (std::size_t i = 0; i < count(); ++i) {
        if (blob_.elements[i].type() == element::Glide && blob_.elements[i].bytes.size() >= 20) return blob_.elements[i].bytes[16];
    }
    return 0;
}

void Design::setGlissandoRange(int semitones) {
    for (std::size_t i = 0; i < count(); ++i) {
        if (blob_.elements[i].type() == element::Glide && blob_.elements[i].bytes.size() >= 20) blob_.elements[i].bytes[16] = static_cast<std::uint8_t>(std::max(1, std::min(96, semitones)));
    }
}

std::size_t Design::addGlide(int semitones) {
    std::vector<std::uint8_t> ids;
    std::vector<std::size_t> covered;
    for (std::size_t i = 0; i < count(); ++i) {
        std::uint8_t type = blob_.elements[i].type();
        if (hasBox(i) && (type == element::Pad || type == element::Key) && ids.size() < 250) {
            ids.push_back(header(i).id);
            covered.push_back(i);
        }
    }
    if (ids.empty()) return count();
    int range = hasGlide() ? glissandoRange() : std::max(1, std::min(96, semitones));
    bool wasOn = glissando();
    for (std::size_t i = 0; i < count(); ++i) {
        if (blob_.elements[i].type() == element::Glide) {
            blob_.elements.erase(blob_.elements.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    Box area = bounds(covered);
    std::size_t padded = (ids.size() + 3) / 4 * 4;
    ElementHeader h;
    h.type = element::Glide;
    h.version = 1;
    h.id = static_cast<std::uint8_t>(nextId());
    h.layer = 4;
    h.x = static_cast<std::uint16_t>(area.x);
    h.y = static_cast<std::uint16_t>(area.y);
    h.width = static_cast<std::uint16_t>(area.w);
    h.height = static_cast<std::uint16_t>(area.h);
    h.depth = 127;
    h.options = 0x0200;
    Bytes record(20 + padded, 0);
    writeElementHeader(h, record.data());
    record[16] = static_cast<std::uint8_t>(range);
    record[17] = static_cast<std::uint8_t>(ids.size());
    record[18] = static_cast<std::uint8_t>(padded);
    std::copy(ids.begin(), ids.end(), record.begin() + 20);
    std::size_t at = hasScript() ? count() - 1 : count();
    blob_.elements.insert(blob_.elements.begin() + static_cast<std::ptrdiff_t>(at), ElementRecord{record});
    if (wasOn) {
        std::vector<std::string> kept;
        for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
            if (command != "c14.4=1") kept.push_back(command);
        }
        setScript(joinCommands(kept));
        setGlissando(true);
    }
    return at;
}

void Design::removeGlide() {
    if (!hasGlide()) return;
    if (glissando()) setGlissando(false);
    std::vector<std::string> kept;
    for (const std::string& command : splitCommands(hasScript() ? script() : std::string())) {
        if (command != "c14.4=0" && command != "c14.4=1") kept.push_back(command);
    }
    std::string text = joinCommands(kept);
    if (text.empty()) {
        if (hasScript()) removeScript();
    } else {
        setScript(text);
    }
    for (std::size_t i = 0; i < count(); ++i) {
        if (blob_.elements[i].type() == element::Glide) {
            blob_.elements.erase(blob_.elements.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
}

bool Design::hasYControl(std::size_t index) const {
    if (index >= count() || !hasBox(index)) return false;
    ElementHeader h = header(index);
    return h.type == element::Key && h.version == 2 && blob_.elements[index].bytes.size() >= 28;
}

YControl Design::yControl(std::size_t index) const {
    YControl y;
    if (!hasYControl(index)) return y;
    ElementHeader h = header(index);
    y.enabled = (h.options & YControlOn) != 0;
    y.relative = (h.options & YControlRelative) != 0;
    const FieldSpec* cc = fieldNamed(h.type, h.version, "Y control (CC)");
    const FieldSpec* lo = fieldNamed(h.type, h.version, "Y min");
    const FieldSpec* hi = fieldNamed(h.type, h.version, "Y max");
    if (cc) y.cc = field(index, *cc);
    int a = lo ? field(index, *lo) : 0, b = hi ? field(index, *hi) : 127;
    y.inverted = a > b;
    y.low = std::min(a, b);
    y.high = std::max(a, b);
    return y;
}

void Design::setYControl(std::size_t index, const YControl& control) {
    if (!hasYControl(index)) return;
    ElementHeader h = header(index);
    h.options = static_cast<std::uint16_t>(control.enabled ? (h.options | YControlOn) : (h.options & ~YControlOn));
    h.options = static_cast<std::uint16_t>(control.relative ? (h.options | YControlRelative) : (h.options & ~YControlRelative));
    setHeader(index, h);
    const FieldSpec* cc = fieldNamed(h.type, h.version, "Y control (CC)");
    const FieldSpec* lo = fieldNamed(h.type, h.version, "Y min");
    const FieldSpec* hi = fieldNamed(h.type, h.version, "Y max");
    int low = std::min(control.low, control.high), high = std::max(control.low, control.high);
    if (cc) setField(index, *cc, std::max(0, std::min(127, control.cc)));
    if (lo) setField(index, *lo, control.inverted ? high : low);
    if (hi) setField(index, *hi, control.inverted ? low : high);
}

std::string buttonCommandFor(ButtonAction action, const std::string& target, int value) {
    switch (action) {
        case ButtonAction::ChannelSelection: return target + ".1=" + std::to_string(std::max(1, std::min(16, value))) + ";";
        case ButtonAction::Sustain: return target + ".21^1;";
        case ButtonAction::Vibrato: return target + ".2^1;";
        case ButtonAction::Bending: return target + ".3^1;";
        case ButtonAction::YControl: return target + ".20^32;";
        case ButtonAction::AftertouchChannel: return target + ".23^1;";
        case ButtonAction::AftertouchPolyphonic: return target + ".22^1;";
        case ButtonAction::Glissando: return target + ".20^128;c14.4^1;";
        case ButtonAction::Fretless: return target + ".11=24;" + target + ".24^1;";
        case ButtonAction::OctaveDown: return target + ".4-12;";
        case ButtonAction::OctaveUp: return target + ".4+12;";
        default: return {};
    }
}

ButtonSetting parseButtonCommand(const std::string& command) {
    ButtonSetting setting;
    setting.command = command;
    std::size_t dot = command.find('.');
    if (dot == std::string::npos || dot == 0) return setting;
    std::string target = command.substr(0, dot), rest = command.substr(dot);
    if (rest.size() > 4 && rest.compare(0, 3, ".1=") == 0 && rest.back() == ';') {
        std::string number = rest.substr(3, rest.size() - 4);
        if (number.find_first_not_of("0123456789") == std::string::npos && number.size() <= 2) {
            setting.action = ButtonAction::ChannelSelection;
            setting.target = target;
            setting.value = std::atoi(number.c_str());
            return setting;
        }
    }
    static const ButtonAction actions[] = {ButtonAction::Sustain, ButtonAction::Vibrato, ButtonAction::Bending, ButtonAction::YControl, ButtonAction::AftertouchChannel, ButtonAction::AftertouchPolyphonic, ButtonAction::Glissando, ButtonAction::Fretless, ButtonAction::OctaveDown, ButtonAction::OctaveUp};
    for (ButtonAction action : actions) {
        if (buttonCommandFor(action, target, 1) == command) {
            setting.action = action;
            setting.target = target;
            return setting;
        }
    }
    return setting;
}

std::string buttonSymbol(const ButtonSetting& setting) {
    switch (setting.action) {
        case ButtonAction::ChannelSelection: return "Ch " + std::to_string(setting.value);
        case ButtonAction::Sustain: return "Sus";
        case ButtonAction::Vibrato: return "Vib";
        case ButtonAction::Bending: return "Bend";
        case ButtonAction::YControl: return "Y";
        case ButtonAction::AftertouchChannel: return "AT";
        case ButtonAction::AftertouchPolyphonic: return "ATp";
        case ButtonAction::Glissando: return "Gliss";
        case ButtonAction::Fretless: return "Fret";
        case ButtonAction::OctaveDown: return "-12";
        case ButtonAction::OctaveUp: return "+12";
        default: return "...";
    }
}

bool Design::hasButton(std::size_t index) const {
    return index < count() && blob_.elements[index].type() == element::Button && blob_.elements[index].bytes.size() >= 20;
}

std::string Design::buttonCommand(std::size_t index) const {
    if (!hasButton(index)) return {};
    const Bytes& b = blob_.elements[index].bytes;
    std::string text(b.begin() + 20, b.end());
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
}

void Design::setButtonCommand(std::size_t index, const std::string& command) {
    if (!hasButton(index)) return;
    std::size_t padded = std::min<std::size_t>((command.size() + 3) / 4 * 4, 0xFFFC);
    Bytes& b = blob_.elements[index].bytes;
    Bytes record(b.begin(), b.begin() + 16);
    record.push_back(static_cast<std::uint8_t>(padded & 0xFF));
    record.push_back(static_cast<std::uint8_t>(padded >> 8));
    record.push_back(0);
    record.push_back(0);
    record.insert(record.end(), command.begin(), command.begin() + static_cast<std::ptrdiff_t>(std::min(command.size(), padded)));
    record.resize(20 + padded, 0);
    b = record;
}

ButtonSetting Design::buttonSetting(std::size_t index) const { return parseButtonCommand(buttonCommand(index)); }

void Design::setButtonSetting(std::size_t index, const ButtonSetting& setting) {
    if (!hasButton(index)) return;
    std::string command = setting.action == ButtonAction::Custom ? setting.command : buttonCommandFor(setting.action, setting.target.empty() ? std::string("*") : setting.target, setting.value);
    setButtonCommand(index, command);
    ElementHeader h = header(index);
    bool octave = setting.action == ButtonAction::OctaveDown || setting.action == ButtonAction::OctaveUp;
    if (octave) h.depth = 127;
    else if (h.depth == 127) h.depth = pressureRangeOfAll() == PressureRange::High ? 70 : 40;
    setHeader(index, h);
}

std::size_t Design::addPadGrid(Box area, int rows, int columns, int gap, int firstNote, int rowStep, int channel) {
    rows = std::max(1, rows);
    columns = std::max(1, columns);
    int cellW = (area.w - gap * (columns - 1)) / columns;
    int cellH = (area.h - gap * (rows - 1)) / rows;
    if (cellW < 2 || cellH < 2) return count();
    std::size_t last = count();
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            std::size_t i = add(element::Pad, {area.x + c * (cellW + gap), area.y + r * (cellH + gap), cellW, cellH});
            if (i == count()) return last;
            for (const FieldSpec& f : PadFields) {
                if (std::string(f.name) == "Note") setField(i, f, firstNote + r * rowStep + c);
                if (std::string(f.name) == "Channel") setField(i, f, channel);
            }
            last = i;
        }
    }
    return last;
}

std::size_t Design::addKeyboard(Box area, int firstNote, int whiteKeys, int channel) {
    whiteKeys = std::max(1, whiteKeys);
    static const int steps[7] = {2, 2, 1, 2, 2, 2, 1};  // semitones from a white key to the next, starting on C
    int whiteW = area.w / whiteKeys;
    if (whiteW < 3) return count();
    std::size_t last = count();
    auto setNote = [&](std::size_t i, int note) {
        for (const FieldSpec& f : PadFields) {
            if (std::string(f.name) == "Note") setField(i, f, note);
            if (std::string(f.name) == "Channel") setField(i, f, channel);
        }
    };
    int note = firstNote;
    // find where the first note sits in the C major pattern
    int offset = firstNote % 12, position = 0, acc = 0;
    while (position < 7 && acc < offset) acc += steps[position++];
    int whiteIndex = position;
    for (int k = 0; k < whiteKeys; ++k) {
        int x = area.x + k * whiteW;
        std::size_t i = add(element::Pad, {x, area.y, whiteW - 1, area.h});
        if (i == count()) return last;
        ElementHeader h = header(i);
        h.layer = 2;
        h.options = 0x00A9;
        setHeader(i, h);
        setNote(i, note);
        last = i;
        int step = steps[(whiteIndex + k) % 7];
        note += step;
    }
    note = firstNote;
    for (int k = 0; k < whiteKeys - 1; ++k) {
        int step = steps[(whiteIndex + k) % 7];
        if (step == 2) {
            int bx = area.x + (k + 1) * whiteW - whiteW * 3 / 10;
            std::size_t i = add(element::Pad, {bx, area.y + area.h * 11 / 20, whiteW * 6 / 10, area.h * 9 / 20});
            if (i == count()) return last;
            ElementHeader h = header(i);
            h.layer = 3;
            h.options = 0x00A9;
            setHeader(i, h);
            setNote(i, note + 1);
            last = i;
        }
        note += step;
    }
    return last;
}

bool Design::hasScript() const { return count() > 0 && blob_.elements.back().isScript(); }

std::string Design::script() const {
    if (!hasScript()) return {};
    const Bytes& b = blob_.elements.back().bytes;
    std::string text(b.begin() + 4, b.end());
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
}

void Design::setScript(const std::string& text) {
    // rounded up to four bytes with no terminator of its own, as the original editors write it (the real Pads module has 16 characters in 16)
    std::size_t padded = ((text.size() + 3) / 4) * 4;
    Bytes record(4 + padded, 0);
    record[0] = element::Script;
    record[1] = 1;
    put16(record.data() + 2, static_cast<int>(padded));
    std::copy(text.begin(), text.end(), record.begin() + 4);
    if (hasScript()) blob_.elements.back().bytes = record;
    else blob_.elements.push_back(ElementRecord{record});
}

void Design::removeScript() {
    if (hasScript()) blob_.elements.pop_back();
}

std::vector<std::string> Design::problems() const {
    std::vector<std::string> out;
    if (count() == 0) out.push_back("Note: the module has no elements yet.");
    for (std::size_t i = 0; i < count(); ++i) {
        if (hasButton(i) && buttonCommand(i).empty()) out.push_back("Note: " + i18n::text(elementName(element::Button)) + " " + std::to_string(header(i).id) + " has no command.");
    }
    if (count() > static_cast<std::size_t>(MaxElements)) out.push_back("Error: more than 255 elements.");
    std::size_t total = blob_.toBytes().size();
    if (total > MaxBlobBytes) out.push_back("Error: " + std::to_string(total) + " bytes is more than the 888 bytes the firmware accepts for a module.");
    std::vector<int> ids;
    for (std::size_t i = 0; i < count(); ++i) {
        if (!hasBox(i)) continue;
        ElementHeader h = header(i);
        std::string label = i18n::text(elementName(h.type)) + " " + std::to_string(h.id);
        if (std::find(ids.begin(), ids.end(), h.id) != ids.end()) out.push_back("Error: " + label + " repeats an element number.");
        ids.push_back(h.id);
        if (h.x + h.width > widthMm() || h.y + h.height > HeightMm) out.push_back("Error: " + label + " reaches beyond the surface.");
        if (h.width < 2 || h.height < 2) out.push_back("Error: " + label + " is smaller than 2 mm.");
        for (std::size_t j = i + 1; j < count(); ++j) {
            if (!hasBox(j)) continue;
            ElementHeader o = header(j);
            if (o.layer != h.layer) continue;
            bool overlap = h.x < o.x + o.width && o.x < h.x + h.width && h.y < o.y + o.height && o.y < h.y + h.height;
            if (overlap) out.push_back("Note: " + label + " overlaps " + i18n::text(elementName(o.type)) + " " + std::to_string(o.id) + " on the same layer, a touch there may be ambiguous.");
        }
    }
    for (std::size_t i = 0; i < count(); ++i) {
        const ElementRecord& e = blob_.elements[i];
        if (e.type() != element::Glide) continue;
        int listed = e.bytes[17];
        for (int k = 0; k < listed && 20 + k < static_cast<int>(e.bytes.size()); ++k) {
            int id = e.bytes[static_cast<std::size_t>(20 + k)];
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                out.push_back("Note: the glide lists element " + std::to_string(id) + " which does not exist.");
                break;
            }
        }
    }
    return out;
}

bool checksumHolds(const Bytes& blob) {
    if (blob.size() < 12) return false;
    std::uint8_t x = 0;
    for (std::size_t i = 8; i < blob.size(); ++i) x ^= blob[i];
    return blob[4] == static_cast<std::uint8_t>(blob[3] ^ x);
}

std::string noteName(int note) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) return std::to_string(note);
    return std::string(names[note % 12]) + std::to_string(note / 12 - 2);
}

}  // namespace joue
