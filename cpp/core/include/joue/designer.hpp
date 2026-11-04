#pragma once
#include <string>
#include <vector>

#include "joue/blob.hpp"

namespace joue {

// Where an element goes along an axis when elements are aligned: the low side (x: left, y: bottom of the mirrored picture), the middle, the high side.
enum class Edge { Low, Centre, High };

// The parameters of the manufacturer's layouts, with the same values: the pressure range of a pad, key, string or area is the depth byte of
// its header (Low and High differ by kind of element); MPE and the MPE channels are bits of the instrument options; the aftertouch type is a
// command of the start-up script; the Y control of a key is its options, its CC number and its range, which is swapped when it is inverted.
enum class PressureRange { Low, High, Custom };
enum class AftertouchType { Off, Channel, Polyphonic };
struct YControl {
    bool enabled = false;
    bool relative = false;
    bool inverted = false;
    int cc = 0;
    int low = 0;
    int high = 127;
};

// What a button does, with the commands of the manufacturer's modules: the command is the target (every element "*", a layer "l3" or "l{2,3}", a kind of
// element "c6" for the strings) followed by what is done to it. An octave button shifts the notes by 12; the others switch a function on and off.
enum class ButtonAction { Custom, ChannelSelection, Sustain, Vibrato, Bending, YControl, AftertouchChannel, AftertouchPolyphonic, Glissando, Fretless, OctaveDown, OctaveUp };
struct ButtonSetting {
    ButtonAction action = ButtonAction::Custom;
    std::string target = "*";
    int value = 1;
    std::string command;
};
std::string buttonCommandFor(ButtonAction action, const std::string& target, int value);
ButtonSetting parseButtonCommand(const std::string& command);
std::string buttonSymbol(const ButtonSetting& setting);

// A rectangle on the module surface in whole millimetres (blob coordinates, origin at the corner, 90 mm high, 90 mm per slot).
struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

// One numeric field of an element record: where it lives, how wide it is and its sensible range.
struct FieldSpec {
    const char* name;
    int offset, size;
    bool isSigned;
    int minimum, maximum;
};

// Editable fields after the 16-byte header for a record type and version (empty for types that are only kept as they are).
const std::vector<FieldSpec>& bodyFields(std::uint8_t type, std::uint8_t version);

// A module layout built by hand or read from a module blob. Everything is kept as the blob itself, so what is saved is what a
// module blob is, and any blob the decoder reads can be opened, changed and saved again without loss.
class Design {
public:
    static constexpr int HeightMm = 90;
    static constexpr int SlotWidthMm = 90;
    static constexpr int MaxElements = 255;
    static constexpr std::size_t MaxBlobBytes = 888;  // the firmware's receive buffer and the size of the module tag

    explicit Design(int slots = 1);
    static Design fromBytes(const Bytes& data);
    Bytes toBytes() const;

    int slots() const { return slots_; }
    void setSlots(int slots);
    int widthMm() const { return slots_ * SlotWidthMm; }
    int inferSlots() const;  // from the module type, else from the furthest element edge

    ModuleBlob& blob() { return blob_; }
    const ModuleBlob& blob() const { return blob_; }
    std::size_t count() const { return blob_.elements.size(); }

    bool hasBox(std::size_t index) const;  // scripts have no place on the surface
    Box box(std::size_t index) const;
    void setBox(std::size_t index, Box box);  // keeps the element on the surface, at least 2 mm wide and high
    ElementHeader header(std::size_t index) const;
    void setHeader(std::size_t index, const ElementHeader& header);  // layer, z, depth and options; type, version and id stay
    int field(std::size_t index, const FieldSpec& spec) const;
    void setField(std::size_t index, const FieldSpec& spec, int value);

    // New elements get the defaults of the official Play modules; returns the index, or count() when the type cannot be created.
    std::size_t add(std::uint8_t type, Box box);
    std::size_t duplicate(std::size_t index);
    void remove(std::size_t index);

    // Gestures on several elements at once, as in the editor of BlockPaint.
    bool resizeGroup(const std::vector<std::size_t>& indices, Box from, Box to);
    Box bounds(const std::vector<std::size_t>& indices) const;  // the smallest box that holds the elements that have a place
    // Move the elements together, keeping their spacing. Nothing moves, and false is returned, when any of them would reach off the surface.
    bool moveGroup(const std::vector<std::size_t>& indices, int dx, int dy);
    // Exchange the places of two elements; each keeps its size and all its fields. False, and nothing changes, when either would then reach off the surface.
    bool swapPositions(std::size_t a, std::size_t b);
    // Put a copy of an element record (taken from this design or another) in the design, with a new id and moved by (dx, dy). Returns the index, or count() when there is no room.
    std::size_t paste(const ElementRecord& record, int dx, int dy);
    // Arranging, as in a drawing program. With one element, alignment is to the surface; with several, to the box that holds them.
    void alignX(const std::vector<std::size_t>& indices, Edge edge);
    void alignY(const std::vector<std::size_t>& indices, Edge edge);
    // Equal gaps between at least three elements along x or y; the first and the last stay where they are. False when there are fewer.
    bool distributeX(const std::vector<std::size_t>& indices);
    bool distributeY(const std::vector<std::size_t>& indices);
    // Mirrors the elements within the box that holds them (their places only: sizes and settings are kept).
    void flipX(const std::vector<std::size_t>& indices);
    void flipY(const std::vector<std::size_t>& indices);
    // Copies of the elements: copy k is moved by k * (dx, dy) and, for pads, keys and strings, its note is k * noteStep higher. It stops before a copy
    // that would reach off the surface or does not fit in the blob. Returns the indexes of the new elements.
    std::vector<std::size_t> repeat(const std::vector<std::size_t>& indices, int copies, int dx, int dy, int noteStep);
    std::size_t addPadGrid(Box area, int rows, int columns, int gap, int firstNote, int rowStep, int channel);
    // Pads whose note is firstNote + column * columnInterval + row * rowInterval; with stagger every other row is shifted by half a pad.
    std::size_t addIsomorphicGrid(Box area, int rows, int columns, int gap, int firstNote, int columnInterval, int rowInterval, int channel, bool stagger);
    // One string element per open-string note, the first at the low side of the area, as the Grand Fretboard has them.
    std::size_t addFretboard(Box area, const std::vector<int>& openNotes, int frets, int gap, int channel);
    std::size_t addKeyboard(Box area, int firstNote, int whiteKeys, int channel);  // white keys on layer 2, black keys on layer 3

    bool hasPressureRange(std::size_t index) const;
    PressureRange pressureRange(std::size_t index) const;
    void setPressureRange(std::size_t index, PressureRange range);
    PressureRange pressureRangeOfAll() const;
    void setPressureRangeOfAll(PressureRange range);
    bool mpe() const;
    void setMpe(bool on);
    int mpeChannels() const;
    void setMpeChannels(int zone);
    AftertouchType aftertouch() const;
    void setAftertouch(AftertouchType type);
    bool hasYControl(std::size_t index) const;
    YControl yControl(std::size_t index) const;
    void setYControl(std::size_t index, const YControl& control);

    bool vibrato() const;
    void setVibrato(bool on);
    bool hasVibratoValue() const;
    int vibratoValue() const;
    void setVibratoValue(int value);
    bool hasGlide() const;
    bool glissando() const;
    void setGlissando(bool on);
    int glissandoRange() const;
    void setGlissandoRange(int semitones);
    std::size_t addGlide(int semitones);
    void removeGlide();

    bool hasButton(std::size_t index) const;
    std::string buttonCommand(std::size_t index) const;
    void setButtonCommand(std::size_t index, const std::string& command);
    ButtonSetting buttonSetting(std::size_t index) const;
    void setButtonSetting(std::size_t index, const ButtonSetting& setting);

    bool hasScript() const;
    std::string script() const;
    void setScript(const std::string& text);  // creates the script element when there is none
    void removeScript();

    // Lines starting with "Error:" would not make a valid blob; "Note:" lines are cautions.
    std::vector<std::string> problems() const;

private:
    int nextId() const;
    ModuleBlob blob_;
    int slots_ = 1;
};

// True when header byte 4 equals byte 3 XOR every byte from offset 8 on, as on every module read from a real Joue.
bool checksumHolds(const Bytes& blob);

std::string noteName(int note);  // 60 -> "C3", as the original editors show it (note 0 is C-2)

}  // namespace joue
