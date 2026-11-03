#include "joue/blob.hpp"

#include <cstdio>
#include <stdexcept>

namespace joue {

namespace {
std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
void put16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v & 0xFF); p[1] = static_cast<std::uint8_t>(v >> 8); }
}  // namespace

InstrumentHeader readInstrumentHeader(const Bytes& data) {
    if (data.size() < InstrumentHeader::Length) throw std::runtime_error("Module blob shorter than its header.");
    InstrumentHeader h;
    h.type = data[0]; h.version = data[1]; h.color = data[2]; h.security = data[3]; h.checksum = data[4]; h.revision = data[5];
    h.reserved0 = data[6]; h.reserved1 = data[7]; h.elementCount = data[8]; h.options = data[9]; h.size = u16(&data[10]);
    return h;
}

void writeInstrumentHeader(const InstrumentHeader& h, std::uint8_t* d) {
    d[0] = h.type; d[1] = h.version; d[2] = h.color; d[3] = h.security; d[4] = h.checksum; d[5] = h.revision;
    d[6] = h.reserved0; d[7] = h.reserved1; d[8] = h.elementCount; d[9] = h.options; put16(d + 10, h.size);
}

ElementHeader readElementHeader(const std::uint8_t* d) {
    ElementHeader h;
    h.type = d[0]; h.version = d[1]; h.id = d[2]; h.layer = d[3];
    h.x = u16(d + 4); h.width = u16(d + 6); h.y = u16(d + 8); h.height = u16(d + 10);
    h.z = d[12]; h.depth = d[13]; h.options = u16(d + 14);
    return h;
}

void writeElementHeader(const ElementHeader& h, std::uint8_t* d) {
    d[0] = h.type; d[1] = h.version; d[2] = h.id; d[3] = h.layer;
    put16(d + 4, h.x); put16(d + 6, h.width); put16(d + 8, h.y); put16(d + 10, h.height);
    d[12] = h.z; d[13] = h.depth; put16(d + 14, h.options);
}

std::size_t recordLength(const std::uint8_t* data, std::size_t available) {
    if (available < 2) throw std::runtime_error("Element record too short.");
    std::uint8_t type = data[0], version = data[1];
    auto unsupported = [&]() { return std::runtime_error("Element type " + std::to_string(type) + " version " + std::to_string(version) + " is not supported."); };
    if (type == element::Script) {
        if (available < 4) throw std::runtime_error("Element record truncated.");
        if (version != 1) throw unsupported();
        return 4 + u16(data + 2);
    }
    if (type == element::PiePart) {  // the petals and ring of Play Audio: 56 bytes, a layout of their own
        if (version != 2) throw unsupported();
        if (available < 56) throw std::runtime_error("Element record truncated.");
        return 56;
    }
    std::size_t fixed = 0;
    switch (type) {
        case element::Pad: if (version != 1) throw unsupported(); fixed = 16 + 4; break;
        case element::Strip: if (version == 1) fixed = 16 + 8; else if (version == 2) fixed = 16 + 12; else throw unsupported(); break;
        case element::Key: if (version != 1 && version != 2) throw unsupported(); fixed = 16 + 12; break;
        case element::String: if (version != 1) throw unsupported(); fixed = 16 + 8; break;
        case element::Area: if (version == 1) fixed = 16 + 16; else if (version == 2) fixed = 16 + 32; else throw unsupported(); break;
        case element::Round: if (version == 1) fixed = 16 + 8; else if (version == 2) fixed = 16 + 12; else throw unsupported(); break;
        case element::Control: if (version == 1) fixed = 16 + 4; else if (version == 2) fixed = 16 + 12; else throw unsupported(); break;
        case element::Button: if (version != 1) throw unsupported(); fixed = 16 + 4; break;
        case element::Glide: if (version != 1) throw unsupported(); fixed = 16 + 4; break;
        default: throw unsupported();
    }
    if (available < fixed) throw std::runtime_error("Element record truncated.");
    if (type == element::Button) return fixed + u16(data + 16);
    if (type == element::Glide) return fixed + data[18];
    return fixed;
}

ModuleBlob ModuleBlob::parse(const Bytes& data) {
    ModuleBlob blob;
    blob.header = readInstrumentHeader(data);
    if (blob.header.size < InstrumentHeader::Length || blob.header.size > data.size()) throw std::runtime_error("Declared module size is inconsistent with the data.");
    std::size_t position = InstrumentHeader::Length;
    for (int i = 0; i < blob.header.elementCount; ++i) {
        std::size_t length = recordLength(data.data() + position, blob.header.size - position);
        if (position + length > blob.header.size) throw std::runtime_error("Element record truncated.");
        blob.elements.push_back(ElementRecord{Bytes(data.begin() + position, data.begin() + position + length)});
        position += length;
    }
    if (position != blob.header.size) throw std::runtime_error("Unused bytes inside the declared module size.");
    return blob;
}

Bytes ModuleBlob::toBytes() const {
    std::size_t total = InstrumentHeader::Length;
    for (const auto& e : elements) total += e.bytes.size();
    if (total > 0xFFFF || elements.size() > 255) throw std::runtime_error("Module too large for the blob format.");
    Bytes out(total);
    InstrumentHeader h = header;
    h.elementCount = static_cast<std::uint8_t>(elements.size());
    h.size = static_cast<std::uint16_t>(total);
    writeInstrumentHeader(h, out.data());
    std::size_t position = InstrumentHeader::Length;
    for (const auto& e : elements) {
        std::copy(e.bytes.begin(), e.bytes.end(), out.begin() + position);
        position += e.bytes.size();
    }
    return out;
}

std::string moduleName(int type) {
    switch (type) {
        case 1: return "Area"; case 3: return "Bubbles"; case 5: return "Pads"; case 6: return "Fretboard"; case 7: return "Grand Fretboard";
        case 9: return "Strips"; case 10: return "Rounds"; case 12: return "Synth"; case 14: return "Scaler"; case 17: return "Grand Clavier";
        case 22: return "Play Piano"; case 23: return "Play Keys"; case 24: return "Play Guitar"; case 25: return "Play Drum"; case 27: return "Play Audio";
        case 2: return "Area Control"; case 4: return "Big Pads"; case 8: return "Strings"; case 11: return "Pads 3x3"; case 13: return "Keyboard";
        case 15: return "Multipoint"; case 16: return "Sticks Pads"; case 18: return "UVI Walker"; case 19: return "Pads Rack"; case 20: return "Strumming"; case 21: return "Big Perc";
        default: return "Unknown (" + std::to_string(type) + ")";
    }
}

std::string elementName(int type) {
    switch (type) {
        case element::Pad: return "Pad"; case element::Strip: return "Strip"; case element::Button: return "Button"; case element::Key: return "Key";
        case element::String: return "String"; case element::Area: return "Area"; case element::Round: return "Round"; case element::Script: return "Script";
        case element::Control: return "Control"; case element::Glide: return "Glide"; case element::PiePart: return "Pie part";
        default: return "Unknown (" + std::to_string(type) + ")";
    }
}

static std::string text(const std::uint8_t* begin, const std::uint8_t* end) {
    std::string s(begin, end);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::vector<std::string> describe(const Bytes& data) {
    std::vector<std::string> lines;
    ModuleBlob blob;
    try {
        blob = ModuleBlob::parse(data);
    } catch (const std::runtime_error& error) {
        lines.push_back(std::string("Could not decode the module blob: ") + error.what());
        return lines;
    }
    char buffer[512];
    const InstrumentHeader& h = blob.header;
    std::snprintf(buffer, sizeof buffer, "module type %d (%s), version %d, revision %d, color %d, security %d, checksum byte %02X, options %02X, declared size %d, %d elements, reserved %02X%02X",
                  h.type, moduleName(h.type).c_str(), h.version, h.revision, h.color, h.security, h.checksum, h.options, h.size, h.elementCount, h.reserved0, h.reserved1);
    lines.push_back(buffer);
    int index = 0;
    for (const auto& record : blob.elements) {
        ++index;
        std::string name = elementName(record.type());
        if (record.isScript()) {
            std::snprintf(buffer, sizeof buffer, "  %2d. %s v%d text \"", index, name.c_str(), record.version());
            lines.push_back(std::string(buffer) + text(record.bytes.data() + 4, record.bytes.data() + record.bytes.size()) + "\"");
            continue;
        }
        ElementHeader e = readElementHeader(record.bytes.data());
        Bytes body(record.bytes.begin() + ElementHeader::Length, record.bytes.end());
        std::snprintf(buffer, sizeof buffer, "  %2d. %s v%d id %d layer %d at (%d,%d) size %dx%d z %d depth %d options %04X body ", index, name.c_str(), record.version(), e.id, e.layer, e.x, e.y, e.width, e.height, e.z, e.depth, e.options);
        std::string line = std::string(buffer) + toHex(body);
        if (record.type() == element::Button) {
            line += " text \"" + text(record.bytes.data() + ElementHeader::Length + 4, record.bytes.data() + record.bytes.size()) + "\"";
        }
        lines.push_back(line);
    }
    return lines;
}

}  // namespace joue
