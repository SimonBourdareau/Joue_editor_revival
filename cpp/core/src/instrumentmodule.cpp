#include "joue/instrumentmodule.hpp"
#include "joue/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <cctype>
#include <functional>

#include "joue/designer.hpp"

namespace joue {

namespace {

std::vector<std::string> scriptCommands(const Bytes& blob) {
    std::vector<std::string> out;
    ModuleBlob parsed = ModuleBlob::parse(blob);
    if (parsed.elements.empty() || !parsed.elements.back().isScript()) return out;
    const Bytes& b = parsed.elements.back().bytes;
    std::string text(b.begin() + 4, b.end());
    std::string current;
    for (char c : text) {
        if (c == '\0') break;
        if (c == ';') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string joinScript(const std::vector<std::string>& commands) {
    std::string text;
    for (const std::string& c : commands) text += c + ";";
    return text;
}

// "key=value" -> position in the command list, or -1
int findCommand(const std::vector<std::string>& commands, const std::string& key) {
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].compare(0, key.size() + 1, key + "=") == 0) return static_cast<int>(i);
    }
    return -1;
}

void putCommand(std::vector<std::string>& commands, const std::string& command) {
    std::string key = command.substr(0, command.find('='));
    int at = findCommand(commands, key);
    if (at >= 0) commands[static_cast<std::size_t>(at)] = command;
    else if (std::find(commands.begin(), commands.end(), command) == commands.end()) commands.push_back(command);
}

int roundInt(double v) { return static_cast<int>(std::lround(v)); }

std::string stripPart(const std::string& name) {
    std::size_t at = name.find('#');
    return at == std::string::npos ? name : name.substr(0, at);
}

void fixChecksum(Bytes& blob) {
    std::uint8_t x = 0;
    for (std::size_t i = 8; i < blob.size(); ++i) x ^= blob[i];
    blob[4] = static_cast<std::uint8_t>(blob[3] ^ x);
}

// Absolute position of a cell, or the size of the blob when the record does not exist.
std::size_t position(const std::vector<std::size_t>& starts, const ByteCell& c, std::size_t size) {
    if (c.record < 0) return static_cast<std::size_t>(c.offset) < InstrumentHeader::Length ? static_cast<std::size_t>(c.offset) : size;
    if (static_cast<std::size_t>(c.record) >= starts.size()) return size;
    std::size_t p = starts[static_cast<std::size_t>(c.record)] + static_cast<std::size_t>(c.offset);
    return p < size ? p : size;
}

std::string recordText(const Bytes& rec) {
    if (rec.size() < 20 || rec[0] != element::Button) return {};
    std::size_t length = std::min<std::size_t>(static_cast<std::size_t>(rec[16] | (rec[17] << 8)), rec.size() - 20);
    std::string text(rec.begin() + 20, rec.begin() + 20 + static_cast<std::ptrdiff_t>(length));
    std::size_t end = text.find('\0');
    return end == std::string::npos ? text : text.substr(0, end);
}

// '#' in the pattern stands for one or more digits
bool matchPattern(const std::string& text, const std::string& pattern) {
    std::size_t t = 0;
    for (char c : pattern) {
        if (c == '#') {
            std::size_t start = t;
            while (t < text.size() && std::isdigit(static_cast<unsigned char>(text[t]))) ++t;
            if (t == start) return false;
        } else {
            if (t >= text.size() || text[t] != c) return false;
            ++t;
        }
    }
    return t == text.size();
}

int currentVariant(const FunctionTable& table, const ModuleBlob& parsed) {
    if (table.record < 0 || static_cast<std::size_t>(table.record) >= parsed.elements.size()) return -1;
    const Bytes& rec = parsed.elements[static_cast<std::size_t>(table.record)].bytes;
    if (rec.size() < 16) return -1;
    for (std::size_t i = 0; i < table.variants.size(); ++i) {
        const FunctionVariant& v = table.variants[i];
        if (rec[0] != v.type || rec[1] != v.version) continue;
        if (!v.text.empty() && !matchPattern(recordText(rec), v.text)) continue;
        bool ok = true;
        for (const auto& d : v.disc) ok = ok && static_cast<std::size_t>(d.first) < rec.size() && rec[static_cast<std::size_t>(d.first)] == d.second;
        if (ok) return static_cast<int>(i);
    }
    return -1;
}

const FunctionTable* tableOf(const ModuleBindings& bindings, const std::string& scope) {
    for (const FunctionTable& t : bindings.functions) {
        if (t.scope == scope) return &t;
    }
    return nullptr;
}

const FunctionVariant* variantOf(const FunctionTable& table, const std::string& value) {
    for (const FunctionVariant& v : table.variants) {
        if (v.value == value) return &v;
    }
    return nullptr;
}

// The blob read once: its records, where they start, and the Action each element with a choice has.
struct State {
    ModuleBlob parsed;
    std::vector<std::size_t> starts;
    std::map<std::string, std::string> action;  // element -> Action, absent when the record is none of those the table knows
};

State stateOf(const ModuleBindings& bindings, const Bytes& blob) {
    State st;
    st.parsed = ModuleBlob::parse(blob);
    std::size_t at = InstrumentHeader::Length;
    for (const ElementRecord& e : st.parsed.elements) {
        if (e.isScript()) break;
        st.starts.push_back(at);
        at += e.bytes.size();
    }
    for (const FunctionTable& t : bindings.functions) {
        int v = currentVariant(t, st.parsed);
        if (v >= 0) st.action[t.scope] = t.variants[static_cast<std::size_t>(v)].value;
    }
    return st;
}

// A setting that belongs to an Action only counts while its element has that Action.
bool bindingActive(const Binding& b, const State& st) {
    if (b.when.empty()) return true;
    auto it = st.action.find(b.scope);
    return it != st.action.end() && it->second == b.when;
}

// A cell is only read or written in a record of the kind it was measured in: the note byte of a pad is not touched when that element
// has become a button.
bool cellFits(const ModuleBindings& bindings, const Binding& b, int record, const State& st) {
    if (record < 0) return true;
    if (static_cast<std::size_t>(record) >= st.parsed.elements.size()) return false;
    const Bytes& rec = st.parsed.elements[static_cast<std::size_t>(record)].bytes;
    int type = -1, version = -1;
    if (!b.when.empty()) {
        const FunctionTable* t = tableOf(bindings, b.scope);
        const FunctionVariant* v = t ? variantOf(*t, b.when) : nullptr;
        if (!v) return false;
        type = v->type;
        version = v->version;
    } else if (static_cast<std::size_t>(record) < bindings.baselineTypes.size()) {
        type = bindings.baselineTypes[static_cast<std::size_t>(record)].first;
        version = bindings.baselineTypes[static_cast<std::size_t>(record)].second;
    }
    return type < 0 || (rec.size() > 1 && rec[0] == type && rec[1] == version);
}

// The depth rule that applies to the element of a record, found by the first letters of its name (KEY1, BUTTON2, OCTAVE+...).
const DepthRule* depthRuleFor(const ModuleBindings& bindings, std::size_t record) {
    if (record >= bindings.names.size()) return nullptr;
    const std::string& name = bindings.names[record];
    for (const DepthRule& r : bindings.depthRules) {
        if (name.compare(0, r.prefix.size(), r.prefix) != 0) continue;
        bool rest = true;
        for (std::size_t i = r.prefix.size(); i < name.size(); ++i) rest = rest && (std::isdigit(static_cast<unsigned char>(name[i])) || name[i] == '+' || name[i] == '-' || name[i] == '_');
        if (rest) return &r;
    }
    return nullptr;
}

// A group (ALL_KEYS, ALL_PADS) stands for its members (KEY1 to KEY17, PAD1 to PAD16): the settings of the group are those that every member
// has, and a change of one is made in all of them. "Note" is left out: for the group it is the first note, with the following ones after it.
struct Group {
    std::string name;
    std::vector<std::string> members;
};

std::vector<Group> groupsOf(const ModuleBindings& bindings) {
    std::vector<std::pair<std::string, std::vector<std::string>>> bases;
    for (const std::string& name : bindings.names) {
        std::string base = name;
        while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
        if (base.empty() || base == name) continue;
        auto it = std::find_if(bases.begin(), bases.end(), [&](const auto& b) { return b.first == base; });
        if (it == bases.end()) {
            bases.push_back({base, {}});
            it = bases.end() - 1;
        }
        if (std::find(it->second.begin(), it->second.end(), name) == it->second.end()) it->second.push_back(name);
    }
    // a group goes through its members only when they have in the table everything that the group has (the Grand Clavier has a group with
    // its own settings and keys with hardly any: it keeps them)
    auto namesOf = [&](const std::string& scope) {
        std::set<std::string> names;
        for (const Binding& binding : bindings.bindings) {
            if (binding.scope != scope) continue;
            names.insert(stripPart(binding.name));
            if (!binding.pair.empty()) names.insert(binding.dirName);
        }
        return names;
    };
    std::vector<Group> groups;
    for (const auto& b : bases) {
        if (b.second.size() < 3) continue;
        std::set<std::string> common = namesOf(b.second.front());
        for (const std::string& member : b.second) {
            std::set<std::string> mine = namesOf(member), kept;
            std::set_intersection(common.begin(), common.end(), mine.begin(), mine.end(), std::inserter(kept, kept.begin()));
            common = kept;
        }
        common.erase("Note");
        if (common.empty()) continue;
        std::set<std::string> own = namesOf("ALL_" + b.first + "S");
        own.erase("Note");
        if (std::includes(common.begin(), common.end(), own.begin(), own.end())) groups.push_back({"ALL_" + b.first + "S", b.second});
    }
    return groups;
}

// The names of the settings that every member of the group has.
std::set<std::string> groupSettings(const ModuleBindings& bindings, const Group& group, const std::set<std::pair<std::string, std::string>>& bound) {
    std::set<std::string> common;
    bool first = true;
    for (const std::string& member : group.members) {
        std::set<std::string> mine;
        for (const auto& entry : bound) {
            if (entry.first == member && entry.second != "Note" && entry.second != "Function") mine.insert(entry.second);
        }
        if (first) {
            common = mine;
            first = false;
        } else {
            std::set<std::string> kept;
            std::set_intersection(common.begin(), common.end(), mine.begin(), mine.end(), std::inserter(kept, kept.begin()));
            common = kept;
        }
    }
    (void)bindings;
    return common;
}

// The scopes of the groups that are handled through their members: the bindings that the table has for such a scope itself (made for every
// record of the same kind, which in the Play modules includes the octave buttons) stand aside, except for "Note".
std::set<std::string> groupScopes(const ModuleBindings& bindings) {
    std::set<std::string> scopes;
    for (const Group& group : groupsOf(bindings)) scopes.insert(group.name);
    return scopes;
}

// The notes of the keys of a scale: the first note, then each key adds the step of the scale that follows (the steps go round).
std::vector<int> scaleNotes(const ScaleSupport& scale, std::size_t index, int first) {
    std::vector<int> notes;
    int note = first;
    for (int k = 0; k < scale.keys; ++k) {
        notes.push_back(std::min(note, 127));
        const std::vector<int>& steps = scale.steps[index];
        if (!steps.empty()) note += steps[static_cast<std::size_t>(k) % steps.size()];
    }
    return notes;
}

// The index of the first scale whose notes are those of the keys, or -1 when the keys follow none of them (a custom mapping).
int scaleOfNotes(const ScaleSupport& scale, const std::vector<int>& notes) {
    if (notes.size() != static_cast<std::size_t>(scale.keys)) return -1;
    for (std::size_t s = 0; s < scale.steps.size(); ++s) {
        if (scale.steps[s].empty()) continue;
        if (scaleNotes(scale, s, notes[0]) == notes) return static_cast<int>(s);
    }
    return -1;
}

bool keyNotes(const ScaleSupport& scale, const SettingValues& values, std::vector<int>& notes) {
    notes.clear();
    for (int k = 1; k <= scale.keys; ++k) {
        auto it = values.find({"KEY" + std::to_string(k), "Note"});
        if (it == values.end()) return false;
        notes.push_back(std::atoi(it->second.c_str()));
    }
    return true;
}

// A range and its direction, stored together: "swap" is two bytes that change places when the direction is the second value (a control
// change), "sign" is a 16-bit number that is negative for the second value (a pitch bend).
struct PairValue {
    bool known = false;
    int lo = 0, hi = 0, direction = 0;
};

PairValue readPair(const ModuleBindings& bindings, const Binding& b, const Bytes& blob, const State& st) {
    PairValue v;
    std::size_t need = b.pair == "swap14" ? 4 : 2;
    if (b.bytes.size() < need) return v;
    std::size_t at[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < need; ++i) {
        if (!cellFits(bindings, b, b.bytes[i].record, st)) return v;
        at[i] = position(st.starts, b.bytes[i], blob.size());
        if (at[i] >= blob.size()) return v;
    }
    v.known = true;
    if (b.pair == "swap14") {
        int first = blob[at[0]] | ((blob[at[1]] & 63) << 8), second = blob[at[2]] | ((blob[at[3]] & 63) << 8);
        v.direction = first > second ? 1 : 0;
        v.lo = (std::min(first, second) + 64) / 129;
        v.hi = (std::max(first, second) + 64) / 129;
        return v;
    }
    int x = blob[at[0]], y = blob[at[1]];
    if (b.pair == "swap") {
        v.direction = x > y ? 1 : 0;
        v.lo = std::min(x, y);
        v.hi = std::max(x, y);
    } else {
        int number = static_cast<int>(static_cast<std::int16_t>(x | (y << 8)));
        v.direction = number < 0 ? 1 : 0;
        v.hi = std::abs(number);
    }
    return v;
}

void writePair(const ModuleBindings& bindings, const Binding& b, Bytes& blob, const State& st, const PairValue& v) {
    std::size_t need = b.pair == "swap14" ? 4 : 2;
    if (b.bytes.size() < need) return;
    std::size_t at[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < need; ++i) {
        if (!cellFits(bindings, b, b.bytes[i].record, st)) return;
        at[i] = position(st.starts, b.bytes[i], blob.size());
        if (at[i] >= blob.size()) return;
    }
    if (b.pair == "swap14") {
        int lo = std::max(0, std::min(127, v.lo)) * 129, hi = std::max(0, std::min(127, v.hi)) * 129;
        int first = v.direction == 0 ? lo : hi, second = v.direction == 0 ? hi : lo;
        blob[at[0]] = static_cast<std::uint8_t>(first & 0xFF);
        blob[at[1]] = static_cast<std::uint8_t>((blob[at[1]] & ~63) | ((first >> 8) & 63));
        blob[at[2]] = static_cast<std::uint8_t>(second & 0xFF);
        blob[at[3]] = static_cast<std::uint8_t>((blob[at[3]] & ~63) | ((second >> 8) & 63));
        return;
    }
    if (b.pair == "swap") {
        int lo = std::max(0, std::min(255, v.lo)), hi = std::max(0, std::min(255, v.hi));
        blob[at[0]] = static_cast<std::uint8_t>(v.direction == 0 ? lo : hi);
        blob[at[1]] = static_cast<std::uint8_t>(v.direction == 0 ? hi : lo);
    } else {
        int range = std::max(0, std::min(32767, v.hi));
        int number = v.direction == 0 ? range : -range;
        blob[at[0]] = static_cast<std::uint8_t>(number & 0xFF);
        blob[at[1]] = static_cast<std::uint8_t>((number >> 8) & 0xFF);
    }
}

// The record of a button with a command of its script text set.
Bytes withTextValue(const Bytes& rec, const std::string& key, long value) {
    std::vector<std::string> commands;
    std::string current;
    for (char c : recordText(rec)) {
        if (c == ';') {
            if (!current.empty()) commands.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) commands.push_back(current);
    putCommand(commands, key + "=" + std::to_string(value));
    std::string text = joinScript(commands);
    std::size_t padded = (text.size() + 3) / 4 * 4;
    Bytes out(rec.begin(), rec.begin() + 16);
    out.push_back(static_cast<std::uint8_t>(padded & 0xFF));
    out.push_back(static_cast<std::uint8_t>(padded >> 8));
    out.push_back(0);
    out.push_back(0);
    out.insert(out.end(), text.begin(), text.end());
    out.resize(20 + padded, 0);
    return out;
}

}  // namespace

bool structureMatches(const ModuleBindings& bindings, const Bytes& blob) {
    try {
        ModuleBlob parsed = ModuleBlob::parse(blob);
        if (parsed.header.type != bindings.type) return false;
        std::vector<std::string> expected;
        std::string part;
        for (char c : bindings.structure) {
            if (c == ',') {
                expected.push_back(part);
                part.clear();
            } else {
                part += c;
            }
        }
        expected.push_back(part);
        std::size_t index = 0;
        for (const ElementRecord& e : parsed.elements) {
            if (e.isScript()) continue;
            if (index >= expected.size()) return false;
            const FunctionTable* table = nullptr;
            for (const FunctionTable& t : bindings.functions) {
                if (static_cast<std::size_t>(t.record) == index) table = &t;
            }
            if (table) {
                // an element with a choice of Action may have the record of any of them
                bool known = false;
                for (const FunctionVariant& v : table->variants) known = known || (e.bytes[0] == v.type && e.bytes[1] == v.version);
                if (!known) return false;
            } else {
                std::string signature = std::to_string(e.bytes[0]) + ":" + std::to_string(e.bytes[1]) + ":" + (e.bytes[0] == element::Button ? std::string("*") : std::to_string(e.bytes.size()));
                // the two octave buttons of the Play modules are pad records, those of the Pro modules script buttons; the cells of the table
                // are only written into records of the kind they were measured on, so the pad form is accepted
                bool octavePad = index < bindings.names.size() && bindings.names[index].compare(0, 6, "OCTAVE") == 0 && e.bytes[0] == 2 && expected[index].compare(0, 4, "4:1:") == 0;
                if (signature != expected[index] && !octavePad) return false;
            }
            ++index;
        }
        return index == expected.size();
    } catch (const std::exception&) {
        return false;
    }
}

SettingValues decodeSettings(const ModuleBindings& bindings, const Bytes& blob) {
    SettingValues out;
    State st = stateOf(bindings, blob);
    for (const auto& a : st.action) out[{a.first, "Function"}] = a.second;
    std::vector<std::string> commands = scriptCommands(blob);
    if (!bindings.depthRules.empty()) {
        // Pressure range: every element that follows a rule must have the Low depth, or every one the High depth
        int found = -2;
        for (std::size_t i = 0; i < st.parsed.elements.size(); ++i) {
            const DepthRule* r = depthRuleFor(bindings, i);
            if (!r || st.parsed.elements[i].bytes.size() < 14) continue;
            int depth = st.parsed.elements[i].bytes[13];
            int which = depth == r->low ? 0 : depth == r->high ? 1 : -1;
            found = found == -2 ? which : (found == which ? found : -1);
        }
        if (found >= 0) out[{"M", "Dynamic"}] = bindings.depthValues[static_cast<std::size_t>(found)];
    }
    if (!bindings.aftertouchPrefix.empty()) {
        bool poly = findCommand(commands, bindings.aftertouchPrefix + ".22") >= 0, channel = findCommand(commands, bindings.aftertouchPrefix + ".23") >= 0;
        out[{"M", "Aftertouch"}] = poly || channel ? "1" : "0";
        out[{"M", "AftertouchType"}] = channel ? "Channel" : "Polyphonic";
    }
    if (bindings.expertAlways) out[{"M", "Expert"}] = "1";
    const std::set<std::string> grouped = groupScopes(bindings);
    for (const Binding& b : bindings.bindings) {
        if (!bindingActive(b, st)) continue;
        if (grouped.count(b.scope) && stripPart(b.name) != "Note") continue;
        auto key = std::make_pair(b.scope, b.name);
        if (!b.pair.empty()) {
            PairValue v = readPair(bindings, b, blob, st);
            if (!v.known) continue;
            if (b.pair != "sign") out[{b.scope, b.name + "#lo"}] = std::to_string(v.lo);
            out[{b.scope, b.name + "#hi"}] = std::to_string(v.hi);
            out[{b.scope, b.dirName}] = b.dirValues[static_cast<std::size_t>(v.direction)];
            continue;
        }
        if (!b.text.empty()) {
            const FunctionTable* table = tableOf(bindings, b.scope);
            if (!table || b.text[0].slope == 0 || static_cast<std::size_t>(table->record) >= st.parsed.elements.size()) continue;
            std::vector<std::string> text;
            std::string current;
            for (char c : recordText(st.parsed.elements[static_cast<std::size_t>(table->record)].bytes)) {
                if (c == ';') {
                    if (!current.empty()) text.push_back(current);
                    current.clear();
                } else {
                    current += c;
                }
            }
            if (!current.empty()) text.push_back(current);
            int at = findCommand(text, b.text[0].key);
            if (at < 0) continue;
            int value = std::atoi(text[static_cast<std::size_t>(at)].c_str() + b.text[0].key.size() + 1);
            out[key] = std::to_string(b.base + roundInt((value - b.text[0].base) / b.text[0].slope));
            continue;
        }
        if (b.numeric) {
            if (!b.bytes.empty()) {
                const ByteCell& c = b.bytes[0];
                if (!cellFits(bindings, b, c.record, st)) continue;
                std::size_t at = position(st.starts, c, blob.size());
                if (at >= blob.size() || c.slope == 0) continue;
                int shown = b.base + roundInt((static_cast<double>(blob[at]) - c.base) / c.slope);
                // a setting of a group (all the pads, all the keys) that moves several cells by the same amount is only read when every
                // cell agrees: a module whose members were set one by one has no single value, and the setting is left out
                bool shift = b.bytes.size() > 1;
                for (const ByteCell& cell : b.bytes) shift = shift && cell.slope == 1.0;
                bool agree = true;
                for (std::size_t i = 1; shift && agree && i < b.bytes.size(); ++i) {
                    if (!cellFits(bindings, b, b.bytes[i].record, st)) continue;
                    std::size_t other = position(st.starts, b.bytes[i], blob.size());
                    if (other >= blob.size()) continue;
                    agree = b.base + roundInt(static_cast<double>(blob[other]) - b.bytes[i].base) == shown;
                }
                if (!agree) continue;
                out[key] = std::to_string(shown);
            } else if (!b.script.empty() && b.script[0].slope != 0) {
                int at = findCommand(commands, b.script[0].key);
                if (at < 0) continue;
                int value = std::atoi(commands[static_cast<std::size_t>(at)].c_str() + b.script[0].key.size() + 1);
                out[key] = std::to_string(b.base + roundInt((value - b.script[0].base) / b.script[0].slope));
            }
            continue;
        }
        // a choice: the option with the most conditions that all hold
        int best = -1, bestScore = -1;
        for (std::size_t i = 0; i < b.options.size(); ++i) {
            const Binding::Option& o = b.options[i];
            bool ok = true;
            for (const ByteCell& c : o.cells) {
                if (!cellFits(bindings, b, c.record, st)) {
                    ok = false;
                    continue;
                }
                std::size_t at = position(st.starts, c, blob.size());
                if (at >= blob.size() || (blob[at] & c.mask) != (c.base & c.mask)) ok = false;
            }
            for (const std::string& a : o.added) ok = ok && std::find(commands.begin(), commands.end(), a) != commands.end();
            for (const std::string& r : o.removed) ok = ok && std::find(commands.begin(), commands.end(), r) == commands.end();
            int score = static_cast<int>(o.cells.size() + o.added.size() + o.removed.size());
            if (ok && score > bestScore) {
                best = static_cast<int>(i);
                bestScore = score;
            }
        }
        if (best >= 0) out[key] = b.options[static_cast<std::size_t>(best)].value;
    }
    {
        std::set<std::pair<std::string, std::string>> bound = boundSettings(bindings);
        for (const Group& group : groupsOf(bindings)) {
            std::set<std::string> names = groupSettings(bindings, group, bound);
            std::vector<std::pair<std::pair<std::string, std::string>, std::string>> copies;
            for (const auto& entry : out) {
                if (entry.first.first != group.members.front() || !names.count(stripPart(entry.first.second))) continue;
                // the group has a value only when every member has it: members set one by one have no common value to show
                bool same = true;
                for (const std::string& member : group.members) {
                    auto other = out.find({member, entry.first.second});
                    same = same && other != out.end() && other->second == entry.second;
                }
                if (same) copies.push_back({{group.name, entry.first.second}, entry.second});
            }
            for (const auto& copy : copies) out[copy.first] = copy.second;
        }
    }
    if (!bindings.scale.steps.empty()) {
        std::vector<int> notes;
        if (keyNotes(bindings.scale, out, notes)) {
            int index = scaleOfNotes(bindings.scale, notes);
            out[{bindings.scale.scope, bindings.scale.noteName}] = std::to_string(notes[0]);
            out[{bindings.scale.scope, bindings.scale.scaleName}] = index >= 0 ? std::to_string(index) : "";
            out[{"M", bindings.scale.expertName}] = index >= 0 ? "0" : "1";
        }
    }
    return out;
}

Bytes encodeSettings(const ModuleBindings& bindings, const Bytes& original, const SettingValues& requested) {
    SettingValues changes = requested;
    if (!bindings.scale.steps.empty()) {
        // a new first note or a new scale is written as the notes of the keys, unless a key is given a note of its own
        auto scaleChange = requested.find({bindings.scale.scope, bindings.scale.scaleName});
        auto noteChange = requested.find({bindings.scale.scope, bindings.scale.noteName});
        if (scaleChange != requested.end() || noteChange != requested.end()) {
            SettingValues now = decodeSettings(bindings, original);
            std::vector<int> notes;
            if (keyNotes(bindings.scale, now, notes)) {
                int first = noteChange != requested.end() ? std::atoi(noteChange->second.c_str()) : notes[0];
                std::string scaleText = scaleChange != requested.end() ? scaleChange->second : now[{bindings.scale.scope, bindings.scale.scaleName}];
                int index = scaleText.empty() ? -1 : std::atoi(scaleText.c_str());
                if (index >= 0 && static_cast<std::size_t>(index) < bindings.scale.steps.size() && !bindings.scale.steps[static_cast<std::size_t>(index)].empty()) {
                    std::vector<int> target = scaleNotes(bindings.scale, static_cast<std::size_t>(index), first);
                    for (int k = 1; k <= bindings.scale.keys; ++k) {
                        std::pair<std::string, std::string> key{"KEY" + std::to_string(k), "Note"};
                        if (!requested.count(key)) changes[key] = std::to_string(target[static_cast<std::size_t>(k - 1)]);
                    }
                }
            }
        }
    }
    {
        std::set<std::pair<std::string, std::string>> bound = boundSettings(bindings);
        for (const Group& group : groupsOf(bindings)) {
            std::set<std::string> names = groupSettings(bindings, group, bound);
            for (const auto& change : requested) {
                if (change.first.first != group.name || !names.count(change.first.second)) continue;
                for (const std::string& member : group.members) {
                    std::pair<std::string, std::string> key{member, change.first.second};
                    if (!requested.count(key)) changes[key] = change.second;
                }
            }
        }
    }
    Bytes blob = original;
    const std::set<std::string> groupedScopes = groupScopes(bindings);
    State st = stateOf(bindings, blob);

    // First what changes the records themselves: the Action of an element, and the numbers in the script text of a button.
    std::map<std::size_t, Bytes> replaced;
    std::map<std::string, std::string> action = st.action;
    for (const FunctionTable& t : bindings.functions) {
        auto it = changes.find({t.scope, "Function"});
        if (it == changes.end()) continue;
        const FunctionVariant* v = variantOf(t, it->second);
        auto now = st.action.find(t.scope);
        if (!v || static_cast<std::size_t>(t.record) >= st.parsed.elements.size() || (now != st.action.end() && now->second == it->second)) continue;
        Bytes record = v->record;
        const Bytes& old = st.parsed.elements[static_cast<std::size_t>(t.record)].bytes;
        // the place, the size, the id, the layer and the depth of the element stay; the type, options and body are those of the Action
        for (std::size_t i = 2; i <= 13 && i < record.size() && i < old.size(); ++i) record[i] = old[i];
        replaced[static_cast<std::size_t>(t.record)] = record;
        action[t.scope] = v->value;
    }
    for (const Binding& b : bindings.bindings) {
        if (b.text.empty()) continue;
        auto act = action.find(b.scope);
        const FunctionTable* t = tableOf(bindings, b.scope);
        auto change = changes.find({b.scope, b.name});
        if (!t || change == changes.end() || act == action.end() || act->second != b.when) continue;
        std::size_t index = static_cast<std::size_t>(t->record);
        if (index >= st.parsed.elements.size()) continue;
        const Bytes& base = replaced.count(index) ? replaced[index] : st.parsed.elements[index].bytes;
        long value = std::lround(b.text[0].base + b.text[0].slope * (std::atoi(change->second.c_str()) - b.base));
        replaced[index] = withTextValue(base, b.text[0].key, value);
    }
    if (!replaced.empty()) {
        Design d = Design::fromBytes(blob);
        for (const auto& r : replaced) d.blob().elements[r.first].bytes = r.second;
        blob = d.toBytes();
        st = stateOf(bindings, blob);
        for (const auto& a : action) st.action[a.first] = a.second;
    }

    std::vector<std::string> commands = scriptCommands(blob);
    std::vector<std::string> before = commands;
    auto setNumeric = [&](const Binding& b, int value) {
        double delta = value - b.base;
        for (const ByteCell& c : b.bytes) {
            if (!cellFits(bindings, b, c.record, st)) continue;
            std::size_t at = position(st.starts, c, blob.size());
            if (at >= blob.size()) continue;
            blob[at] = static_cast<std::uint8_t>(std::max(0, std::min(255, roundInt(c.base + c.slope * delta))));
        }
        for (const ScriptCell& sc : b.script) putCommand(commands, sc.key + "=" + std::to_string(sc.base + roundInt(sc.slope * delta)));
    };
    auto setChoice = [&](const Binding& b, const std::string& value) {
        auto keyOf = [](const std::string& c) { return c.substr(0, c.find('=')); };
        auto has = [](const std::vector<std::string>& list, const std::string& c) { return std::find(list.begin(), list.end(), c) != list.end(); };
        for (const Binding::Option& o : b.options) {
            if (o.value != value) continue;
            // The choices are states of the same bits and commands. First leave the state of the other options: a command that only
            // another option adds goes away (or becomes again the one the baseline has for the same key, in the same place), and a
            // command that another option removes comes back if it is missing. Then apply the chosen option. A choice is turned off
            // by this, and a choice that is already on leaves every byte as it was.
            std::vector<std::string> allAdded, allRemoved;
            for (const Binding::Option& other : b.options) {
                allAdded.insert(allAdded.end(), other.added.begin(), other.added.end());
                allRemoved.insert(allRemoved.end(), other.removed.begin(), other.removed.end());
            }
            for (std::size_t i = 0; i < commands.size();) {
                if (!has(allAdded, commands[i]) || has(o.added, commands[i])) {
                    ++i;
                    continue;
                }
                std::string replacement;
                for (const std::string& r : allRemoved) {
                    if (keyOf(r) == keyOf(commands[i]) && !has(o.removed, r)) replacement = r;
                }
                if (!replacement.empty()) commands[i++] = replacement;
                else commands.erase(commands.begin() + static_cast<std::ptrdiff_t>(i));
            }
            for (const std::string& r : allRemoved) {
                if (!has(o.removed, r) && findCommand(commands, keyOf(r)) < 0) commands.push_back(r);
            }
            for (const ByteCell& c : o.cells) {
                if (!cellFits(bindings, b, c.record, st)) continue;
                std::size_t at = position(st.starts, c, blob.size());
                if (at >= blob.size()) continue;
                blob[at] = static_cast<std::uint8_t>((blob[at] & ~c.mask) | (c.base & c.mask));
            }
            for (const std::string& r : o.removed) {
                bool replaced = false;
                for (const std::string& a : o.added) replaced = replaced || keyOf(a) == keyOf(r);
                if (!replaced) commands.erase(std::remove(commands.begin(), commands.end(), r), commands.end());
            }
            for (const std::string& a : o.added) putCommand(commands, a);
            return;
        }
    };
    for (const auto& change : changes) {
        const std::string& scope = change.first.first;
        const std::string& name = change.first.second;
        const std::string& value = change.second;
        for (const Binding& b : bindings.bindings) {
            if (b.scope != scope || stripPart(b.name) != name || !b.text.empty() || !b.pair.empty() || !bindingActive(b, st)) continue;
            if (groupedScopes.count(b.scope) && stripPart(b.name) != "Note") continue;
            if (!b.numeric) {
                setChoice(b, value);
            } else if (b.name.size() > name.size()) {
                // a slider "lo;hi": the part this binding follows
                std::size_t semicolon = value.find(';');
                if (semicolon == std::string::npos) continue;
                std::string part = b.name.substr(name.size() + 1);
                setNumeric(b, roundInt(std::atof((part == "lo" ? value.substr(0, semicolon) : value.substr(semicolon + 1)).c_str())));
            } else {
                setNumeric(b, std::atoi(value.c_str()));
            }
        }
    }
    // a range and its direction are written together, from what the module has now and what was asked
    for (const Binding& b : bindings.bindings) {
        if (b.pair.empty() || !bindingActive(b, st)) continue;
        auto range = changes.find({b.scope, b.name});
        auto direction = changes.find({b.scope, b.dirName});
        if (range == changes.end() && direction == changes.end()) continue;
        PairValue v = readPair(bindings, b, blob, st);
        if (!v.known) continue;
        if (range != changes.end()) {
            std::size_t semicolon = range->second.find(';');
            if (semicolon == std::string::npos) {
                v.hi = roundInt(std::atof(range->second.c_str()));
            } else {
                if (b.pair != "sign") v.lo = roundInt(std::atof(range->second.substr(0, semicolon).c_str()));
                v.hi = roundInt(std::atof(range->second.substr(semicolon + 1).c_str()));
            }
        }
        if (direction != changes.end()) {
            for (std::size_t i = 0; i < b.dirValues.size(); ++i) {
                if (b.dirValues[i] == direction->second) v.direction = static_cast<int>(i);
            }
        }
        writePair(bindings, b, blob, st, v);
    }
    // Pressure range: the depth of every element that follows a rule
    {
        auto change = changes.find({"M", "Dynamic"});
        if (change != changes.end() && !bindings.depthRules.empty()) {
            int which = change->second == bindings.depthValues[1] ? 1 : change->second == bindings.depthValues[0] ? 0 : -1;
            for (std::size_t i = 0; which >= 0 && i < st.parsed.elements.size() && i < st.starts.size(); ++i) {
                const DepthRule* r = depthRuleFor(bindings, i);
                if (!r || st.starts[i] + 13 >= blob.size()) continue;
                blob[st.starts[i] + 13] = static_cast<std::uint8_t>(which == 0 ? r->low : r->high);
            }
        }
    }
    // Aftertouch and its type: one command of the script, for polyphonic (22) or for channel pressure (23)
    if (!bindings.aftertouchPrefix.empty()) {
        auto on = changes.find({"M", "Aftertouch"});
        auto type = changes.find({"M", "AftertouchType"});
        if (on != changes.end() || type != changes.end()) {
            std::string p22 = bindings.aftertouchPrefix + ".22", p23 = bindings.aftertouchPrefix + ".23";
            bool enabled = findCommand(commands, p22) >= 0 || findCommand(commands, p23) >= 0;
            bool channel = findCommand(commands, p23) >= 0;
            if (on != changes.end()) enabled = on->second == "1";
            if (type != changes.end()) channel = type->second == "Channel";
            // the command keeps its place in the script
            auto isAftertouch = [&](const std::string& c) { return c.compare(0, p22.size() + 1, p22 + "=") == 0 || c.compare(0, p23.size() + 1, p23 + "=") == 0; };
            std::size_t place = commands.size();
            for (std::size_t i = 0; i < commands.size(); ++i) {
                if (isAftertouch(commands[i])) {
                    place = i;
                    break;
                }
            }
            commands.erase(std::remove_if(commands.begin(), commands.end(), isAftertouch), commands.end());
            if (enabled) commands.insert(commands.begin() + static_cast<std::ptrdiff_t>(std::min(place, commands.size())), (channel ? p23 : p22) + "=1");
        }
    }
    if (commands != before) {
        Design d = Design::fromBytes(blob);
        d.setScript(joinScript(commands));
        return d.toBytes();
    }
    fixChecksum(blob);
    return blob;
}

SettingValues completeActionChanges(const ModuleBindings& bindings, const SettingValues& all, SettingValues changes) {
    SettingValues extra;
    for (const auto& c : changes) {
        if (c.first.second != "Function") continue;
        for (const Binding& b : bindings.bindings) {
            if (b.scope != c.first.first || b.when != c.second) continue;
            auto key = std::make_pair(b.scope, stripPart(b.name));
            auto it = all.find(key);
            if (it != all.end()) extra[key] = it->second;
            if (!b.pair.empty()) {
                auto d = all.find({b.scope, b.dirName});
                if (d != all.end()) extra[{b.scope, b.dirName}] = d->second;
            }
        }
    }
    for (const auto& e : extra) changes[e.first] = e.second;
    return changes;
}

std::set<std::pair<std::string, std::string>> boundSettings(const ModuleBindings& bindings) {
    std::set<std::pair<std::string, std::string>> out;
    const std::set<std::string> grouped = groupScopes(bindings);
    for (const Binding& b : bindings.bindings) {
        if (grouped.count(b.scope) && stripPart(b.name) != "Note") continue;
        out.insert({b.scope, stripPart(b.name)});
        if (!b.pair.empty()) out.insert({b.scope, b.dirName});
    }
    for (const FunctionTable& t : bindings.functions) out.insert({t.scope, "Function"});
    if (!bindings.depthRules.empty()) out.insert({"M", "Dynamic"});
    {
        std::set<std::pair<std::string, std::string>> members = out;
        for (const Group& group : groupsOf(bindings)) {
            for (const std::string& name : groupSettings(bindings, group, members)) out.insert({group.name, name});
        }
    }
    if (!bindings.scale.steps.empty()) {
        out.insert({bindings.scale.scope, bindings.scale.noteName});
        out.insert({bindings.scale.scope, bindings.scale.scaleName});
        out.insert({"M", bindings.scale.expertName});
    }
    if (!bindings.aftertouchPrefix.empty()) {
        out.insert({"M", "Aftertouch"});
        out.insert({"M", "AftertouchType"});
    }
    return out;
}

namespace {

template <typename F>
void eachSetting(Node& module, F visit) {
    std::function<void(Node&, const std::string&)> walk = [&](Node& node, const std::string& scope) {
        for (auto& child : node.children) {
            if (child->tag == "Setting") visit(scope, *child);
            walk(*child, scope);
        }
    };
    for (auto& child : module.children) {
        if (child->tag == "Array" && child->name() == "SettingsList") walk(*child, "M");
        if (child->tag == "Array" && child->name() == "ElementList") {
            for (auto& element : child->children) {
                if (element->tag == "Element") walk(*element, element->name());
            }
        }
    }
}

}  // namespace

SettingValues collectValues(const Node& module) {
    SettingValues out;
    eachSetting(const_cast<Node&>(module), [&](const std::string& scope, Node& setting) {
        const std::string* v = setting.attr("value");
        if (v) out[{scope, setting.name()}] = *v;
    });
    return out;
}

std::string elementForShape(const Node& module, const std::string& shape) {
    const Node* list = nullptr;
    for (const auto& child : module.children) {
        if (child->tag == "Array" && child->name() == "ElementList") list = child.get();
    }
    if (!list) return shape;
    auto has = [&](const std::string& name) {
        for (const auto& child : list->children) {
            if (child->tag == "Element" && child->name() == name) return true;
        }
        return false;
    };
    std::string base = shape;
    while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
    if (base.empty() || base == shape) return shape;
    std::string group = "ALL_" + base + "S";
    if (!has(group)) return shape;
    if (!has(shape)) return group;
    SettingValues values = collectValues(module);
    auto expert = values.find({"M", "Expert"});
    return expert != values.end() && expert->second == "0" ? group : shape;
}

bool containsLockedModule(const Node& node) {
    if (node.tag == "Module") {
        for (const auto& child : node.children) {
            if (child->tag == "String" && child->name() == "ReadOnly" && child->attr("value") && *child->attr("value") == "1") return true;
        }
        return false;
    }
    for (const auto& child : node.children) {
        if (containsLockedModule(*child)) return true;
    }
    return false;
}

std::string elementNameOfRecord(int moduleType, std::size_t record) {
    const ModuleBindings* bindings = moduleBindings(moduleType);
    if (!bindings || record >= bindings->names.size()) return {};
    return bindings->names[record];
}

std::string touchedElementName(const Node& module, int touchedType, std::size_t record) {
    if (module.tag != "Module") return {};
    int type = -1;
    const Node* list = nullptr;
    for (const auto& child : module.children) {
        if (child->tag == "String" && child->name() == "Type" && child->attr("value")) type = std::atoi(child->attr("value")->c_str());
        if (child->tag == "Array" && child->name() == "ElementList") list = child.get();
    }
    if (type != touchedType || !list) return {};
    std::string name = elementNameOfRecord(touchedType, record);
    if (name.empty()) return {};
    for (const auto& child : list->children) {
        if (child->tag == "Element" && child->name() == name) return name;
    }
    return {};
}

int lockFactoryPresets(Node& root) {
    int locked = 0;
    if (root.tag == "Module") {
        std::string type;
        bool unlocked = false;
        for (const auto& child : root.children) {
            if (child->tag == "String" && child->name() == "Type" && child->attr("value")) type = *child->attr("value");
            if (child->tag == "String" && child->name() == "ReadOnly" && child->attr("value") && *child->attr("value") == "0") unlocked = true;
        }
        if (root.name() == "Play Audio" && type == "27" && unlocked) {
            std::function<void(Node&)> lockAll = [&](Node& n) {
                if ((n.tag == "String" || n.tag == "Setting") && n.name() == "ReadOnly") n.attrRef("value") = "1";
                for (auto& c : n.children) lockAll(*c);
            };
            lockAll(root);
            ++locked;
        }
        return locked;
    }
    for (auto& child : root.children) locked += lockFactoryPresets(*child);
    return locked;
}

void applyValues(Node& module, const SettingValues& values) {
    eachSetting(module, [&](const std::string& scope, Node& setting) {
        auto direct = values.find({scope, setting.name()});
        if (direct != values.end()) {
            setting.attrRef("value") = direct->second;
            return;
        }
        auto lo = values.find({scope, setting.name() + "#lo"});
        auto hi = values.find({scope, setting.name() + "#hi"});
        if (lo == values.end() && hi == values.end()) return;
        std::string& value = setting.attrRef("value");
        std::size_t semicolon = value.find(';');
        std::string oldLo = semicolon == std::string::npos ? "0" : value.substr(0, semicolon);
        std::string oldHi = semicolon == std::string::npos ? "0" : value.substr(semicolon + 1);
        bool decimals = value.find('.') != std::string::npos;
        int a = lo != values.end() ? std::atoi(lo->second.c_str()) : static_cast<int>(std::lround(std::atof(oldLo.c_str())));
        int b = hi != values.end() ? std::atoi(hi->second.c_str()) : static_cast<int>(std::lround(std::atof(oldHi.c_str())));
        char buffer[64];
        if (decimals) std::snprintf(buffer, sizeof buffer, "%.6f;%.6f", static_cast<double>(a), static_cast<double>(b));
        else std::snprintf(buffer, sizeof buffer, "%d;%d", a, b);
        value = buffer;
    });
}

std::string presetMismatch(const Node& libraryModule, const Bytes& blob) {
    if (blob.size() < InstrumentHeader::Length) return "The module holds no layout.";
    auto number = [&](const char* name, int& out) {
        for (const auto& child : libraryModule.children) {
            if (child->tag == "String" && child->name() == name && child->attr("value")) {
                out = std::atoi(child->attr("value")->c_str());
                return true;
            }
        }
        return false;
    };
    int type = 0, version = 0, revision = 0;
    if (!number("Type", type) || !number("Version", version) || !number("Revision", revision)) return "This preset cannot be sent: it has no module type. Nothing is written.";
    int blobType = blob[0], blobVersion = blob[1], blobRevision = blob[5];
    if (type != blobType) {
        return "This preset cannot be sent to this module: it is for a " + moduleName(type) + ", and the module is a " + moduleName(blobType) + ". Nothing is written.";
    }
    if (version != blobVersion) return "This preset cannot be sent to this module: its layout version is " + std::to_string(version) + ", the module's is " + std::to_string(blobVersion) + ". Nothing is written.";
    if (revision != blobRevision) return "This preset cannot be sent to this module: its revision is " + std::to_string(revision) + ", the module's is " + std::to_string(blobRevision) + ". Nothing is written.";
    const ModuleBindings* bindings = moduleBindings(type);
    if (!bindings) return "This preset cannot be sent: the settings of " + moduleName(type) + " modules cannot be written yet. Nothing is written.";
    if (!structureMatches(*bindings, blob)) return "This preset cannot be sent to this module: the module is not the factory " + moduleName(type) + " layout (it may be a custom one). Nothing is written.";
    return {};
}

SettingValues presetChanges(const ModuleBindings& bindings, const Bytes& base, const Node& libraryModule) {
    SettingValues changes;
    if (!presetMismatch(libraryModule, base).empty()) return changes;
    SettingValues preset = collectValues(libraryModule);
    SettingValues current = decodeSettings(bindings, base);
    std::set<std::pair<std::string, std::string>> bound = boundSettings(bindings);
    // the Action each element will have: the preset's, or what the module has when the preset says nothing
    std::map<std::string, std::string> action;
    for (const FunctionTable& t : bindings.functions) {
        auto want = preset.find({t.scope, "Function"});
        auto have = current.find({t.scope, "Function"});
        if (want != preset.end() && variantOf(t, want->second)) action[t.scope] = want->second;
        else if (have != current.end()) action[t.scope] = have->second;
    }
    for (const auto& kv : preset) {
        if (!bound.count(kv.first)) continue;
        const std::string& value = kv.second;
        if (kv.first.second == "Function") {
            auto have = current.find(kv.first);
            const FunctionTable* t = tableOf(bindings, kv.first.first);
            if (t && variantOf(*t, value) && (have == current.end() || have->second != value)) changes[kv.first] = value;
            continue;
        }
        // a setting that belongs to an Action is only written when the element has that Action
        bool applies = false;
        for (const Binding& b : bindings.bindings) {
            if (b.scope != kv.first.first || (stripPart(b.name) != kv.first.second && b.dirName != kv.first.second)) continue;
            auto a = action.find(b.scope);
            if (b.when.empty() || (a != action.end() && a->second == b.when)) applies = true;
        }
        {
            std::set<std::pair<std::string, std::string>> bound = boundSettings(bindings);
            for (const Group& group : groupsOf(bindings)) {
                if (kv.first.first == group.name && groupSettings(bindings, group, bound).count(stripPart(kv.first.second))) applies = true;
            }
        }
        if (!bindings.scale.steps.empty() && ((kv.first.first == bindings.scale.scope && (kv.first.second == bindings.scale.noteName || kv.first.second == bindings.scale.scaleName)) || (kv.first.first == "M" && kv.first.second == bindings.scale.expertName))) applies = true;
        if (kv.first.first == "M" && ((kv.first.second == "Dynamic" && !bindings.depthRules.empty()) || ((kv.first.second == "Aftertouch" || kv.first.second == "AftertouchType") && !bindings.aftertouchPrefix.empty()))) applies = true;
        if (!applies) continue;
        std::size_t semicolon = value.find(';');
        if (semicolon != std::string::npos) {
            // a slider: it differs when either end differs
            auto lo = current.find({kv.first.first, kv.first.second + "#lo"});
            auto hi = current.find({kv.first.first, kv.first.second + "#hi"});
            int a = roundInt(std::atof(value.substr(0, semicolon).c_str())), b = roundInt(std::atof(value.substr(semicolon + 1).c_str()));
            bool same = lo != current.end() && hi != current.end() && std::atoi(lo->second.c_str()) == a && std::atoi(hi->second.c_str()) == b;
            if (!same) changes[kv.first] = value;
            continue;
        }
        auto it = current.find(kv.first);
        if (it == current.end() || it->second != value) changes[kv.first] = value;
    }
    return changes;
}

Bytes presetToBlob(const ModuleBindings& bindings, const Bytes& base, const Node& libraryModule) {
    // a preset of another type, version or revision changes nothing, whatever the caller checked before
    if (!presetMismatch(libraryModule, base).empty()) return base;
    return encodeSettings(bindings, base, presetChanges(bindings, base, libraryModule));
}

std::vector<std::string> describeChanges(const ModuleBindings& bindings, const Bytes& base, const SettingValues& changes) {
    SettingValues current = decodeSettings(bindings, base);
    std::vector<std::string> lines;
    for (const auto& kv : changes) {
        std::string before = "?";
        auto it = current.find(kv.first);
        if (it != current.end()) {
            before = it->second;
        } else {
            auto lo = current.find({kv.first.first, kv.first.second + "#lo"});
            auto hi = current.find({kv.first.first, kv.first.second + "#hi"});
            if (lo != current.end() || hi != current.end()) before = (lo != current.end() ? lo->second : std::string("?")) + ";" + (hi != current.end() ? hi->second : std::string("?"));
        }
        std::string after = kv.second;
        std::size_t semicolon = after.find(';');
        if (semicolon != std::string::npos) after = std::to_string(roundInt(std::atof(after.substr(0, semicolon).c_str()))) + ";" + std::to_string(roundInt(std::atof(after.substr(semicolon + 1).c_str())));
        lines.push_back(kv.first.first + ": " + i18n::text(kv.first.second) + "  " + i18n::text(before) + " -> " + i18n::text(after));
    }
    return lines;
}

std::unique_ptr<Node> moduleFromBlob(const Node& libraryModule, const ModuleBindings& bindings, const Bytes& blob) {
    std::unique_ptr<Node> module = libraryModule.clone();
    applyValues(*module, decodeSettings(bindings, blob));
    ModuleBlob parsed = ModuleBlob::parse(blob);
    auto setString = [&](const char* name, int value) {
        for (auto& child : module->children) {
            if (child->tag == "String" && child->name() == name) child->attrRef("value") = std::to_string(value);
        }
    };
    setString("Color", parsed.header.color);
    setString("Revision", parsed.header.revision);
    setString("ReadOnly", 0);
    // the settings panel shows the colour and the lock as settings too
    SettingValues own;
    own[{"M", "Color"}] = std::to_string(parsed.header.color);
    own[{"M", "ReadOnly"}] = "0";
    applyValues(*module, own);
    return module;
}

}  // namespace joue
