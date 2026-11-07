#pragma once
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "joue/device.hpp"

namespace joue {

struct DeviceInfo {
    std::optional<std::uint32_t> hardwareId, hardwareLimitations, touchHardwareId, firmwareVersion, touchVersion;
    std::optional<std::uint16_t> firmwareBuild, touchBuild;
    std::optional<Bytes> serial;
    std::array<std::optional<Bytes>, SlotCount> moduleSerials;
};

DeviceInfo readDeviceInfo(Device& device);

std::string versionText(std::uint32_t version);
std::string hardwareFamily(std::uint32_t hardwareId);

// Bit 0 of the limitations is set on a Play board: it can be read but its modules cannot be changed (the manufacturer's own application calls
// a board without that bit a Pro).
constexpr std::uint32_t LimitationPlayBoard = 1, LimitationAccessories = 2;
bool isPlayBoard(std::uint32_t limitations);

std::vector<std::pair<std::string, std::string>> describeDeviceInfo(const DeviceInfo& info);

}  // namespace joue
