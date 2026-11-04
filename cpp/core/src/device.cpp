#include "joue/device.hpp"

#include <cstdio>
#include <stdexcept>

namespace joue {

std::string paramName(const char* verb, int id) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%s%02d", verb, id);
    return buffer;
}

namespace {

int checkSlot(int slot) {
    if (slot < 1 || slot > SlotCount) throw std::out_of_range("Slots are numbered 1 to 3.");
    return slot;
}

bool isLetter(std::uint8_t value, char upper) { return value == static_cast<std::uint8_t>(upper) || value == static_cast<std::uint8_t>(upper + 32); }

bool digits(const Bytes& name, std::size_t at, int& value) {
    value = 0;
    for (std::size_t i = at; i < at + 2; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
        value = value * 10 + (name[i] - '0');
    }
    return true;
}

}  // namespace

std::optional<ModuleEvent> parseInfoMessage(const Bytes& name) {
    if (name.size() < 5 || !isLetter(name[0], 'I') || !isLetter(name[1], 'F') || !isLetter(name[2], 'O')) return std::nullopt;
    int id = 0;
    if (!digits(name, 3, id) || id < 21 || id > 26) return std::nullopt;
    ModuleEvent event;
    event.slot = id - 21 + 1;
    if (name.size() == 6) return event;
    if (name.size() != 14) return std::nullopt;
    if (!digits(name, 6, event.type) || !digits(name, 8, event.version) || !digits(name, 10, event.color) || !digits(name, 12, event.options)) {
        return std::nullopt;
    }
    event.inserted = true;
    return event;
}

std::optional<Bytes> Device::readAtLeast(const std::string& name, std::size_t length) {
    auto data = transfer_.read(name);
    if (data && data->size() >= length) return data;
    return std::nullopt;
}

std::optional<std::uint32_t> Device::readU32(int id) {
    auto data = readAtLeast(paramName("GET", id), 4);
    if (!data) return std::nullopt;
    const Bytes& b = *data;
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::optional<std::uint16_t> Device::readU16(int id) {
    auto data = readAtLeast(paramName("GET", id), 2);
    if (!data) return std::nullopt;
    return static_cast<std::uint16_t>((*data)[0] | ((*data)[1] << 8));
}

std::optional<Bytes> Device::serial() { return readAtLeast(paramName("GET", 3), SerialLength); }
std::optional<Bytes> Device::moduleSerial(int slot) { return readAtLeast(paramName("GET", 31 + checkSlot(slot) - 1), 7); }
std::optional<std::uint32_t> Device::firmwareVersion() { return readU32(0); }
std::optional<std::uint32_t> Device::touchVersion() { return readU32(7); }
std::optional<std::uint32_t> Device::hardwareId() { return readU32(8); }
std::optional<std::uint32_t> Device::touchHardwareId() { return readU32(6); }
std::optional<std::uint16_t> Device::firmwareBuild() { return readU16(1); }
std::optional<std::uint16_t> Device::touchBuild() { return readU16(2); }
std::optional<Bytes> Device::readModule(int slot) { return transfer_.read(paramName("GET", 11 + checkSlot(slot) - 1)); }
bool Device::writeModule(int slot, const Bytes& blob) { return transfer_.write(paramName("SET", 11 + checkSlot(slot) - 1), blob); }
bool Device::loadTemporarily(const Bytes& blob) { return transfer_.write(paramName("SET", 42), blob); }
bool Device::updateModuleList() { return transfer_.write(paramName("CMD", 9), {}); }

bool Device::setAxis(char axis, bool enabled) {
    int id = 0;
    switch (axis) {
        case 'x': case 'X': id = 3; break;
        case 'y': case 'Y': id = 4; break;
        case 'z': case 'Z': id = 5; break;
        default: throw std::invalid_argument("Axis must be X, Y or Z.");
    }
    return transfer_.write(paramName("CMD", id), Bytes{static_cast<std::uint8_t>(enabled ? 1 : 0)});
}

bool Device::setRawMatrix(bool enabled) { return transfer_.write(paramName("CMD", 6), Bytes{static_cast<std::uint8_t>(enabled ? 1 : 0)}); }

// Unlocks every colour option: the device expects the XOR of its own serial number bytes.
bool Device::requestFullPrivileges() {
    auto id = serial();
    if (!id) return false;
    std::uint8_t checksum = 0;
    for (std::size_t i = 0; i < SerialLength; ++i) checksum ^= (*id)[i];
    return transfer_.write(paramName("CMD", 11), Bytes{checksum});
}

}  // namespace joue
