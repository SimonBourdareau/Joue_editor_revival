#pragma once
#include <functional>
#include <string>

#include "joue/blob.hpp"
#include "joue/device.hpp"
#include "joue/transfer.hpp"

namespace joue {

struct PushReport {
    bool ok = false;
    std::string message;
    Bytes readBack;  // what the module held when it was read back after the write
};

// Replaces the layout of the module in a slot with an edited blob, with the checks that make a bad write unlikely:
//  - the edited blob must be a valid module blob without errors and at most 888 bytes;
//  - bytes 0 to 3 of the header (type, version, colour, security) are kept as the module has them, because the board
//    does not write that part of the tag without full privileges;
//  - the module now in the slot must still be the one the layout was read from (same bytes 0 to 3);
//  - after the write the board gets time to reload the tag, the slot is read back, and the read-back must equal what was
//    written (the board recomputes the checksum byte of the header from the tag's id, so bytes 3 and 4 are not compared).
// pause(ms) is called to wait; it can be a no-op in tests.
PushReport pushLayout(Device& device, TransferClient& client, int slot, const Bytes& original, const Bytes& edited, const std::function<void(int)>& pause);

// Programs the tag in a slot that holds no layout yet, a blank NTAG216, with a whole layout (see the notes in docs/FIRMWARE.md):
//  - nothing is written when the board reports a module in the slot (slotHoldsModule), when it cannot read a tag id there, or when the layout
//    is not a valid module blob of at most 888 bytes;
//  - the security byte (byte 3) is the XOR of the 7 bytes of the tag's id and the checksum byte (byte 4) follows from it, as the board computes them;
//  - the first page of the tag (type, version, colour, security) is written, which the board does only with the full privileges: they are
//    requested first (the XOR of the board's serial), and nothing is written if they are refused;
//  - the slot is read back, and the read-back must equal what was written (byte 4 is recomputed by the board and is not compared).
PushReport writeBlankTag(Device& device, TransferClient& client, int slot, const Bytes& layout, bool slotHoldsModule, const std::function<void(int)>& pause);

// What a tag holds, told from the layout on it: nothing, a layout that is not one of the manufacturer's (a known type of module whose records
// are not those of its factory layout, as a layout drawn in the designer), a manufacturer's (a known type with the records of its factory
// layout, whatever settings were changed in it), or one that cannot be told (a type this program does not know: it may be a manufacturer's).
enum class TagKind { Empty, Custom, Manufacturer, Unknown };
TagKind classifyTag(const Bytes& blob);

// Erases the tag in a slot, but only a tag that holds a custom layout. The layout is read twice and must be the same, and it must be of the type
// the board announced for the slot (announcedType, -1 when it announced nothing): a read of a slot can return the module of another one, and
// nothing is erased on the strength of an answer that may be for the wrong slot. A manufacturer's layout, one that cannot be told, and a slot
// where no tag is detected are refused, and so is an erase when the board does not give the full privileges. The user memory (888 bytes) is
// written with zeros and the slot is read back: it must hold no layout any more.
PushReport blankTag(Device& device, TransferClient& client, int slot, int announcedType, const std::function<void(int)>& pause);

}  // namespace joue
