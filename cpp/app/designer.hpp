#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "joue/designer.hpp"

// A window to draw module layouts by hand: elements are placed, moved and resized on the module surface, filled in with their
// MIDI fields, checked, and saved as a module blob. It needs no device and no module.
class Designer {
public:
    bool open = false;
    std::function<std::optional<std::string>()> chooseOpen, chooseSave;

    // Opens a file with the program the system uses for it (set by the editor).
    std::function<void(const std::string&)> openExternally;

    // Sending the layout to the board as a temporary one, without any module (see joue::Device::loadTemporarily).
    std::function<bool()> canLoadTemporarily;
    std::function<void(const joue::Bytes&)> loadTemporarily;
    std::function<std::string()> loadStatus;

    // Writing the layout to a blank tag lying in a slot (see joue::writeBlankTag).
    std::function<bool()> canWriteTag;
    std::function<bool(int)> newTagIn;  // the board sees a new, blank tag in that slot
    std::function<bool()> playBoard;  // the board is a Play board: it cannot change modules
    std::function<void(int, const joue::Bytes&)> writeTag;
    std::function<void(int)> eraseTag;
    std::function<std::pair<bool, joue::Bytes>(int)> slotModule;

    void newDesign(int slots);
    bool openFile(const std::string& path);
    bool openBlob(const joue::Bytes& blob, const std::string& savePath, const std::string& source);
    bool saveFile(const std::string& path);
    void draw();
    joue::Design& design() { return design_; }
    bool dirty() const { return dirty_; }
    void selectElement(int index) { pickOnly(index); }
    // A touch on the temporary layout selects the element that was touched (the number of an element is its place in the blob).
    void touchSelect(int index);

private:
    struct Snapshot {
        joue::Bytes data;
        int slots;
    };
    enum class Drag { None, Move, Resize, Group, GroupResize, Band };

    void pushUndo();
    void undo();
    void redo();
    void restore(const Snapshot& snapshot);
    Snapshot snapshot() const;
    void drawToolbar();
    void drawPrintWindow();
    void drawTagWindow();
    void drawEraseWindow();
    void drawArrange();
    bool drawSlotState(int slot, bool erase);
    void drawOutcome(const std::string& english);
    std::vector<std::size_t> pickedIndices() const;
    bool savePdf(const std::string& path);
    void drawCanvas();
    void drawInspector();
    void select(int index) { pickOnly(index); }
    void pickOnly(int index);
    void addPick(int index);
    void togglePick(int index);
    bool isPicked(int index) const;
    void clearPicks();
    int snapTo(float value) const;

    joue::Design design_{1};
    std::string path_ = "layout.bin", status_;
    int selected_ = -1, tool_ = 0, snap_ = 1;
    std::vector<int> picked_;                 // everything picked, in the order of the elements; selected_ is the one the inspector shows
    std::vector<joue::Box> groupStart_;       // where the picked elements were when a move of all of them began
    int dragAxis_ = 0;                        // 0: a corner, both ways; 1: an edge, the width only; 2: an edge, the height only
    int swapTarget_ = -1;                     // the element that a shift-drag would swap with if the pointer were let go now
    int ctrlHit_ = -1;                        // the element a ctrl-press landed on: a click that goes nowhere takes it off the picks again
    bool ctrlWasPicked_ = false;
    bool bandAdd_ = false;                    // the selection rectangle adds to the picks instead of replacing them
    float travel_ = 0;                        // how far the pointer has gone since the drag began, in mm
    std::vector<joue::ElementRecord> clipboard_;
    int pasteCount_ = 0, lastTool_ = 1;
    bool grid_ = true, dirty_ = false;
    std::vector<Snapshot> undo_, redo_;
    Drag drag_ = Drag::None;
    int dragHandle_ = 0;
    joue::Box dragBox_;
    float dragStartX_ = 0, dragStartY_ = 0;
    bool created_ = false;
    int gridRows_ = 4, gridColumns_ = 4, gridGap_ = 2, gridFirstNote_ = 36, gridRowStep_ = 4, gridChannel_ = 1;
    int keyboardWhite_ = 8, keyboardFirst_ = 48, keyboardChannel_ = 1;
    bool printLetter_ = false, printLabels_ = true, printGrid_ = false, printMarks_ = true;  // the choices of the print window
    std::string printPath_, printStatus_;
    // the choices of the generators and of Repeat
    int isoPreset_ = 0, isoRows_ = 4, isoColumns_ = 6, isoGap_ = 2, isoFirstNote_ = 36, isoColumnInterval_ = 1, isoRowInterval_ = 5, isoChannel_ = 1;
    bool isoStagger_ = false;
    int fretPreset_ = 0, fretStrings_ = 6, fretFirstNote_ = 40, fretInterval_ = 5, fretFrets_ = 22, fretChannel_ = 1;
    int repeatCopies_ = 3, repeatDx_ = 15, repeatDy_ = 0, repeatNoteStep_ = 0;
    int tagSlot_ = 1;
    bool tagConfirmed_ = false, eraseConfirmed_ = false;
    int eraseSlot_ = 1;
    std::string scriptText_;
    bool scriptLoaded_ = false;
};
