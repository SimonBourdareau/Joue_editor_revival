#include "joue/live.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "joue/designer.hpp"

namespace joue {

namespace {

int widthInSlots(const Bytes& blob) {
    try {
        return Design::fromBytes(blob).slots();
    } catch (const std::exception&) {
        return 0;
    }
}

std::string buttonScript(const ElementRecord& e) {
    const Bytes& b = e.bytes;
    if (b.size() < 20) return {};
    std::size_t length = static_cast<std::size_t>(b[16] | (b[17] << 8));
    length = std::min(length, b.size() - 20);
    std::string text(b.begin() + 20, b.begin() + 20 + static_cast<std::ptrdiff_t>(length));
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
}

std::int16_t s16(const Bytes& b, std::size_t at) { return static_cast<std::int16_t>(b[at] | (b[at + 1] << 8)); }

float fraction(int value, int low, int high) {
    float f = high != low ? static_cast<float>(value - low) / static_cast<float>(high - low) : static_cast<float>(value) / 127.0f;
    return std::max(0.0f, std::min(1.0f, f));
}

}  // namespace

bool hasOctaveButtons(const ModuleBlob& blob) {
    for (const ElementRecord& e : blob.elements) {
        if (e.type() != element::Button) continue;
        std::string script = buttonScript(e);
        if (script.find(".4+12") != std::string::npos || script.find(".4-12") != std::string::npos) return true;
    }
    return false;
}

bool moduleMayAnswerFrom(int widthInSlots, int slot) {
    if (widthInSlots >= SlotCount) return slot == 1;
    return slot >= 1 && slot <= SlotCount;
}

std::vector<PlacedModule> placeModules(const std::array<SlotInfo, SlotCount>& slots) {
    std::vector<PlacedModule> placed;
    for (int s = 1; s <= SlotCount; ++s) {
        const SlotInfo& info = slots[static_cast<std::size_t>(s - 1)];
        if (!info.present) continue;
        int width = widthInSlots(info.blob);
        if (width < 1 || !moduleMayAnswerFrom(std::min(width, SlotCount), s)) continue;
        PlacedModule module;
        module.blob = ModuleBlob::parse(info.blob);
        module.raw = info.blob;
        module.slot = s;
        module.slots = std::min(width, SlotCount);
        if (module.slots >= SlotCount) {
            module.firstSlot = 1;
        } else if (module.slots == 1) {
            module.firstSlot = s;
        } else if (s == 1) {
            module.firstSlot = 1;
        } else if (s == SlotCount) {
            module.firstSlot = SlotCount - 1;
        } else {
            bool firstTaken = slots[0].present, lastFree = !slots[SlotCount - 1].present;
            module.firstSlot = (firstTaken && lastFree) ? 2 : 1;
        }
        placed.push_back(std::move(module));
    }
    std::sort(placed.begin(), placed.end(), [](const PlacedModule& a, const PlacedModule& b) { return a.firstSlot < b.firstSlot; });
    return placed;
}

bool SlotFilter::same(const std::array<SlotInfo, SlotCount>& a, const std::array<SlotInfo, SlotCount>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].present != b[i].present || (a[i].present && a[i].blob != b[i].blob)) return false;
    }
    return true;
}

bool SlotFilter::offer(const std::array<SlotInfo, SlotCount>& scan, bool urgent) {
    if (same(scan, accepted_)) {
        seen_ = 0;
        return false;
    }
    if (seen_ > 0 && same(scan, pending_)) ++seen_;
    else {
        pending_ = scan;
        seen_ = 1;
    }
    if (seen_ < 2 && !urgent) return false;
    accepted_ = scan;
    seen_ = 0;
    return true;
}

bool blobFitsAnnouncement(const Bytes& blob, const ModuleEvent& event) {
    return event.inserted && blob.size() >= InstrumentHeader::Length && static_cast<int>(blob[0]) == event.type;
}

void Announcements::apply(const ModuleEvent& event) {
    if (event.slot < 1 || event.slot > SlotCount) return;
    std::size_t i = static_cast<std::size_t>(event.slot - 1);
    events_[i] = event;
    if (event.inserted) ++stamps_[i];
    ++changes_[i];
    any_ = true;
}

bool Announcements::present(int slot) const { return slot >= 1 && slot <= SlotCount && events_[static_cast<std::size_t>(slot - 1)].inserted; }

ModuleEvent Announcements::event(int slot) const { return slot >= 1 && slot <= SlotCount ? events_[static_cast<std::size_t>(slot - 1)] : ModuleEvent(); }

unsigned Announcements::changes(int slot) const { return slot >= 1 && slot <= SlotCount ? changes_[static_cast<std::size_t>(slot - 1)] : 0; }

bool temporaryLayoutOutlived(const std::array<unsigned, SlotCount>& sent, const Announcements& now) {
    for (int slot = 1; slot <= SlotCount; ++slot) {
        if (now.changes(slot) != sent[static_cast<std::size_t>(slot - 1)]) return true;
    }
    return false;
}

unsigned Announcements::stamp(int slot) const { return slot >= 1 && slot <= SlotCount ? stamps_[static_cast<std::size_t>(slot - 1)] : 0; }

void Announcements::clear() {
    events_ = {};
    stamps_ = {};
    changes_ = {};
    any_ = false;
}

void SlotScanner::forget() {
    for (Known& k : known_) k = Known();
}

std::array<SlotInfo, SlotCount> SlotScanner::scan(const SlotFilter& filter) {
    std::array<SlotInfo, SlotCount> slots;
    for (int slot = 1; slot <= SlotCount; ++slot) {
        Known& known = known_[static_cast<std::size_t>(slot - 1)];
        client_.setTimeout(std::chrono::milliseconds(250));
        auto serial = device_.moduleSerial(slot);
        // a module that was there is asked once more before it is taken for gone
        if (!serial && filter.wasPresent(slot)) serial = device_.moduleSerial(slot);
        if (!serial) {
            known = Known();
            continue;
        }
        if (known.serial == *serial && !known.blob.empty()) {
            slots[static_cast<std::size_t>(slot - 1)] = {true, known.blob};
            continue;
        }
        client_.setTimeout(std::chrono::milliseconds(2500));
        ++blobReads_;
        auto blob = device_.readModule(slot);
        if (!blob) blob = device_.readModule(slot);
        if (!blob || blob->size() < InstrumentHeader::Length) continue;
        try {
            ModuleBlob::parse(*blob);
        } catch (const std::exception&) {
            continue;
        }
        known.serial = *serial;
        known.blob = *blob;
        slots[static_cast<std::size_t>(slot - 1)] = {true, *blob};
    }
    // one tag read through two readers is one module, kept in the lowest slot
    for (std::size_t i = 0; i < slots.size(); ++i) {
        for (std::size_t j = i + 1; j < slots.size(); ++j) {
            if (!slots[i].present || !slots[j].present) continue;
            if (slots[i].blob == slots[j].blob || known_[i].serial == known_[j].serial) {
                slots[j] = SlotInfo();
                known_[j] = Known();
            }
        }
    }
    return slots;
}

void TouchTracker::markTouched(const Target& target) {
    lastTouched_.module = target.module;
    lastTouched_.record = target.record;
    lastTouched_.sequence = ++touchSequence_;
}

void TouchTracker::setModules(const std::vector<PlacedModule>& modules) {
    std::vector<std::pair<int, Bytes>> signature;
    for (const PlacedModule& m : modules) signature.emplace_back(m.firstSlot * 8 + m.slots, m.blob.toBytes());
    if (signature == signature_ && !targets_.empty()) return;
    signature_ = std::move(signature);
    targets_.clear();
    active_.clear();
    unmatched_ = 0;
    lastUnmatched_ = -1;
    shifts_.assign(modules.size(), 0);
    std::size_t moduleIndex = 0;
    for (const PlacedModule& module : modules) {
        float left = static_cast<float>(module.firstSlot - 1) * SlotWidthMm;
        float right = left + static_cast<float>(module.slots) * SlotWidthMm;
        bool mpe = (module.blob.header.options & 1) != 0;
        bool shiftable = hasOctaveButtons(module.blob);
        int bendRange = 48;
        for (const ElementRecord& e : module.blob.elements) {
            if (e.type() == element::Glide && e.bytes.size() > 16) bendRange = e.bytes[16];
        }
        std::size_t first = targets_.size();
        for (std::size_t record = 0; record < module.blob.elements.size(); ++record) {
            const ElementRecord& e = module.blob.elements[record];
            if (e.isScript() || e.bytes.size() < ElementHeader::Length) continue;
            ElementHeader h = readElementHeader(e.bytes.data());
            Target t;
            t.module = moduleIndex;
            t.record = record;
            t.shiftable = shiftable;
            t.type = h.type;
            t.x = left + h.x;
            t.y = h.y;
            t.w = h.width;
            t.h = h.height;
            t.mpe = mpe;
            t.bendRange = bendRange;
            t.moduleLeft = left;
            t.moduleRight = right;
            const Bytes& b = e.bytes;
            if (h.type == element::Pad && b.size() >= 20) {
                t.note = b[16];
                t.channel = b[17];
            } else if (h.type == element::Key && b.size() >= 28) {
                t.note = b[16];
                t.channel = b[17];
                t.yMin = s16(b, 20);
                t.yMax = s16(b, 22);
                if (h.version == 2) t.yControl = b[26];
            } else if (h.type == element::String && b.size() >= 22) {
                t.note = b[16];
                t.channel = b[17];
                t.noteHigh = t.note + b[18];
            } else if (h.type == element::Strip && b.size() >= 24) {
                t.cc[0] = b[16];
                t.ccChannel[0] = b[17];
                t.ccMin[0] = s16(b, 20);
                t.ccMax[0] = s16(b, 22);
            } else if (h.type == element::Area && h.version == 2 && b.size() >= 46) {
                for (int a = 0; a < 3; ++a) {
                    t.cc[a] = b[static_cast<std::size_t>(16 + a)];
                    t.ccChannel[a] = b[static_cast<std::size_t>(19 + a)];
                    t.ccMin[a] = s16(b, static_cast<std::size_t>(34 + 2 * a));
                    t.ccMax[a] = s16(b, static_cast<std::size_t>(40 + 2 * a));
                }
            } else {
                continue;
            }
            targets_.push_back(t);
        }
        // the horizontal distance one semitone corresponds to, from the notes at the two ends of the module
        const Target* lowest = nullptr;
        const Target* highest = nullptr;
        for (std::size_t i = first; i < targets_.size(); ++i) {
            const Target& t = targets_[i];
            if (t.note < 0 || (t.type != element::Pad && t.type != element::Key)) continue;
            if (!lowest || t.note < lowest->note) lowest = &t;
            if (!highest || t.note > highest->note) highest = &t;
        }
        if (lowest && highest && highest->note > lowest->note) {
            float spacing = ((highest->x + highest->w / 2) - (lowest->x + lowest->w / 2)) / static_cast<float>(highest->note - lowest->note);
            for (std::size_t i = first; i < targets_.size(); ++i) targets_[i].semitoneMm = std::max(0.5f, std::fabs(spacing));
        }
        ++moduleIndex;
    }
}

void TouchTracker::setOctaveShift(std::size_t module, int semitones) {
    if (module < shifts_.size()) shifts_[module] = std::max(-60, std::min(60, (semitones / 12) * 12));
}

TouchTracker::Active* TouchTracker::findNote(int channel, int note) {
    for (Active& a : active_) {
        if (a.note == note && a.channel == channel) return &a;
    }
    return nullptr;
}

TouchTracker::Active* TouchTracker::findKey(int key) {
    for (Active& a : active_) {
        if (a.timed && a.key == key) return &a;
    }
    return nullptr;
}

void TouchTracker::clear() { active_.clear(); }

void TouchTracker::feed(const Bytes& m, double nowMs) {
    if (m.size() < 2) return;
    int kind = m[0] & 0xF0, channel = (m[0] & 0x0F) + 1;
    int d1 = m[1], d2 = m.size() > 2 ? m[2] : 0;
    if (kind == 0x90 && d2 > 0) {
        // the notes carry the module's octave shift: a note that fits an element at the shift assumed so far is taken as it is; one that
        // fits nowhere moves the shift of a module to the nearest one at which it fits
        const Target* hit = nullptr;
        int element = d1;
        auto fits = [&](const Target& t, int note) {
            bool noteType = t.type == element::Pad || t.type == element::Key || t.type == element::String;
            if (!noteType || t.note < 0) return false;
            int high = t.noteHigh >= 0 ? t.noteHigh : t.note;
            if (note < t.note || note > high) return false;
            return t.mpe || t.channel == channel;
        };
        for (const Target& t : targets_) {
            int note = d1 - shifts_[t.module];
            if (fits(t, note)) {
                hit = &t;
                element = note;
                break;
            }
        }
        for (int octaves = 1; !hit && octaves <= 5; ++octaves) {
            for (int sign : {1, -1}) {
                for (const Target& t : targets_) {
                    int shift = shifts_[t.module] + sign * octaves * 12;
                    if (!t.shiftable || shift < -60 || shift > 60) continue;
                    int note = d1 - shift;
                    if (fits(t, note)) {
                        shifts_[t.module] = shift;
                        hit = &t;
                        element = note;
                        break;
                    }
                }
                if (hit) break;
            }
        }
        if (!hit) {
            ++unmatched_;
            lastUnmatched_ = d1;
            return;
        }
        const Target& t = *hit;
        int high = t.noteHigh >= 0 ? t.noteHigh : t.note;
        Active* a = findNote(channel, d1);
        if (!a) {
            active_.push_back(Active());
            a = &active_.back();
            a->touch.id = nextId_++;
            markTouched(t);
        }
        a->target = &t;
        a->channel = channel;
        a->note = d1;
        float cx = t.x + t.w / 2;
        if (t.type == element::String && high > t.note) cx = t.x + t.w * static_cast<float>(element - t.note) / static_cast<float>(high - t.note);
        a->baseX = cx;
        a->touch.x = cx;
        a->touch.y = t.y + t.h / 2;
        a->touch.pressure = static_cast<float>(d2) / 127.0f;
        a->touch.label = noteName(d1);
        a->lastMs = nowMs;
        a->timed = false;
        return;
    }
    if (kind == 0x80 || kind == 0x90) {
        for (std::size_t i = 0; i < active_.size(); ++i) {
            if (!active_[i].timed && active_[i].note == d1 && active_[i].channel == channel) {
                active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        return;
    }
    if (kind == 0xD0) {
        for (Active& a : active_) {
            if (!a.timed && a.channel == channel) {
                a.touch.pressure = static_cast<float>(d1) / 127.0f;
                a.lastMs = nowMs;
            }
        }
        return;
    }
    if (kind == 0xA0) {
        if (Active* a = findNote(channel, d1)) {
            a->touch.pressure = static_cast<float>(d2) / 127.0f;
            a->lastMs = nowMs;
        }
        return;
    }
    if (kind == 0xE0) {
        int bend = ((d2 << 7) | d1) - 8192;
        for (Active& a : active_) {
            if (a.timed || a.channel != channel || !a.target) continue;
            float dx = static_cast<float>(bend) / 8192.0f * static_cast<float>(a.target->bendRange) * a.target->semitoneMm;
            a.touch.x = std::max(a.target->moduleLeft, std::min(a.target->moduleRight, a.baseX + dx));
            a.lastMs = nowMs;
        }
        return;
    }
    if (kind != 0xB0) return;
    if (d1 == 120 || d1 == 123) {
        // all sound off, all notes off: nothing is held on that channel any more
        for (std::size_t i = 0; i < active_.size();) {
            if (!active_[i].timed && active_[i].channel == channel) active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
        return;
    }
    for (Active& a : active_) {
        if (!a.timed && a.channel == channel && a.target && a.target->yControl == d1) {
            a.touch.y = a.target->y + a.target->h * fraction(d2, a.target->yMin, a.target->yMax);
            a.lastMs = nowMs;
        }
    }
    for (std::size_t ti = 0; ti < targets_.size(); ++ti) {
        const Target& t = targets_[ti];
        if (t.type == element::Strip && t.cc[0] == d1 && t.ccChannel[0] == channel) {
            Active* a = findKey(static_cast<int>(ti));
            if (!a) {
                active_.push_back(Active());
                a = &active_.back();
                a->touch.id = nextId_++;
                a->key = static_cast<int>(ti);
                a->timed = true;
                a->target = &t;
                markTouched(t);
            }
            float f = fraction(d2, t.ccMin[0], t.ccMax[0]);
            if (t.w >= t.h) {
                a->touch.x = t.x + t.w * f;
                a->touch.y = t.y + t.h / 2;
            } else {
                a->touch.x = t.x + t.w / 2;
                a->touch.y = t.y + t.h * f;
            }
            a->touch.pressure = 0.5f;
            a->touch.label = "CC" + std::to_string(d1);
            a->lastMs = nowMs;
        } else if (t.type == element::Area) {
            for (int axis = 0; axis < 3; ++axis) {
                if (t.cc[axis] != d1 || t.ccChannel[axis] != channel) continue;
                Active* a = findKey(static_cast<int>(ti));
                if (!a) {
                    active_.push_back(Active());
                    a = &active_.back();
                    a->touch.id = nextId_++;
                    a->key = static_cast<int>(ti);
                    a->timed = true;
                    a->target = &t;
                    a->touch.label = "XYZ";
                    markTouched(t);
                }
                float f = fraction(d2, t.ccMin[axis], t.ccMax[axis]);
                if (axis == 0) a->areaX = f;
                if (axis == 1) a->areaY = f;
                if (axis == 2) a->areaZ = f;
                a->touch.x = t.x + t.w * a->areaX;
                a->touch.y = t.y + t.h * a->areaY;
                a->touch.pressure = a->areaZ;
                a->lastMs = nowMs;
            }
        }
    }
}

std::vector<Touch> TouchTracker::touches(double nowMs) {
    for (std::size_t i = 0; i < active_.size();) {
        double age = nowMs - active_[i].lastMs;
        if ((active_[i].timed && age > 700.0) || age > 600000.0) active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    std::vector<Touch> out;
    for (const Active& a : active_) out.push_back(a.touch);
    return out;
}

}  // namespace joue
