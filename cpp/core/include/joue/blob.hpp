#pragma once
#include <string>
#include <vector>

#include "joue/bytes.hpp"

namespace joue {

namespace element {
constexpr std::uint8_t Pad = 2, Strip = 3, Button = 4, Key = 5, String = 6, Area = 7, Round = 8, Script = 9, Control = 10, Glide = 14, PiePart = 16;
}

struct InstrumentHeader {
    std::uint8_t type = 0, version = 0, color = 0, security = 0, checksum = 0, revision = 0;
    std::uint8_t reserved0 = 0, reserved1 = 0, elementCount = 0, options = 0;
    std::uint16_t size = 0;
    static constexpr std::size_t Length = 12;
};

struct ElementHeader {
    std::uint8_t type = 0, version = 0, id = 0, layer = 0;
    std::uint16_t x = 0, width = 0, y = 0, height = 0;
    std::uint8_t z = 0, depth = 0;
    std::uint16_t options = 0;
    static constexpr std::size_t Length = 16;
};

InstrumentHeader readInstrumentHeader(const Bytes& data);
void writeInstrumentHeader(const InstrumentHeader& header, std::uint8_t* destination);
ElementHeader readElementHeader(const std::uint8_t* data);
void writeElementHeader(const ElementHeader& header, std::uint8_t* destination);

struct ElementRecord {
    Bytes bytes;
    std::uint8_t type() const { return bytes[0]; }
    std::uint8_t version() const { return bytes[1]; }
    bool isScript() const { return bytes[0] == element::Script; }
};

// Fixed bodies per (type, version); buttons, scripts and glides add a trailer sized by a field in the body.
// Throws std::runtime_error for unknown layouts or truncated data.
std::size_t recordLength(const std::uint8_t* data, std::size_t available);

struct ModuleBlob {
    InstrumentHeader header;
    std::vector<ElementRecord> elements;

    // Reads exactly the declared element count; bytes after the declared size (the device pads reads) are ignored.
    static ModuleBlob parse(const Bytes& data);
    Bytes toBytes() const;
};

std::string moduleName(int type);
std::string elementName(int type);
std::vector<std::string> describe(const Bytes& data);

}  // namespace joue
