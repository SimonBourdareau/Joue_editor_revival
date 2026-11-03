#include "joue/settingsmeta.hpp"
#include "joue/instrumentmodule.hpp"

#include <functional>
#include <map>
#include <sstream>

namespace joue {

namespace {

#include "settings_data.inc"

struct Group {
    std::vector<std::string> names;
    std::vector<SettingMeta> settings;
};

struct ModuleMeta {
    std::vector<SettingMeta> module;
    std::vector<Group> groups;
    std::vector<SettingRule> rules;
};

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

// Appends a setting at the given depth of an existing tree.
void append(std::vector<SettingMeta>& roots, int depth, SettingMeta&& setting) {
    std::vector<SettingMeta>* list = &roots;
    for (int i = 0; i < depth && !list->empty(); ++i) list = &list->back().children;
    list->push_back(std::move(setting));
}

const std::map<int, ModuleMeta>& all() {
    static const std::map<int, ModuleMeta> table = [] {
        std::map<int, ModuleMeta> result;
        ModuleMeta* module = nullptr;
        std::vector<SettingMeta>* target = nullptr;
        for (const char* text : kSettingsLines) {
            std::vector<std::string> f = split(text, '\t');
            if (f.empty()) continue;
            if (f[0] == "M" && f.size() >= 2) {
                module = &result[std::stoi(f[1])];
                target = &module->module;
            } else if (f[0] == "E" && module && f.size() >= 2) {
                module->groups.push_back({split(f[1], ','), {}});
                target = &module->groups.back().settings;
            } else if (f[0] == "R" && module && f.size() >= 7) {
                SettingRule r;
                r.sourceScope = split(f[1], ',');
                r.sourceName = f[2];
                r.value = f[3];
                r.targetScope = f[4];
                if (f[4] != "M" && f[4] != "E*" && f[4] != ".") r.targetElements = split(f[4], ',');
                r.targetName = f[5];
                r.hidden = f[6].size() > 0 && f[6][0] == '1';
                r.disabled = f[6].size() > 1 && f[6][1] == '1';
                r.readOnly = f[6].size() > 2 && f[6][2] == '1';
                module->rules.push_back(std::move(r));
            } else if (f[0] == "S" && target && f.size() >= 11) {
                SettingMeta s;
                s.name = f[2];
                s.title = f[3];
                s.type = std::stoi(f[4]);
                s.parentValue = f[6];
                if (!f[7].empty()) { s.hasLimitMin = true; s.limitMin = std::stoi(f[7]); }
                if (!f[8].empty()) { s.hasLimitMax = true; s.limitMax = std::stoi(f[8]); }
                if (!f[9].empty()) s.fields = split(f[9], '\037');
                if (!f[10].empty()) s.values = split(f[10], '\037');
                if (f.size() > 11) {
                    s.baseHidden = f[11].find('h') != std::string::npos;
                    s.baseDisabled = f[11].find('d') != std::string::npos;
                    s.baseReadOnly = f[11].find('r') != std::string::npos;
                }
                append(*target, std::stoi(f[1]), std::move(s));
            }
        }
        // The Scaler and Play Keys offer more scales than the original application: the choice has the titles of the table of the module, the
        // original ones first (their numbers are those of the original), then the headings and scales that follow. A number without a title
        // (the unused 23) is not offered.
        std::function<void(std::vector<SettingMeta>&, const ModuleBindings&)> extend = [&](std::vector<SettingMeta>& list, const ModuleBindings& b) {
            for (SettingMeta& m : list) {
                if (m.name == b.scale.scaleName && m.type == settingtype::Combobox) {
                    m.fields.clear();
                    m.values.clear();
                    for (std::size_t i = 0; i < b.scale.titles.size(); ++i) {
                        if (b.scale.titles[i].empty()) continue;
                        m.fields.push_back(b.scale.titles[i]);
                        m.values.push_back(std::to_string(i));
                    }
                }
                extend(m.children, b);
            }
        };
        for (auto& entry : result) {
            const ModuleBindings* b = moduleBindings(entry.first);
            if (!b || b->scale.steps.empty()) continue;
            extend(entry.second.module, *b);
            for (Group& group : entry.second.groups) extend(group.settings, *b);
        }
        return result;
    }();
    return table;
}

}  // namespace

const std::vector<SettingMeta>* moduleSettingsMeta(int moduleType) {
    auto it = all().find(moduleType);
    return it == all().end() ? nullptr : &it->second.module;
}

const std::vector<SettingMeta>* elementSettingsMeta(int moduleType, const std::string& elementName) {
    auto it = all().find(moduleType);
    if (it == all().end()) return nullptr;
    for (const Group& group : it->second.groups) {
        for (const std::string& name : group.names) {
            if (name == elementName) return &group.settings;
        }
    }
    return nullptr;
}

const std::vector<SettingRule>* moduleRules(int moduleType) {
    auto it = all().find(moduleType);
    return it == all().end() ? nullptr : &it->second.rules;
}

const SettingMeta* findSettingMeta(const std::vector<SettingMeta>& list, const std::string& name) {
    for (const SettingMeta& s : list) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

bool shownFor(const SettingMeta& child, const std::string& parentValue) {
    return child.parentValue.empty() || child.parentValue == "ALL" || child.parentValue == parentValue;
}

std::string fieldFor(const SettingMeta& meta, const std::string& value) {
    for (std::size_t i = 0; i < meta.values.size() && i < meta.fields.size(); ++i) {
        if (meta.values[i] == value) return meta.fields[i];
    }
    return value;
}

}  // namespace joue
