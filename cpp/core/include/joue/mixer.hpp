#pragma once
#include <array>
#include <bitset>
#include <string>
#include <vector>

#include "joue/bytes.hpp"

namespace joue::mixer {

constexpr int KeyCount = 17;
constexpr int StripCount = 8;
constexpr int MasterFader = 8;
constexpr int MaxStrips = 16;
constexpr int MasterSlot = MaxStrips;

constexpr int MasterKey = 0;
constexpr int FirstFaderKey = 1;
constexpr int FirstSelectKey = 9;
constexpr int FineKey = 9;
constexpr int ShiftKey = 10;
constexpr int FirstFunctionKey = 11;
constexpr int MoveLeftKey = 15;
constexpr int MoveRightKey = 16;

struct Layout {
    int keyNoteFirst = 36;
    int keyCcFirst = 14;
    int bubbleXCc = 80;
    int bubbleYCc = 81;
    int bubblePressureCc = 82;
    int stripCc = 83;
    int playNote = 94;
    int recordNote = 95;
};

enum class BubbleSideways { SelectTrack = 0, Channel = 1, Bank = 2, Off = 3, Pan = 4 };
enum class BubbleVertical { Cursor = 0, Off = 1, Bank = 2, Channel = 3 };
enum class FaderMode { Absolute = 0, Relative = 1, Hybrid = 2 };
enum class KeyLayout { Functions = 0, Selects = 1 };
enum class BankStep { Channel = 0, Bank = 1 };
enum class RightKeys { Move = 0, Select = 1 };
enum class Faders { Eight = 0, Sixteen = 1 };

struct Options {
    int deadZone = 24;
    double repeatDelayMs = 700.0;
    double repeatMs = 500.0;
    double buttonDelayMs = 500.0;
    double buttonRepeatMs = 250.0;
    BankStep bankStep = BankStep::Channel;
    RightKeys rightKeys = RightKeys::Move;
    BubbleVertical bubbleVertical = BubbleVertical::Bank;
    KeyLayout keyLayout = KeyLayout::Functions;
    double fineGain = 0.25;
    int pickupWindow = 400;
    int panTicks = 2;
    double panDelayMs = 300.0;
    double panRepeatMs = 90.0;
    double keyResolveMs = 30.0;
    Faders faders = Faders::Eight;
    bool extenderFirst = false;
    bool passThrough = false;
    std::bitset<128> blockedNotes;  // in pass-through, the notes of keys that are switched off send nothing
    double bubbleIdleMs = 700.0;
    double stripIdleMs = 300.0;
    double jogPerStep = 1.0;
    bool invertBubbleY = false;
    BubbleSideways bubbleSideways = BubbleSideways::Pan;
};

struct Snapshot {
    std::array<int, MaxStrips + 1> fader{};
    std::array<bool, KeyCount> touched{};
    bool stripActive = false;
    bool bubbleActive = false;
    int bubbleX = 64;
    int bubbleY = 64;
    bool playHeld = false;
    bool recordHeld = false;
    int noteShift = 0;
    FaderMode faderMode = FaderMode::Absolute;
    bool fine = false;
    bool shift = false;
    int selectedStrip = 0;
    std::array<bool, 128> notesDown{};  // the notes that a module in pass-through holds down
};

struct SurfaceState {
    std::array<std::string, StripCount> names;
    std::array<int, StripCount + 1> fader{};
    std::array<bool, StripCount + 1> faderKnown{};
    std::array<int, StripCount> meter{};
    std::array<bool, StripCount> record{}, solo{}, mute{}, select{};
    bool play = false, stop = false, recording = false;
    std::string topLine = std::string(56, ' '), bottomLine = std::string(56, ' ');

    void feed(const Bytes& message);
};

class Bridge {
public:
    explicit Bridge(Layout layout = Layout(), Options options = Options());

    void fromSurface(const Bytes& message, double nowMs, std::vector<Bytes>& out);
    void fromHost(const Bytes& message, std::vector<Bytes>& out, int unit = 0);
    std::vector<Bytes> takeExtender();
    void tick(double nowMs, std::vector<Bytes>& out);
    void releaseAll(std::vector<Bytes>& out);

    void press(int note, std::vector<Bytes>& out, int unit = 0);
    void setBubbleSideways(BubbleSideways mode) { options_.bubbleSideways = mode; }
    void setBankStep(BankStep step) { options_.bankStep = step; }
    void setOptions(const Options& options) { options_ = options; }
    void setFaderMode(FaderMode mode) { mode_ = mode; }
    FaderMode faderMode() const { return mode_; }
    Snapshot snapshot() const;
    const SurfaceState& host(int unit = 0) const { return hosts_[static_cast<std::size_t>(unit)]; }
    int visibleStrips() const;
    const Layout& layout() const { return layout_; }
    const Options& options() const { return options_; }

private:
    struct Place {
        int slot = -1;
        int unit = 0;
        int strip = 0;
    };
    Place placeOf(int key) const;
    void emit(int unit, const Bytes& message, std::vector<Bytes>& out);
    void touch(int key, bool down, double nowMs, std::vector<Bytes>& out);
    void flushPending(double nowMs, std::vector<Bytes>& out);
    void noteReleased(int note, double nowMs, std::vector<Bytes>& out);
    void updateFader(int key, std::vector<Bytes>& out);
    int shaped(const Place& place, int raw);
    void setFine(bool on);
    void functionKey(int index, bool down, std::vector<Bytes>& out);
    int currentStrip() const;
    void sendPan(int step, std::vector<Bytes>& out);
    bool isPan(int direction) const;
    void bubbleChanged(double nowMs, std::vector<Bytes>& out);
    void bubbleReleased();
    void tap(int note, std::vector<Bytes>& out);
    void strip(int value, double nowMs, std::vector<Bytes>& out);
    void fireDirection(int direction, std::vector<Bytes>& out);
    void updateDirections(double nowMs, std::vector<Bytes>& out);
    void stepWindow(int step, std::vector<Bytes>& out);
    void moveSelection(int step, std::vector<Bytes>& out);

    Layout layout_;
    Options options_;
    std::array<int, KeyCount> high_{}, low_{};
    std::array<bool, KeyCount> hasLow_{};
    std::array<int, MaxStrips + 1> sent_{};
    std::array<bool, KeyCount> touched_{};
    bool stripActive_ = false;
    int stripLast_ = 0;
    double stripSeenMs_ = 0.0;
    bool bubbleActive_ = false;
    int bubbleX_ = 64, bubbleY_ = 64;
    double bubbleSeenMs_ = 0.0;
    std::array<bool, 4> direction_{};
    std::array<double, 4> nextRepeatMs_{};
    bool playHeld_ = false, recordHeld_ = false, bubblePressed_ = false;
    std::array<bool, 2> buttonHeld_{};
    std::array<double, 2> buttonNextMs_{};
    struct Follow {
        enum State { Direct, Relative, Waiting } state = Direct;
        bool started = false;
        int refRaw = 0, refOut = 0, target = 0, lastRaw = 0, currentRaw = 0;
        double gain = 1.0;
    };
    std::array<Follow, MaxStrips + 1> follow_{};
    FaderMode mode_ = FaderMode::Absolute;
    bool fine_ = false, shift_ = false;
    int lastTouched_ = -1;
    std::array<int, 8> heldNote_{}, heldModifier_{};
    std::array<bool, 128> notesDown_{};
    std::array<SurfaceState, 2> hosts_;
    std::vector<Bytes> extender_;
    int noteShift_ = 0;
    std::array<int, KeyCount> noteOf_{};
    bool pendingActive_ = false;
    int pendingNote_ = 0;
    double pendingMs_ = 0.0;
};

bool isMixerLayout(const Bytes& blob);

std::string describe(const Bytes& message);

// What a Mackie Control note is, in words ("mute 3", "play"), or empty.
std::string noteName(int note);

}  // namespace joue::mixer
