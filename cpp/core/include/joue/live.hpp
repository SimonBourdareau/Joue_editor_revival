#pragma once
#include <array>
#include <string>
#include <vector>

#include "joue/blob.hpp"
#include "joue/device.hpp"
#include "joue/transfer.hpp"

namespace joue {

// What was read from one slot: the module blob, when a module sits there.
struct SlotInfo {
    bool present = false;
    Bytes blob;
};

// A module placed on the three slots: a small module takes one, a medium one two, a large one all three.
struct PlacedModule {
    int firstSlot = 1, slots = 1;
    int slot = 1;  // the slot the module answers from, the one a read or a write is addressed to
    ModuleBlob blob;
    Bytes raw;     // the bytes as read
};

// Where each module sits. A module answers from the slot of its tag, and says in its header where the tag is in the module (tagPositionOf), so it
// starts at the slot it answers from minus that position plus one. A large module takes every slot, a small one the slot it answers from. When the
// header gives a start that does not fit, a medium module takes slots 1 and 2 when it answers from slot 1, slots 2 and 3 when it answers from
// slot 3, and when it answers from slot 2 slots 1 and 2, unless slot 1 is taken by another module and 3 is free.
std::vector<PlacedModule> placeModules(const std::array<SlotInfo, SlotCount>& slots);

// A module of three slots answers from slot 1 only: it fills the whole instrument. A module of one or two slots can answer from any slot,
// because its tag may sit in either of its slots: a module of two slots on slots 2 and 3 can answer from slot 3.
bool moduleMayAnswerFrom(int widthInSlots, int slot);

// Where the tag sits in a module, counted from 1 at the left (byte 6 of the header; 0 counts as 1). The board says the slot in which it reads the
// tag; the module starts there minus the tag position plus one. The Pro modules have their tag in the first slot, the Play modules in the third.
int tagPositionOf(const Bytes& blob);

// moduleMayAnswerFrom, knowing the type: the tag of a Play module is in its third slot, so it is announced from there.
bool moduleTypeMayAnswerFrom(int type, int widthInSlots, int slot);

// Decides when a scan of the slots counts as a change, so that the picture does not flicker. A scan that equals what is shown
// changes nothing. A scan that differs has to be seen twice in a row, because one read can fail by chance, unless the board
// announced the change itself (urgent), and the first scan after connecting is urgent.
class SlotFilter {
public:
    // Returns true when the state to show has changed; accepted() is then the new state.
    bool offer(const std::array<SlotInfo, SlotCount>& scan, bool urgent);
    const std::array<SlotInfo, SlotCount>& accepted() const { return accepted_; }
    bool wasPresent(int slot) const { return accepted_[static_cast<std::size_t>(slot - 1)].present; }

private:
    static bool same(const std::array<SlotInfo, SlotCount>& a, const std::array<SlotInfo, SlotCount>& b);
    std::array<SlotInfo, SlotCount> accepted_, pending_;
    int seen_ = 0;
};

// A blob read for a slot is the module the board announced there when it is of the announced type. A read of a slot can hand back the
// module of another slot (docs/FIRMWARE.md), so a blob is kept for a slot only after this check; it looks at the type and not at the colour
// or the version, which are not needed to tell one kind of module from another.
bool blobFitsAnnouncement(const Bytes& blob, const ModuleEvent& event);

// What the board announced about its slots: a module inserted (with its type, version, colour and options) or removed. The
// board announces by itself when a tag appears or goes away, and again for every module when asked with `updateModuleList`;
// the official J-Play application trusts these announcements and nothing else for what is in which slot.
class Announcements {
public:
    void apply(const ModuleEvent& event);  // announcements for slots beyond the three are ignored
    bool present(int slot) const;
    ModuleEvent event(int slot) const;
    unsigned stamp(int slot) const;  // grows at every insertion into the slot
    unsigned changes(int slot) const;  // grows at every announcement for the slot, an insertion or a removal
    bool any() const { return any_; }  // an announcement has arrived since the last clear
    void clear();

private:
    std::array<ModuleEvent, SlotCount> events_;
    std::array<unsigned, SlotCount> stamps_{};
    std::array<unsigned, SlotCount> changes_{};
    bool any_ = false;
};

// A layout sent to the board without a tag (the temporary one) lasts until the board says that something happened to a tag: a module, a custom
// tag included, put on the instrument or taken off it, in any slot. `sent` holds changes() of the three slots when the layout was sent.
bool temporaryLayoutOutlived(const std::array<unsigned, SlotCount>& sent, const Announcements& now);

// Reads what is in the three slots without holding the instrument up. Reading a module blob makes the board read the whole
// tag of the module through its NFC reader in its main loop, which stalls the touch output; the serial of the tag is
// answered from a cache. So the serial of each slot is read every time, and a blob only when the serial is one not seen
// before (a new module) or when no blob is known for it.
class SlotScanner {
public:
    // A tag that two slots both report (the same serial or the very same blob) is one module: it is kept in the lowest slot.
    SlotScanner(TransferClient& client, Device& device) : client_(client), device_(device) {}
    std::array<SlotInfo, SlotCount> scan(const SlotFilter& filter);
    int blobReads() const { return blobReads_; }
    void forget();

private:
    struct Known {
        Bytes serial, blob;
    };
    TransferClient& client_;
    Device& device_;
    std::array<Known, SlotCount> known_;
    int blobReads_ = 0;
};

// True when the module has octave buttons: Button elements whose script adds or subtracts 12 from parameter 4 (`l{2,3}.4+12;`).
// Only such modules play notes that the board shifts; the notes of the others are never taken for shifted.
bool hasOctaveButtons(const ModuleBlob& blob);

constexpr float SlotWidthMm = 90.0f;
constexpr float SurfaceHeightMm = 90.0f;

// One finger on the surface, in millimetres from the left edge of slot 1 (0 to 270) and from the top of the blob's y axis.
struct Touch {
    int id = 0;
    float x = 0, y = 0;
    float pressure = 0;  // 0 to 1
    std::string label;
};

// Rebuilds touches from the MIDI the instrument plays, using the element records of the modules in the slots: a note on
// puts a touch on the element with that note, channel pressure or poly pressure sets its pressure, the Y control of a key
// its height, pitch bend its horizontal drift; strips and areas follow their controllers and fade when they stop. A finger that is held stays until its note off
// (or an all-notes-off), however still it is.
// The positions are reconstructed from MIDI, not measured.
// Where the last finger came down: the module (its place in the list given to setModules) and the record of the element in its blob.
// The sequence grows with every new touch; 0 means none yet.
struct TouchedElement {
    std::size_t module = 0;
    std::size_t record = 0;
    unsigned sequence = 0;
};

class TouchTracker {
public:
    // Replaces the modules the touches are matched against. Touches in progress are kept when the modules are the same as before
    // (a scan of the slots changes nothing), and dropped when they are not.
    void setModules(const std::vector<PlacedModule>& modules);
    void feed(const Bytes& message, double nowMs);
    TouchedElement lastTouched() const { return lastTouched_; }
    int unmatchedNoteOns() const { return unmatched_; }  // notes that no element of the modules plays (after trying octave shifts)
    int lastUnmatchedNote() const { return lastUnmatched_; }
    // The octave buttons of the Pro modules only run a script inside the board (`l{2,3}.4+12;`), the board tells nothing when
    // one is pressed, and the notes it plays are shifted by it. The shift of each module (in semitones, a multiple of 12, in the
    // order of the modules given to setModules) is therefore inferred: it is assumed unchanged while the notes fit the module's
    // elements, and moves to the nearest shift that makes a note fit when one does not. setOctaveShift corrects it by hand.
    std::vector<int> octaveShifts() const { return shifts_; }
    void setOctaveShift(std::size_t module, int semitones);
    std::vector<Touch> touches(double nowMs);
    void clear();

private:
    struct Target {
        std::uint8_t type = 0;
        float x = 0, y = 0, w = 0, h = 0;
        int note = -1, noteHigh = -1, channel = 0;
        bool mpe = false;
        int yControl = -1, yMin = 0, yMax = 127;
        int cc[3] = {-1, -1, -1}, ccChannel[3] = {0, 0, 0}, ccMin[3] = {0, 0, 0}, ccMax[3] = {127, 127, 127};
        int bendRange = 48;
        float semitoneMm = 8.0f;
        float moduleLeft = 0, moduleRight = 0;
        std::size_t module = 0;
        std::size_t record = 0;  // the place of the element in the blob of its module
        bool shiftable = false;
    };
    struct Active {
        Touch touch;
        const Target* target = nullptr;
        int channel = 0, note = -1;
        double lastMs = 0;
        bool timed = false;
        float baseX = 0, areaX = 0.5f, areaY = 0.5f, areaZ = 0.0f;
        int key = 0;
    };
    void markTouched(const Target& target);
    TouchedElement lastTouched_;
    unsigned touchSequence_ = 0;
    Active* findNote(int channel, int note);
    Active* findKey(int key);
    std::vector<Target> targets_;
    std::vector<Active> active_;
    std::vector<std::pair<int, Bytes>> signature_;
    std::vector<int> shifts_;
    int nextId_ = 1;
    int unmatched_ = 0, lastUnmatched_ = -1;
};

}  // namespace joue
