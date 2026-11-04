#pragma once
#include <optional>

#include "joue/transfer.hpp"

namespace joue {

constexpr int SlotCount = 3;  // the instrument has three slots
constexpr std::size_t SerialLength = 12;

struct ModuleEvent {
    int slot = 0;
    bool inserted = false;
    int type = 0, version = 0, color = 0, options = 0;
};

// Slot change announcements: a 6-byte name when a module is removed, a 14-byte name with four 2-digit fields when inserted.
std::optional<ModuleEvent> parseInfoMessage(const Bytes& name);

std::string paramName(const char* verb, int id);

class Device {
public:
    explicit Device(TransferClient& transfer) : transfer_(transfer) {}

    std::optional<Bytes> serial();
    std::optional<Bytes> moduleSerial(int slot);
    std::optional<std::uint32_t> firmwareVersion();
    std::optional<std::uint32_t> touchVersion();
    std::optional<std::uint32_t> hardwareId();
    std::optional<std::uint32_t> touchHardwareId();
    std::optional<std::uint16_t> firmwareBuild();
    std::optional<std::uint16_t> touchBuild();
    std::optional<Bytes> readModule(int slot);
    bool writeModule(int slot, const Bytes& blob);
    // The "update slot" request of the official J-Play application: the board builds the instrument of a slot from the blob without
    // touching any tag, so nothing is written to a module and nothing has to be on the device. The slot is not named in the request: it is
    // the one of the last insert or removal, and slot 1 after a reset. The layout is lost at a reset or at the next tag event there.
    bool loadTemporarily(const Bytes& blob);
    bool updateModuleList();
    bool setAxis(char axis, bool enabled);
    bool setRawMatrix(bool enabled);
    bool requestFullPrivileges();

private:
    std::optional<Bytes> readAtLeast(const std::string& name, std::size_t length);
    std::optional<std::uint32_t> readU32(int id);
    std::optional<std::uint16_t> readU16(int id);

    TransferClient& transfer_;
};

}  // namespace joue
