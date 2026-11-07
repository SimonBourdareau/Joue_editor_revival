#include "joue/deviceinfo.hpp"

namespace joue {

namespace {

std::string hex(const Bytes& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) out += ' ';
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

std::string versionAndBuild(const std::optional<std::uint32_t>& version, const std::optional<std::uint16_t>& build) {
    if (!version) return "not answered";
    return versionText(*version) + ", build " + (build ? std::to_string(*build) : std::string("?")) + " (raw " + std::to_string(*version) + ")";
}

}  // namespace

DeviceInfo readDeviceInfo(Device& device) {
    DeviceInfo info;
    if (auto hardware = device.hardwareInfo()) {
        info.hardwareId = hardware->id;
        info.hardwareLimitations = hardware->limitations;
    }
    info.touchHardwareId = device.touchHardwareId();
    info.firmwareVersion = device.firmwareVersion();
    info.touchVersion = device.touchVersion();
    info.firmwareBuild = device.firmwareBuild();
    info.touchBuild = device.touchBuild();
    info.serial = device.serial();
    for (int slot = 1; slot <= SlotCount; ++slot) info.moduleSerials[static_cast<std::size_t>(slot - 1)] = device.moduleSerial(slot);
    return info;
}

std::string versionText(std::uint32_t version) { return std::to_string(version / 100) + "." + std::to_string(version % 100); }

bool isPlayBoard(std::uint32_t limitations) { return (limitations & LimitationPlayBoard) != 0; }

std::string hardwareFamily(std::uint32_t hardwareId) {
    switch (hardwareId) {
        case 2: return "v2";
        case 3:
        case 5: return "v3/v5";
        case 4: return "v4";
        default: return "unknown";
    }
}

std::vector<std::pair<std::string, std::string>> describeDeviceInfo(const DeviceInfo& info) {
    std::vector<std::pair<std::string, std::string>> lines;
    lines.push_back({"Hardware", info.hardwareId ? std::to_string(*info.hardwareId) + " (family " + hardwareFamily(*info.hardwareId) + ")" : std::string("not answered")});
    if (info.hardwareLimitations) {
        std::string text;
        if (isPlayBoard(*info.hardwareLimitations)) text = "Play board: its modules can be read but not changed";
        if (*info.hardwareLimitations & LimitationAccessories) text += std::string(text.empty() ? "" : "; ") + "accessories";
        lines.push_back({"Limitations", text.empty() ? std::string("none") : text});
    }
    lines.push_back({"Firmware", versionAndBuild(info.firmwareVersion, info.firmwareBuild)});
    std::string touch = versionAndBuild(info.touchVersion, info.touchBuild);
    if (info.touchHardwareId && info.touchVersion) touch += ", hardware " + std::to_string(*info.touchHardwareId);
    lines.push_back({"Touch processor", touch});
    lines.push_back({"Board serial", info.serial ? hex(*info.serial) : std::string("not answered")});
    for (int slot = 1; slot <= SlotCount; ++slot) {
        const auto& tag = info.moduleSerials[static_cast<std::size_t>(slot - 1)];
        lines.push_back({"Tag in slot " + std::to_string(slot), tag && !tag->empty() ? hex(*tag) : std::string("no tag")});
    }
    return lines;
}

}  // namespace joue
