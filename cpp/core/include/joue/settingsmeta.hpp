#pragma once
#include <string>
#include <vector>

namespace joue {

namespace settingtype {
constexpr int Checkbox = 1, Combobox = 2, TextField = 3, Slider = 4, DoubleSlider = 5, Button = 6, ToggleButton = 7, Category = 8;
}

// Description of one setting as the original application defines it.
struct SettingMeta {
    std::string name, title;
    int type = 0;
    std::string parentValue;  // "ALL" or the value of the parent setting for which this setting is shown
    bool hasLimitMin = false, hasLimitMax = false;
    int limitMin = 0, limitMax = 0;
    std::vector<std::string> fields;  // what the user sees in a combobox
    std::vector<std::string> values;  // what is stored for each field
    bool baseHidden = false, baseDisabled = false, baseReadOnly = false;  // state for the default values
    std::vector<SettingMeta> children;
};

// A dynamic rule: while setting `sourceName` (of the module, or of the elements named in sourceScope) has `value`, the
// target setting gets these flags. targetScope is "M" (module), "E*" (every element), "." (the source element itself)
// or a list of element names in targetElements.
struct SettingRule {
    std::vector<std::string> sourceScope;
    std::string sourceName, value, targetScope, targetName;
    std::vector<std::string> targetElements;
    bool hidden = false, disabled = false, readOnly = false;
};

const std::vector<SettingRule>* moduleRules(int moduleType);

// Settings of a module (by module type id), or nullptr when the type is unknown.
const std::vector<SettingMeta>* moduleSettingsMeta(int moduleType);

// Settings of one element of a module (for example "PAD5" of module type 25), or nullptr.
const std::vector<SettingMeta>* elementSettingsMeta(int moduleType, const std::string& elementName);

const SettingMeta* findSettingMeta(const std::vector<SettingMeta>& list, const std::string& name);

// A child setting is shown when its parent value is "ALL" or equals the current value of the parent.
bool shownFor(const SettingMeta& child, const std::string& parentValue);

// Display text of a stored combobox value (the matching field), or the value itself.
std::string fieldFor(const SettingMeta& meta, const std::string& value);

}  // namespace joue
