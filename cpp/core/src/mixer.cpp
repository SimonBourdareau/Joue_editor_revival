#include "joue/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

#include "joue/instrumentmodule.hpp"

namespace joue::mixer {

namespace {

constexpr int TouchNoteFirst = 104;
constexpr int MasterTouchNote = 112;
constexpr int SelectNoteFirst = 24;
constexpr int PlayNote = 94;
constexpr int StopNote = 93;
constexpr int RecordNote = 95;
constexpr int JogCc = 60;
constexpr int CursorUpNote = 96;
constexpr int CursorDownNote = 97;
constexpr int ChannelLeftNote = 48;
constexpr int ChannelRightNote = 49;
constexpr int BankLeftNote = 46;
constexpr int BankRightNote = 47;
constexpr int UndoNote = 81;
constexpr int CycleNote = 86;
constexpr int DropNote = 87;
constexpr int ClickNote = 89;
constexpr int RewindNote = 91;
constexpr int FastForwardNote = 92;
constexpr int ShiftModifierNote = 70;
constexpr int VPotCcFirst = 16;
constexpr int Left = 0;
constexpr int Right = 1;
constexpr int Up = 2;
constexpr int Down = 3;

Bytes make(int a, int b, int c) {
    return Bytes{static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c)};
}

Bytes noteMessage(int note, int velocity) { return make(0x90, note & 0x7F, velocity & 0x7F); }

Bytes mackieSysex(int device, int command, const Bytes& body) {
    Bytes message{0xF0, 0x00, 0x00, 0x66, static_cast<std::uint8_t>(device), static_cast<std::uint8_t>(command)};
    message.insert(message.end(), body.begin(), body.end());
    message.push_back(0xF7);
    return message;
}

const Bytes& surfaceSerial() {
    static const Bytes serial{'J', 'O', 'U', 'E', 'M', 'C', 'U'};
    return serial;
}

std::string mackieNoteName(int note) {
    if (note >= 0 && note < 8) return "record arm " + std::to_string(note + 1);
    if (note >= 8 && note < 16) return "solo " + std::to_string(note - 7);
    if (note >= 16 && note < 24) return "mute " + std::to_string(note - 15);
    if (note >= SelectNoteFirst && note < SelectNoteFirst + 8) return "select " + std::to_string(note - SelectNoteFirst + 1);
    if (note >= TouchNoteFirst && note < TouchNoteFirst + 8) return "touch fader " + std::to_string(note - TouchNoteFirst + 1);
    if (note == MasterTouchNote) return "touch master fader";
    if (note == PlayNote) return "play";
    if (note == StopNote) return "stop";
    if (note == RecordNote) return "record";
    if (note == ChannelLeftNote) return "channel left";
    if (note == ChannelRightNote) return "channel right";
    if (note == BankLeftNote) return "bank left";
    if (note == BankRightNote) return "bank right";
    if (note == CursorUpNote) return "cursor up";
    if (note == CursorDownNote) return "cursor down";
    if (note == UndoNote) return "undo";
    if (note == CycleNote) return "cycle";
    if (note == DropNote) return "drop (punch)";
    if (note == ClickNote) return "click";
    if (note == RewindNote) return "rewind";
    if (note == FastForwardNote) return "fast forward";
    if (note == ShiftModifierNote) return "shift";
    if (note == 84) return "marker";
    if (note == 85) return "nudge";
    if (note == 88) return "replace";
    if (note == 90) return "solo";
    if (note == 98) return "cursor left";
    if (note == 99) return "cursor right";
    if (note == 100) return "zoom";
    if (note == 101) return "scrub";
    return "";
}

std::string trimRight(std::string text) {
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

}  // namespace

Bridge::Bridge(Layout layout, Options options) : layout_(layout), options_(options) {
    sent_.fill(-1);
    direction_.fill(false);
    nextRepeatMs_.fill(0.0);
    heldNote_.fill(-1);
    heldModifier_.fill(-1);
}

Bridge::Place Bridge::placeOf(int key) const {
    Place place;
    if (key == MasterKey) {
        place.slot = MasterSlot;
        place.unit = 0;
        place.strip = MasterFader;
        return place;
    }
    if (options_.faders == Faders::Sixteen) {
        if (key >= FirstFaderKey && key < FirstFaderKey + MaxStrips) {
            int fader = key - FirstFaderKey;
            place.slot = fader;
            place.unit = fader / StripCount;
            if (options_.extenderFirst) place.unit = 1 - place.unit;
            place.strip = fader % StripCount;
        }
        return place;
    }
    if (key >= FirstFaderKey && key < FirstSelectKey) {
        place.slot = key - FirstFaderKey;
        place.strip = place.slot;
    }
    return place;
}

void Bridge::emit(int unit, const Bytes& message, std::vector<Bytes>& out) {
    if (unit == 0) out.push_back(message);
    else extender_.push_back(message);
}

std::vector<Bytes> Bridge::takeExtender() {
    std::vector<Bytes> taken;
    taken.swap(extender_);
    return taken;
}

int Bridge::visibleStrips() const {
    auto count = [](const SurfaceState& state) {
        int visible = 0;
        for (std::size_t i = 0; i < state.names.size(); ++i) {
            if (!state.names[i].empty()) visible = static_cast<int>(i) + 1;
        }
        return visible;
    };
    int first = options_.extenderFirst && options_.faders == Faders::Sixteen ? 1 : 0;
    int firstCount = count(hosts_[static_cast<std::size_t>(first)]);
    if (options_.faders == Faders::Eight) return firstCount == 0 ? StripCount : firstCount;
    int secondCount = count(hosts_[static_cast<std::size_t>(1 - first)]);
    if (firstCount == 0 && secondCount == 0) return MaxStrips;
    return firstCount < StripCount ? firstCount : StripCount + secondCount;
}

void Bridge::touch(int key, bool down, double nowMs, std::vector<Bytes>& out) {
    if (key < 0 || key >= KeyCount || touched_[static_cast<std::size_t>(key)] == down) return;
    touched_[static_cast<std::size_t>(key)] = down;
    Place place = placeOf(key);
    if (options_.faders == Faders::Eight && options_.keyLayout == KeyLayout::Functions && key >= FineKey) {
        functionKey(key - FineKey, down, out);
        return;
    }
    if (place.slot >= 0) {
        follow_[static_cast<std::size_t>(place.slot)] = Follow();
        if (down && options_.faders == Faders::Eight && place.slot < StripCount) lastTouched_ = place.slot;
        int note = key == MasterKey ? MasterTouchNote : TouchNoteFirst + place.strip;
        emit(place.unit, noteMessage(note, down ? 127 : 0), out);
        return;
    }
    if (options_.faders != Faders::Eight) return;
    if (key >= MoveLeftKey && options_.rightKeys == RightKeys::Move) {
        std::size_t which = key == MoveRightKey ? 1 : 0;
        if (down) {
            buttonHeld_[which] = true;
            stepWindow(which == 1 ? 1 : -1, out);
            buttonNextMs_[which] = nowMs + options_.buttonDelayMs;
        } else {
            buttonHeld_[which] = false;
        }
        return;
    }
    out.push_back(noteMessage(SelectNoteFirst + key - FirstSelectKey, down ? 127 : 0));
}

void Bridge::flushPending(double nowMs, std::vector<Bytes>& out) {
    if (!pendingActive_) return;
    pendingActive_ = false;
    int key = pendingNote_ - layout_.keyNoteFirst - noteShift_;
    if (key < 0 || key >= KeyCount) return;
    noteOf_[static_cast<std::size_t>(key)] = pendingNote_;
    touch(key, true, nowMs, out);
}

void Bridge::noteReleased(int note, double nowMs, std::vector<Bytes>& out) {
    if (pendingActive_ && pendingNote_ == note) flushPending(nowMs, out);
    for (int key = 0; key < KeyCount; ++key) {
        if (touched_[static_cast<std::size_t>(key)] && noteOf_[static_cast<std::size_t>(key)] == note) {
            touch(key, false, nowMs, out);
            return;
        }
    }
    int key = note - layout_.keyNoteFirst - noteShift_;
    if (key >= 0 && key < KeyCount) touch(key, false, nowMs, out);
}

int Bridge::shaped(const Place& place, int raw) {
    std::size_t slot = static_cast<std::size_t>(place.slot);
    Follow& f = follow_[slot];
    const SurfaceState& host = hosts_[static_cast<std::size_t>(place.unit)];
    std::size_t hostIndex = static_cast<std::size_t>(place.strip);
    bool known = host.faderKnown[hostIndex];
    int hostValue = host.fader[hostIndex];
    int lastOut = sent_[slot] >= 0 ? sent_[slot] : (known ? hostValue : raw);
    f.currentRaw = raw;
    if (!f.started) {
        f.started = true;
        f.lastRaw = raw;
        if (fine_) {
            f.state = Follow::Relative;
            f.gain = options_.fineGain;
            f.refRaw = raw;
            f.refOut = known ? hostValue : lastOut;
        } else if (mode_ == FaderMode::Absolute || !known) {
            f.state = Follow::Direct;
        } else if (mode_ == FaderMode::Relative) {
            f.state = Follow::Relative;
            f.gain = 1.0;
            f.refRaw = raw;
            f.refOut = hostValue;
        } else if (std::abs(raw - hostValue) <= options_.pickupWindow) {
            f.state = Follow::Direct;
        } else {
            f.state = Follow::Waiting;
            f.target = hostValue;
        }
    }
    if (f.state == Follow::Direct) return raw;
    if (f.state == Follow::Relative) return std::max(0, std::min(16383, f.refOut + static_cast<int>(std::lround(static_cast<double>(raw - f.refRaw) * f.gain))));
    long long product = static_cast<long long>(f.lastRaw - f.target) * static_cast<long long>(raw - f.target);
    bool near = std::abs(raw - f.target) <= options_.pickupWindow;
    f.lastRaw = raw;
    if (product <= 0 || near) {
        f.state = Follow::Direct;
        return raw;
    }
    return -1;
}

void Bridge::setFine(bool on) {
    if (fine_ == on) return;
    fine_ = on;
    if (options_.faders != Faders::Eight) return;
    for (int key = 0; key < KeyCount; ++key) {
        if (!touched_[static_cast<std::size_t>(key)]) continue;
        Place place = placeOf(key);
        if (place.slot < 0) continue;
        std::size_t slot = static_cast<std::size_t>(place.slot);
        Follow& f = follow_[slot];
        if (!f.started) continue;
        int lastOut = sent_[slot] >= 0 ? sent_[slot] : f.currentRaw;
        if (on) {
            f.state = Follow::Relative;
            f.gain = options_.fineGain;
            f.refRaw = f.currentRaw;
            f.refOut = lastOut;
        } else if (mode_ == FaderMode::Relative) {
            f.state = Follow::Relative;
            f.gain = 1.0;
            f.refRaw = f.currentRaw;
            f.refOut = lastOut;
        } else if (std::abs(f.currentRaw - lastOut) <= options_.pickupWindow) {
            f.state = Follow::Direct;
        } else {
            f.state = Follow::Waiting;
            f.target = lastOut;
            f.lastRaw = f.currentRaw;
        }
    }
}

int Bridge::currentStrip() const {
    if (lastTouched_ >= 0) return lastTouched_;
    for (int i = 0; i < StripCount; ++i) {
        if (hosts_[0].select[static_cast<std::size_t>(i)]) return i;
    }
    return 0;
}

void Bridge::sendPan(int step, std::vector<Bytes>& out) {
    int ticks = std::max(1, std::min(15, options_.panTicks));
    out.push_back(make(0xB0, VPotCcFirst + currentStrip(), step > 0 ? ticks : (0x40 | ticks)));
}

bool Bridge::isPan(int direction) const { return (direction == Left || direction == Right) && options_.bubbleSideways == BubbleSideways::Pan; }

void Bridge::functionKey(int index, bool down, std::vector<Bytes>& out) {
    if (index == 0) {
        setFine(down);
        return;
    }
    if (index == 1) {
        shift_ = down;
        return;
    }
    std::size_t slot = static_cast<std::size_t>(index);
    if (!down) {
        if (heldNote_[slot] >= 0) out.push_back(noteMessage(heldNote_[slot], 0));
        if (heldModifier_[slot] >= 0) out.push_back(noteMessage(heldModifier_[slot], 0));
        heldNote_[slot] = -1;
        heldModifier_[slot] = -1;
        return;
    }
    int position = index - 2;
    int strip = currentStrip();
    int note = -1, modifier = -1;
    if (!shift_) {
        const int fixed[3] = {StopNote, RewindNote, FastForwardNote};
        if (position < 3) note = fixed[position];
        else note = (position == 3 ? 8 : (position == 4 ? 16 : 0)) + strip;
    } else {
        switch (position) {
            case 0: note = CycleNote; break;
            case 1: note = DropNote; break;
            case 2: note = UndoNote; break;
            case 3:
                note = UndoNote;
                modifier = ShiftModifierNote;
                break;
            case 4:
                mode_ = mode_ == FaderMode::Absolute ? FaderMode::Relative : (mode_ == FaderMode::Relative ? FaderMode::Hybrid : FaderMode::Absolute);
                return;
            default: note = ClickNote; break;
        }
    }
    if (modifier >= 0) {
        out.push_back(noteMessage(modifier, 127));
        heldModifier_[slot] = modifier;
    }
    out.push_back(noteMessage(note, 127));
    heldNote_[slot] = note;
}

void Bridge::updateFader(int key, std::vector<Bytes>& out) {
    Place place = placeOf(key);
    if (place.slot < 0) return;
    std::size_t index = static_cast<std::size_t>(key);
    int value = hasLow_[index] ? high_[index] * 128 + low_[index] : high_[index] * 129;
    value = std::max(0, std::min(16383, value));
    if (options_.faders == Faders::Eight && touched_[index]) {
        value = shaped(place, value);
        if (value < 0) return;
    }
    if (sent_[static_cast<std::size_t>(place.slot)] == value) return;
    sent_[static_cast<std::size_t>(place.slot)] = value;
    emit(place.unit, make(0xE0 | place.strip, value & 0x7F, (value >> 7) & 0x7F), out);
}

void Bridge::tap(int note, std::vector<Bytes>& out) {
    out.push_back(noteMessage(note, 127));
    out.push_back(noteMessage(note, 0));
}

void Bridge::press(int note, std::vector<Bytes>& out, int unit) {
    if (unit == 0) {
        tap(note, out);
        return;
    }
    extender_.push_back(noteMessage(note, 127));
    extender_.push_back(noteMessage(note, 0));
}

void Bridge::moveSelection(int step, std::vector<Bytes>& out) {
    int visible = std::min(visibleStrips(), StripCount);
    int current = -1;
    for (int i = 0; i < visible && i < StripCount; ++i) {
        if (hosts_[0].select[static_cast<std::size_t>(i)]) {
            current = i;
            break;
        }
    }
    int target = current < 0 ? (step > 0 ? 0 : visible - 1) : current + step;
    if (target < 0 || target >= visible) {
        tap(step > 0 ? ChannelRightNote : ChannelLeftNote, out);
        return;
    }
    tap(SelectNoteFirst + target, out);
}

void Bridge::fireDirection(int direction, std::vector<Bytes>& out) {
    if (direction == Up || direction == Down) {
        bool up = direction == Up;
        switch (options_.bubbleVertical) {
            case BubbleVertical::Off: return;
            case BubbleVertical::Cursor: tap(up ? CursorUpNote : CursorDownNote, out); return;
            case BubbleVertical::Bank: tap(up ? BankLeftNote : BankRightNote, out); break;
            case BubbleVertical::Channel: tap(up ? ChannelLeftNote : ChannelRightNote, out); break;
        }
        lastTouched_ = -1;
        return;
    }
    int step = direction == Right ? 1 : -1;
    if (options_.bubbleSideways == BubbleSideways::Off) return;
    if (options_.bubbleSideways == BubbleSideways::Pan) {
        sendPan(step, out);
        return;
    }
    lastTouched_ = -1;
    if (options_.bubbleSideways == BubbleSideways::Channel || (options_.faders == Faders::Sixteen && options_.bubbleSideways == BubbleSideways::SelectTrack)) tap(step > 0 ? ChannelRightNote : ChannelLeftNote, out);
    else if (options_.bubbleSideways == BubbleSideways::Bank) tap(step > 0 ? BankRightNote : BankLeftNote, out);
    else moveSelection(step, out);
}

void Bridge::updateDirections(double nowMs, std::vector<Bytes>& out) {
    int zone = options_.deadZone;
    int across = bubbleX_ - 64;
    int up = options_.invertBubbleY ? 64 - bubbleY_ : bubbleY_ - 64;
    std::array<bool, 4> beyond{across < -zone, across > zone, up > zone, up < -zone};
    std::array<bool, 4> wanted{};
    int current = -1;
    for (std::size_t d = 0; d < 4; ++d) {
        if (direction_[d]) current = static_cast<int>(d);
    }
    if (current >= 0 && beyond[static_cast<std::size_t>(current)]) {
        wanted[static_cast<std::size_t>(current)] = true;
    } else if (std::abs(across) >= std::abs(up)) {
        wanted[Left] = beyond[Left];
        wanted[Right] = beyond[Right];
    } else {
        wanted[Up] = beyond[Up];
        wanted[Down] = beyond[Down];
    }
    for (std::size_t d = 0; d < 4; ++d) {
        if (wanted[d] && !direction_[d]) {
            direction_[d] = true;
            fireDirection(static_cast<int>(d), out);
            nextRepeatMs_[d] = nowMs + (isPan(static_cast<int>(d)) ? options_.panDelayMs : options_.repeatDelayMs);
        } else if (!wanted[d]) {
            direction_[d] = false;
        }
    }
}

void Bridge::bubbleChanged(double nowMs, std::vector<Bytes>& out) {
    bubbleSeenMs_ = nowMs;
    if (bubblePressed_) updateDirections(nowMs, out);
}

void Bridge::bubbleReleased() {
    bubbleActive_ = false;
    bubblePressed_ = false;
    direction_.fill(false);
}

void Bridge::stepWindow(int step, std::vector<Bytes>& out) {
    lastTouched_ = -1;
    bool bank = options_.bankStep == BankStep::Bank;
    tap(step > 0 ? (bank ? BankRightNote : ChannelRightNote) : (bank ? BankLeftNote : ChannelLeftNote), out);
}

void Bridge::strip(int value, double nowMs, std::vector<Bytes>& out) {
    stripSeenMs_ = nowMs;
    if (!stripActive_) {
        stripActive_ = true;
        stripLast_ = value;
        return;
    }
    int delta = value - stripLast_;
    if (delta == 0) return;
    stripLast_ = value;
    int ticks = static_cast<int>(std::lround(std::abs(delta) * options_.jogPerStep));
    ticks = std::max(1, std::min(63, ticks));
    out.push_back(make(0xB0, JogCc, delta > 0 ? ticks : 64 + ticks));
}

void Bridge::fromSurface(const Bytes& message, double nowMs, std::vector<Bytes>& out) {
    if (options_.passThrough) {
        if (message.empty()) return;
        if (message.size() >= 3) {
            int kind = message[0] & 0xF0;
            if ((kind == 0x90 || kind == 0x80) && options_.blockedNotes[message[1] & 0x7F]) return;
            if (kind == 0xB0 && message[1] == layout_.stripCc) {
                strip(message[2], nowMs, out);
                return;
            }
        }
        out.push_back(message);
        if (message.size() >= 3 && (message[0] & 0xF0) == 0x90) notesDown_[message[1] & 0x7F] = message[2] > 0;
        if (message.size() >= 3 && (message[0] & 0xF0) == 0x80) notesDown_[message[1] & 0x7F] = false;
        if ((message[0] & 0xF0) == 0xE0 && message.size() >= 3 && (message[0] & 0x0F) < StripCount) sent_[static_cast<std::size_t>(message[0] & 0x0F)] = message[1] | (message[2] << 7);
        return;
    }

    if (message.size() < 3) return;
    int status = message[0] & 0xF0;
    int first = message[1];
    int second = message[2];
    if (status == 0x90 || status == 0x80) {
        bool down = status == 0x90 && second > 0;
        bool keyNote = first >= layout_.keyNoteFirst - 24 && first <= layout_.keyNoteFirst + KeyCount - 1 + 24;
        if (keyNote) {
            if (down) {
                flushPending(nowMs, out);
                pendingActive_ = true;
                pendingNote_ = first;
                pendingMs_ = nowMs;
            } else {
                noteReleased(first, nowMs, out);
            }
        } else if (first == layout_.playNote) {
            playHeld_ = down;
            out.push_back(noteMessage(PlayNote, down ? 127 : 0));
        } else if (first == layout_.recordNote) {
            recordHeld_ = down;
            out.push_back(noteMessage(RecordNote, down ? 127 : 0));
        }
        return;
    }
    if (status != 0xB0) return;
    int key = first - layout_.keyCcFirst;
    if (key >= 0 && key < KeyCount) {
        if (pendingActive_ && nowMs - pendingMs_ <= 100.0) {
            int shift = pendingNote_ - (layout_.keyNoteFirst + key);
            if (shift % 12 == 0 && std::abs(shift) <= 24) {
                noteShift_ = shift;
                pendingActive_ = false;
                noteOf_[static_cast<std::size_t>(key)] = pendingNote_;
                touch(key, true, nowMs, out);
            }
        }
        high_[static_cast<std::size_t>(key)] = second;
        updateFader(key, out);
        return;
    }
    key = first - 32 - layout_.keyCcFirst;
    if (key >= 0 && key < KeyCount) {
        low_[static_cast<std::size_t>(key)] = second;
        hasLow_[static_cast<std::size_t>(key)] = true;
        updateFader(key, out);
        return;
    }
    if (first == layout_.bubbleXCc) {
        bubbleX_ = second;
        bubbleChanged(nowMs, out);
    } else if (first == layout_.bubbleYCc) {
        bubbleY_ = second;
        bubbleChanged(nowMs, out);
    } else if (first == layout_.bubblePressureCc) {
        bubbleSeenMs_ = nowMs;
        if (second == 0) {
            bubbleReleased();
        } else {
            bubbleActive_ = true;
            if (!bubblePressed_) {
                bubblePressed_ = true;
                updateDirections(nowMs, out);
            }
        }
    } else if (first == layout_.stripCc) {
        strip(second, nowMs, out);
    }
}

void Bridge::tick(double nowMs, std::vector<Bytes>& out) {
    if (pendingActive_ && nowMs - pendingMs_ >= options_.keyResolveMs) flushPending(nowMs, out);
    if (stripActive_ && nowMs - stripSeenMs_ > options_.stripIdleMs) stripActive_ = false;
    if (bubbleActive_ && nowMs - bubbleSeenMs_ > options_.bubbleIdleMs) bubbleReleased();
    for (std::size_t d = 0; d < 4; ++d) {
        if (direction_[d] && nowMs >= nextRepeatMs_[d]) {
            fireDirection(static_cast<int>(d), out);
            nextRepeatMs_[d] = nowMs + (isPan(static_cast<int>(d)) ? options_.panRepeatMs : options_.repeatMs);
        }
    }
    for (std::size_t which = 0; which < 2; ++which) {
        if (buttonHeld_[which] && nowMs >= buttonNextMs_[which]) {
            stepWindow(which == 1 ? 1 : -1, out);
            buttonNextMs_[which] = nowMs + options_.buttonRepeatMs;
        }
    }
}

void Bridge::releaseAll(std::vector<Bytes>& out) {
    pendingActive_ = false;
    for (int key = 0; key < KeyCount; ++key) touch(key, false, 0.0, out);
    buttonHeld_.fill(false);
    stripActive_ = false;
    fine_ = false;
    shift_ = false;
    bubbleReleased();
}

void Bridge::fromHost(const Bytes& message, std::vector<Bytes>& out, int unit) {
    unit = unit == 0 ? 0 : 1;
    hosts_[static_cast<std::size_t>(unit)].feed(message);
    if (message.size() < 7 || message.front() != 0xF0 || message.back() != 0xF7) return;
    if (message[1] != 0x00 || message[2] != 0x00 || message[3] != 0x66) return;
    int device = message[4];
    int command = message[5];
    if (device != 0x14 && device != 0x15) return;
    if (command == 0x00) {
        Bytes body = surfaceSerial();
        const Bytes challenge{0x24, 0x53, 0x1A, 0x08};
        body.insert(body.end(), challenge.begin(), challenge.end());
        emit(unit, mackieSysex(device, 0x01, body), out);
    } else if (command == 0x02) {
        emit(unit, mackieSysex(device, 0x03, surfaceSerial()), out);
    } else if (command == 0x13) {
        emit(unit, mackieSysex(device, 0x14, Bytes{'V', '2', '.', '1', '4'}), out);
    }
}

Snapshot Bridge::snapshot() const {
    Snapshot state;
    for (std::size_t i = 0; i < state.fader.size(); ++i) state.fader[i] = std::max(0, sent_[i]);
    state.touched = touched_;
    state.stripActive = stripActive_;
    state.bubbleActive = bubbleActive_;
    state.bubbleX = bubblePressed_ ? bubbleX_ : 64;
    state.bubbleY = bubblePressed_ ? bubbleY_ : 64;
    state.playHeld = playHeld_;
    state.recordHeld = recordHeld_;
    state.noteShift = noteShift_;
    state.faderMode = mode_;
    state.fine = fine_;
    state.shift = shift_;
    state.selectedStrip = currentStrip();
    state.notesDown = notesDown_;
    return state;
}

void SurfaceState::feed(const Bytes& message) {
    if (message.empty()) return;
    if (message[0] == 0xF0) {
        if (message.size() < 9 || message.back() != 0xF7) return;
        if (message[1] != 0x00 || message[2] != 0x00 || message[3] != 0x66) return;
        if ((message[4] != 0x14 && message[4] != 0x15) || message[5] != 0x12) return;
        std::size_t offset = message[6];
        std::string whole = topLine + bottomLine;
        for (std::size_t i = 7; i + 1 < message.size() && offset < whole.size(); ++i, ++offset) {
            whole[offset] = static_cast<char>(message[i] & 0x7F);
        }
        topLine = whole.substr(0, 56);
        bottomLine = whole.substr(56, 56);
        for (std::size_t strip = 0; strip < names.size(); ++strip) names[strip] = trimRight(topLine.substr(strip * 7, 7));
        return;
    }
    if (message.size() < 2) return;
    int status = message[0] & 0xF0;
    int channel = message[0] & 0x0F;
    if (status == 0xE0 && message.size() >= 3) {
        if (channel <= MasterFader) {
            fader[static_cast<std::size_t>(channel)] = message[1] | (message[2] << 7);
            faderKnown[static_cast<std::size_t>(channel)] = true;
        }
    } else if (status == 0xD0) {
        int strip = (message[1] >> 4) & 0x07;
        int level = message[1] & 0x0F;
        meter[static_cast<std::size_t>(strip)] = level > 14 ? 0 : level;
    } else if ((status == 0x90 || status == 0x80) && message.size() >= 3) {
        int note = message[1];
        bool on = status == 0x90 && message[2] > 0;
        std::size_t index = static_cast<std::size_t>(note % 8);
        if (note < 8) record[index] = on;
        else if (note < 16) solo[index] = on;
        else if (note < 24) mute[index] = on;
        else if (note < 32) select[index] = on;
        else if (note == PlayNote) play = on;
        else if (note == StopNote) stop = on;
        else if (note == RecordNote) recording = on;
    }
}

bool isMixerLayout(const Bytes& blob) {
    constexpr int ScalerType = 14;
    constexpr int PlayKeysType = 23;
    if (blob.size() < 12 || (blob[0] != ScalerType && blob[0] != PlayKeysType)) return false;
    const ModuleBindings* bindings = moduleBindings(blob[0]);
    if (!bindings || !structureMatches(*bindings, blob)) return false;
    SettingValues values;
    try {
        values = decodeSettings(*bindings, blob);
    } catch (const std::exception&) {
        return false;
    }
    Layout layout;
    auto is = [&](const std::string& scope, const std::string& name, const std::string& wanted) {
        auto found = values.find({scope, name});
        return found != values.end() && found->second == wanted;
    };
    for (int key = 1; key <= KeyCount; ++key) {
        std::string scope = "KEY" + std::to_string(key);
        if (!is(scope, "Note", std::to_string(layout.keyNoteFirst + key - 1))) return false;
        if (!is(scope, "YControl", std::to_string(layout.keyCcFirst + key - 1))) return false;
        if (!is(scope, "YControlEnable", "1")) return false;
    }
    return is("BUTTON1", "Note", std::to_string(layout.playNote)) && is("BUTTON2", "Note", std::to_string(layout.recordNote)) &&
           is("BUBBLE", "ControlX", std::to_string(layout.bubbleXCc)) && is("BUBBLE", "ControlY", std::to_string(layout.bubbleYCc)) &&
           is("BUBBLE", "ControlZ", std::to_string(layout.bubblePressureCc)) && is("STRIP", "CC", std::to_string(layout.stripCc));
}

std::string noteName(int note) { return mackieNoteName(note); }

std::string describe(const Bytes& message) {
    if (message.empty()) return "";
    char text[160];
    if (message[0] == 0xF0) {
        bool mackie = message.size() >= 7 && message[1] == 0x00 && message[2] == 0x00 && message[3] == 0x66 && (message[4] == 0x14 || message[4] == 0x15);
        if (mackie) {
            int command = message[5];
            switch (command) {
                case 0x00: return "Mackie: device query";
                case 0x01: return "Mackie: connection query (serial and challenge)";
                case 0x02: return "Mackie: connection reply";
                case 0x03: return "Mackie: connection confirmed";
                case 0x13: return "Mackie: version request";
                case 0x14: return "Mackie: version reply";
                case 0x12: {
                    std::string shown;
                    for (std::size_t i = 7; i + 1 < message.size() && shown.size() < 40; ++i) shown += (message[i] >= 32 && message[i] < 127) ? static_cast<char>(message[i]) : '?';
                    std::snprintf(text, sizeof text, "Mackie: display text from position %d: \"%s\"", message.size() > 6 ? message[6] : 0, shown.c_str());
                    return text;
                }
                default:
                    std::snprintf(text, sizeof text, "Mackie: command 0x%02X, %zu bytes", command, message.size());
                    return text;
            }
        }
        std::string hex;
        for (std::size_t i = 0; i < message.size() && i < 12; ++i) {
            char byte[8];
            std::snprintf(byte, sizeof byte, "%02X ", message[i]);
            hex += byte;
        }
        std::snprintf(text, sizeof text, "SysEx, %zu bytes: %s%s", message.size(), hex.c_str(), message.size() > 12 ? "..." : "");
        return text;
    }
    int status = message[0] & 0xF0;
    int channel = (message[0] & 0x0F) + 1;
    int first = message.size() > 1 ? message[1] : 0;
    int second = message.size() > 2 ? message[2] : 0;
    switch (status) {
        case 0x90:
        case 0x80: {
            bool on = status == 0x90 && second > 0;
            std::string name = mackieNoteName(first);
            std::snprintf(text, sizeof text, "note %s %d%s%s (velocity %d, channel %d)", on ? "on" : "off", first, name.empty() ? "" : " = ", name.c_str(), second, channel);
            return text;
        }
        case 0xB0:
            std::snprintf(text, sizeof text, "controller %d = %d (channel %d)", first, second, channel);
            return text;
        case 0xE0:
            std::snprintf(text, sizeof text, "pitch bend %d (channel %d)", first | (second << 7), channel);
            return text;
        case 0xD0:
            std::snprintf(text, sizeof text, "channel pressure %d (channel %d)", first, channel);
            return text;
        case 0xA0:
            std::snprintf(text, sizeof text, "key pressure %d on note %d (channel %d)", second, first, channel);
            return text;
        case 0xC0:
            std::snprintf(text, sizeof text, "program change %d (channel %d)", first, channel);
            return text;
        default:
            std::snprintf(text, sizeof text, "message, %zu bytes", message.size());
            return text;
    }
}

}  // namespace joue::mixer
