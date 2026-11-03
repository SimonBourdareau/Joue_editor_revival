#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "joue/blob.hpp"
#include "joue/library.hpp"

namespace joue {

// How the settings of a module type map onto the bytes of its blob. The table was measured by running the macOS
// application's own encoder with one setting changed at a time (tools/module_art/gen_bindings.py): a numeric setting
// follows bytes (or a number in the start-up script) with a slope, a choice has, for each value, the bits it sets and the
// script commands it adds or removes. A setting that decides which records exist (the Function of a pad) is not in it.
struct ByteCell {
    int record = -1;    // index of the record before the script, or -1 for the 12-byte header
    int offset = 0;     // position inside that record (inside the header for -1)
    int base = 0;       // numeric: the byte at the baseline; choice: the byte of the option
    double slope = 1;   // numeric only
    int mask = 255;     // choice only: the bits that belong to this setting
};

struct ScriptCell {
    std::string key;    // for example "l{2,3}.3"
    int base = 0;
    double slope = 1;
};

// What the Action (the Function setting) of an element decides. The record an element has depends on its Action: a pad for a note, a
// button with a small script for sustain, vibrato, aftertouch, Y control, glissando and channel selection, a control record for a
// control change, a program change or a pitch bend. The table holds, for every Action, the record the application's own encoder writes
// and how to tell that record again; the settings that belong to an Action are bindings with `when` set to it.
struct FunctionVariant {
    std::string value;
    int type = 0, version = 0;
    Bytes record;
    std::string text;                        // a button: its script text, with # in place of a number; empty for other records
    std::vector<std::pair<int, int>> disc;   // other records: bytes (position in the record, value) that tell this Action
};

struct FunctionTable {
    std::string scope;     // the element
    int record = 0;        // its place among the records before the script
    std::string baseline;  // the Action it has in the factory preset
    std::vector<FunctionVariant> variants;
};

struct Binding {
    std::string scope, name;
    std::string pair;                  // "swap" or "sign": a range and its direction stored together (see decodeSettings); the two cells are bytes[0] and bytes[1]
    std::string dirName;               // the setting of the direction of a pair, and its two values in order
    std::vector<std::string> dirValues;
    std::string when;                  // the Action of the element that the setting belongs to; empty when it does not depend on one
    std::vector<ScriptCell> text;      // a number in the script text of the element's button record (key, number at the base, slope)  // scope is "M" for the module or the name of an element; a slider has "#lo" and "#hi" parts
    bool numeric = true;
    int base = 0;
    std::vector<ByteCell> bytes;
    std::vector<ScriptCell> script;
    struct Option {
        std::string value;
        std::vector<ByteCell> cells;
        std::vector<std::string> added, removed;  // script commands
    };
    std::vector<Option> options;
};

// Pressure range: Low and High are two depth values (byte 13 of the record) for each kind of element, found by the first letters of its name.
struct DepthRule {
    std::string prefix;
    int low = 0, high = 0;
};

// The Scaler (and Play Keys): the notes of the 17 keys follow from the first note and a scale (a cycle of steps); notes that follow
// none of the scales are a custom mapping. The settings of the group (scope, Note, Scale) and the Custom mapping checkbox are derived
// from, and written as, the notes of the keys.
struct ScaleSupport {
    std::string scope, noteName, scaleName, expertName;
    std::vector<std::vector<int>> steps;     // by number; empty for a number that is not a scale (a heading, or the unused 23)
    std::vector<std::string> titles;         // the same numbers; a title starting with "--" is a heading
    int keys = 17;
};

struct ModuleBindings {
    ScaleSupport scale;                     // empty (no steps) when the module has no scales
    bool expertAlways = false;              // Custom mapping is on in a module read from the device (its notes are always editable)
    std::vector<DepthRule> depthRules;      // empty when the module has no Pressure range
    std::vector<std::string> depthValues;   // "Low", "High"
    std::string aftertouchPrefix;           // how the start-up script addresses the layers ("l3", "*"...); empty when the module has no aftertouch setting
    int type = 0;
    std::string name;
    std::string structure;  // "type:version:length" of every record before the script; a button has "*" for its length
    std::vector<std::string> names;
    std::vector<std::pair<int, int>> baselineTypes;  // type and version of every record before the script, in the factory layout
    std::vector<Binding> bindings;
    std::vector<FunctionTable> functions;
};

const ModuleBindings* moduleBindings(int type);

using SettingValues = std::map<std::pair<std::string, std::string>, std::string>;  // (scope, setting name) -> value

// True when the records of the blob are those of the factory structure of the type, so that the table applies to it.
bool structureMatches(const ModuleBindings& bindings, const Bytes& blob);

// The values of every setting the table covers, read from a blob. Sliders come as "lo;hi".
SettingValues decodeSettings(const ModuleBindings& bindings, const Bytes& blob);

// The blob with the changed settings written into it. Only the bytes and script commands of those settings change; the type,
// version, colour and security bytes, all geometry and everything the table does not cover stay as they were.
Bytes encodeSettings(const ModuleBindings& bindings, const Bytes& original, const SettingValues& changes);

// The values to write when the Action of an element changes: the settings that belong to the new Action, taken from `all` (the values of
// the module as it is shown), added to `changes`, so that what is written is what the person sees and not the defaults of the record.
SettingValues completeActionChanges(const ModuleBindings& bindings, const SettingValues& all, SettingValues changes);

// Names of the settings (scope, name) that the table can read and write.
std::set<std::pair<std::string, std::string>> boundSettings(const ModuleBindings& bindings);

// The blob of the module that is on the instrument (base) with the settings of a library module of the same type written into it:
// every setting the table covers whose value in the library module differs from what the base holds is written; the rest of the
// base, geometry included, stays as it is.
Bytes presetToBlob(const ModuleBindings& bindings, const Bytes& base, const Node& libraryModule);

// Why a library module must not be written into a module blob, or an empty text when it may. A preset is only written into a module
// of exactly the same type, layout version and revision, whose records are those of the factory layout of that type; anything else
// would put the settings of one kind of module into another.
std::string presetMismatch(const Node& libraryModule, const Bytes& blob);

// The settings (scope, name) -> value that writing a preset would change in a module blob, and the same as lines of text.
SettingValues presetChanges(const ModuleBindings& bindings, const Bytes& base, const Node& libraryModule);
std::vector<std::string> describeChanges(const ModuleBindings& bindings, const Bytes& base, const SettingValues& changes);

// Library module nodes for a module of the instrument. The template is a library module of the same type (a factory preset):
// it supplies every setting the table does not cover, the values read from the blob replace the others.
std::unique_ptr<Node> moduleFromBlob(const Node& libraryModule, const ModuleBindings& bindings, const Bytes& blob);
void applyValues(Node& module, const SettingValues& values);
SettingValues collectValues(const Node& module);

// The element that a click on a key or a pad edits. With Custom mapping off the group (ALL_KEYS, ALL_PADS) stands for all of them: it is the
// element that has the scale and the first note, and the keys or pads are not edited one by one; with it on each key or pad has its own
// settings and the group is not used. This is the rule of the original application. Without a group, or for anything that is not a key
// or a pad, the element is the one clicked.
std::string elementForShape(const Node& module, const std::string& shape);

// Locked factory presets: a module is locked when its ReadOnly is 1. A module, or a folder that holds one, cannot be deleted while it holds a
// locked module (Duplicate gives a copy that can).
bool containsLockedModule(const Node& node);

// The name that the factory layout of a module type gives to the element of a record of its blob (KEY3, BUTTON1...), or an empty string when
// the type is not known or the record is not an element of that layout (a layout drawn in the designer).
std::string elementNameOfRecord(int moduleType, std::size_t record);

// The element of a library module that a touch selects: the touched module must be of the type of this module and its layout must have
// the element of that record (an empty string when it is not, so that a touch on a Scaler does nothing to an open Pads preset).
std::string touchedElementName(const Node& module, int touchedType, std::size_t record);

// The factory library of the original application leaves one preset unlocked, Play Audio: it is locked here like the others when a library is
// opened. Returns how many modules were locked. (Unlock to edit still unlocks it for the session; a copy made with Duplicate is never locked.)
int lockFactoryPresets(Node& root);

}  // namespace joue
