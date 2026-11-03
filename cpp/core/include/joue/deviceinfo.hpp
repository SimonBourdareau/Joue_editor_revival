#pragma once
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "joue/device.hpp"

namespace joue {

struct DeviceInfo {
    std::optional<std::uint32_t> hardwareId, touchHardwareId, firmwareVersion, touchVersion;
    std::optional<std::uint16_t> firmwareBuild, touchBuild;
    std::optional<Bytes> serial;
    std::array<std::optional<Bytes>, SlotCount> moduleSerials;
};

DeviceInfo readDeviceInfo(Device& device);

std::string versionText(std::uint32_t version);
std::string hardwareFamily(std::uint32_t hardwareId);

std::vector<std::pair<std::string, std::string>> describeDeviceInfo(const DeviceInfo& info);

}  // namespace joue
