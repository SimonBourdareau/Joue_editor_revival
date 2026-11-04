#include "joue/modulewrite.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

#include "joue/designer.hpp"
#include "joue/instrumentmodule.hpp"

namespace joue {

namespace {

PushReport failure(const std::string& message) {
    PushReport report;
    report.message = message;
    return report;
}

}  // namespace

PushReport pushLayout(Device& device, TransferClient& client, int slot, const Bytes& original, const Bytes& edited, const std::function<void(int)>& pause) {
    if (original.size() < InstrumentHeader::Length || edited.size() < InstrumentHeader::Length) return failure("There is no layout to write.");
    try {
        Design design = Design::fromBytes(edited);
        design.setSlots(Design::fromBytes(original).slots());  // the layout has to fit the module that is there, not grow it
        for (const std::string& line : design.problems()) {
            if (line.rfind("Error", 0) == 0) return failure("Not written: the layout has an error.\nWhy: " + line.substr(7) + "\nWhat to do: fix it in the Check section of the designer, then try again.");
        }
    } catch (const std::exception&) {
        return failure("Not written: the edited layout is not a valid module blob.");
    }
    if (edited.size() > Design::MaxBlobBytes) return failure("Not written: the layout is larger than the 888 bytes a module holds.");

    // the layout must be for the very type and version of the module that is there; the type byte is never rewritten to make it fit
    if (edited[0] != original[0] || edited[1] != original[1]) {
        return failure("Not written: this layout is for a " + moduleName(edited[0]) + " module (type " + std::to_string(edited[0]) + ", version " + std::to_string(edited[1]) + "), and the module in the slot is a " + moduleName(original[0]) + " (type " + std::to_string(original[0]) + ", version " + std::to_string(original[1]) + ").");
    }

    Bytes toWrite = edited;
    std::copy(original.begin(), original.begin() + 4, toWrite.begin());
    // the checksum byte follows the rule the board uses (security byte XOR everything from the element count on)
    std::uint8_t x = 0;
    for (std::size_t i = 8; i < toWrite.size(); ++i) x ^= toWrite[i];
    toWrite[4] = static_cast<std::uint8_t>(toWrite[3] ^ x);

    client.setTimeout(std::chrono::milliseconds(2500));
    auto current = device.readModule(slot);
    if (!current || current->size() < InstrumentHeader::Length || !std::equal(original.begin(), original.begin() + 4, current->begin())) {
        return failure("Not written: the module in slot " + std::to_string(slot) + " is not the one this layout was read from.");
    }
    if (!device.writeModule(slot, toWrite)) return failure("The instrument did not accept the write. Nothing is known to have changed; check the module with the live view.");
    pause(1500);

    Bytes back;
    for (int attempt = 0; attempt < 3 && back.empty(); ++attempt) {
        auto read = device.readModule(slot);
        if (read && read->size() == toWrite.size()) back = *read;
        else pause(700);
    }
    PushReport report;
    report.readBack = back;
    if (back.empty()) {
        report.message = "Written, but the module could not be read back to check it. Look at it in the live view; the original can be restored.";
        return report;
    }
    for (std::size_t i = 0; i < toWrite.size(); ++i) {
        if (i == 3 || i == 4) continue;
        if (back[i] != toWrite[i]) {
            report.message = "Written, but the read-back differs from what was written (first at byte " + std::to_string(i) + "). The original can be restored.";
            return report;
        }
    }
    report.ok = true;
    report.message = "Written and read back: the module holds the new layout.";
    return report;
}

PushReport writeBlankTag(Device& device, TransferClient& client, int slot, const Bytes& layout, bool slotHoldsModule, const std::function<void(int)>& pause) {
    if (layout.size() < InstrumentHeader::Length) return failure("There is no layout to write.");
    if (slotHoldsModule) {
        return failure("Not written: slot " + std::to_string(slot) + " already holds a module.\nWhy: the board reports a module in this slot, and writing here would overwrite its layout.\nWhat to do: a layout is written to a blank tag only. To change the settings of this module, use Edit on it in the live window. To write a new layout, take the module out of the slot and lay a blank tag there.");
    }
    try {
        Design design = Design::fromBytes(layout);
        for (const std::string& line : design.problems()) {
            if (line.rfind("Error", 0) == 0) return failure("Not written: the layout has an error.\nWhy: " + line.substr(7) + "\nWhat to do: fix it in the Check section of the designer, then try again.");
        }
    } catch (const std::exception&) {
        return failure("Not written: the layout is not a valid module blob.");
    }
    if (layout.size() > Design::MaxBlobBytes) return failure("Not written: the layout is larger than the 888 bytes a tag holds.\nWhat to do: remove some elements, then try again.");
    if (layout[0] == 0) return failure("Not written: the layout has no module type.\nWhat to do: choose a Type in the Module section of the designer, then try again.");

    client.setTimeout(std::chrono::milliseconds(2500));
    auto id = device.moduleSerial(slot);
    bool tagSeen = id && id->size() >= 7 && std::any_of(id->begin(), id->begin() + 7, [](std::uint8_t b) { return b != 0; });
    if (!tagSeen) return failure("Not written: no tag is detected in slot " + std::to_string(slot) + ".\nWhy: the board finds no tag where the tag of a module would be.\nWhat to do: lay the blank tag flat on the instrument, where the tag of a module would be in that slot, and try again.");

    std::optional<Bytes> before;
    for (int attempt = 0; attempt < 2 && !before; ++attempt) before = device.readModule(slot);
    if (before) {
        const std::string another = "\nIf other modules are in the other slots, take them out and try again: the board can answer for another slot.";
        switch (classifyTag(*before)) {
            case TagKind::Empty: break;
            case TagKind::Manufacturer: return failure("Not written: this tag cannot be overwritten.\nWhy: it holds a manufacturer layout (" + moduleName((*before)[0]) + "). The program never overwrites a manufacturer's layout, because it could not be put back.\nWhat to do: to change the settings of this module, use Edit on it in the live window; to write your own layout, use a blank tag." + another);
            case TagKind::Custom: return failure("Not written: this tag already holds a layout (" + moduleName((*before)[0]) + ").\nWhy: writing over it would lose that layout.\nWhat to do: erase the tag first with Erase a tag (allowed, because it is not a manufacturer's layout), then write again." + another);
            case TagKind::Unknown: return failure("Not written: this tag holds a layout that cannot be told from a manufacturer's (type " + std::to_string((*before)[0]) + ").\nWhy: it may be a manufacturer's layout, and those are never overwritten.\nWhat to do: use a blank tag." + another);
        }
    }

    Bytes toWrite = layout;
    std::uint8_t security = 0;
    for (std::size_t i = 0; i < 7; ++i) security ^= (*id)[i];
    toWrite[3] = security;
    std::uint8_t x = 0;
    for (std::size_t i = 8; i < toWrite.size(); ++i) x ^= toWrite[i];
    toWrite[4] = static_cast<std::uint8_t>(security ^ x);

    if (!device.requestFullPrivileges()) return failure("Not written: the board did not give the full privileges.\nWhy: the first page of a tag, which holds its type and its security byte, can only be written with them.\nWhat to do: nothing was changed. Try again; if it keeps failing, take the other modules out of the other slots.");
    if (!device.writeModule(slot, toWrite)) return failure("The instrument did not accept the write. Nothing is known to have changed; look at the slot in the live view.");
    pause(1500);

    Bytes back;
    for (int attempt = 0; attempt < 3 && back.empty(); ++attempt) {
        auto read = device.readModule(slot);
        if (read && read->size() == toWrite.size()) back = *read;
        else pause(700);
    }
    PushReport report;
    report.readBack = back;
    if (back.empty()) {
        report.message = "Written, but the slot could not be read back to check it. Look at it in the live view: the module should appear.";
        return report;
    }
    for (std::size_t i = 0; i < toWrite.size(); ++i) {
        if (i == 4) continue;
        if (back[i] != toWrite[i]) {
            report.message = "Written, but the read-back differs from what was written (first at byte " + std::to_string(i) + "). If other modules are in the other slots, take them out and read the slot again: the board can answer for the wrong slot.";
            return report;
        }
    }
    report.ok = true;
    report.message = "Written and read back: the tag in slot " + std::to_string(slot) + " now holds the layout.";
    return report;
}

bool looksLikeNewTag(const Bytes& blob) {
    if (blob.size() < 3 || blob[0] != 0x03) return false;
    std::size_t length = blob[1];
    bool emptyRecord = length == 3 && blob.size() > 5 && blob[2] == 0xD0 && blob[3] == 0x00 && blob[4] == 0x00;
    if (length != 0 && !emptyRecord) return false;
    std::size_t end = 2 + length;
    if (blob.size() <= end || blob[end] != 0xFE) return false;
    return std::all_of(blob.begin() + static_cast<std::ptrdiff_t>(end) + 1, blob.end(), [](std::uint8_t b) { return b == 0; });
}

bool announcementMayBeNewTag(int type) { return type == 3; }

TagKind classifyTag(const Bytes& blob) {
    if (blob.size() < InstrumentHeader::Length || blob[0] == 0 || looksLikeNewTag(blob)) return TagKind::Empty;
    const ModuleBindings* bindings = moduleBindings(blob[0]);
    if (!bindings) return TagKind::Unknown;
    try {
        ModuleBlob::parse(blob);
    } catch (const std::exception&) {
        return TagKind::Unknown;
    }
    return structureMatches(*bindings, blob) ? TagKind::Manufacturer : TagKind::Custom;
}

PushReport blankTag(Device& device, TransferClient& client, int slot, int announcedType, const std::function<void(int)>& pause) {
    client.setTimeout(std::chrono::milliseconds(2500));
    auto id = device.moduleSerial(slot);
    bool tagSeen = id && id->size() >= 7 && std::any_of(id->begin(), id->begin() + 7, [](std::uint8_t b) { return b != 0; });
    if (!tagSeen) return failure("Not erased: no tag is detected in slot " + std::to_string(slot) + ".\nWhy: the board finds no tag in this slot.\nWhat to do: check that the tag is lying flat on the instrument, where the tag of a module would be.");
    if (announcedType < 0) return failure("Not erased: the board has announced nothing for slot " + std::to_string(slot) + ".\nWhy: without the board's announcement the program cannot check that what it reads is the tag of this slot.\nWhat to do: press Rescan modules in the live window, then try again.");

    auto first = device.readModule(slot);
    pause(300);
    auto second = device.readModule(slot);
    if (!first || !second || *first != *second) return failure("Not erased: the tag could not be read twice with the same result.\nWhy: the program erases only what it has read the same way twice.\nWhat to do: take the other modules out of the other slots and try again.");
    const Bytes& layout = *first;
    if (!layout.empty() && announcedType != 0 && static_cast<int>(layout[0]) != announcedType) {
        return failure("Not erased: the board announced a " + moduleName(announcedType) + " in slot " + std::to_string(slot) + " but the read gave a " + moduleName(layout[0]) + ".\nWhy: the answer may be for another slot.\nWhat to do: take the other modules out of the other slots and try again.");
    }
    switch (classifyTag(layout)) {
        case TagKind::Empty: {
            PushReport report;
            report.ok = true;
            report.message = "The tag in slot " + std::to_string(slot) + " is already blank.";
            return report;
        }
        case TagKind::Manufacturer: return failure("Not erased: this tag cannot be erased.\nWhy: it holds a manufacturer layout (" + moduleName(layout[0]) + "). The program never erases a manufacturer's layout, because it could not be put back.\nWhat to do: nothing. To change the settings of this module, use Edit on it in the live window.");
        case TagKind::Unknown: return failure("Not erased: this tag cannot be erased.\nWhy: its layout cannot be told from a manufacturer's (type " + std::to_string(layout[0]) + "), and a manufacturer's layout is never erased.\nWhat to do: nothing.");
        case TagKind::Custom: break;
    }
    if (!device.requestFullPrivileges()) return failure("Not erased: the board did not give the full privileges.\nWhy: erasing the first page of a tag needs them.\nWhat to do: nothing was changed. Try again.");
    Bytes zeros(Design::MaxBlobBytes, 0);
    if (!device.writeModule(slot, zeros)) return failure("The instrument did not accept the write. Nothing is known to have changed.");
    pause(1500);

    PushReport report;
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto back = device.readModule(slot);
        if (!back) {
            pause(700);
            continue;
        }
        report.readBack = *back;
        break;
    }
    if (report.readBack.empty() && !device.readModule(slot)) {
        report.message = "Written, but the slot could not be read back to check it. Look at it in the live view.";
        return report;
    }
    // blank means zeros all through, except the security and checksum bytes (3 and 4), which the board computes itself on every write
    bool blank = classifyTag(report.readBack) == TagKind::Empty;
    for (std::size_t i = 0; i < report.readBack.size(); ++i) {
        if (i != 3 && i != 4 && report.readBack[i] != 0) blank = false;
    }
    if (!blank) {
        report.message = "The tag does not read as blank after the write. Look at it in the live view.";
        return report;
    }
    report.ok = true;
    report.message = "Erased: the tag in slot " + std::to_string(slot) + " is blank.";
    return report;
}

}  // namespace joue
