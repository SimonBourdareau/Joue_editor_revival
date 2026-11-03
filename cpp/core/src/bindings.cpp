#include <cstdlib>
#include <sstream>

#include "joue/instrumentmodule.hpp"

namespace joue {

namespace {

#include "bindings_data.inc"

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

const std::map<int, ModuleBindings>& all() {
    static const std::map<int, ModuleBindings> table = [] {
        std::map<int, ModuleBindings> result;
        ModuleBindings* module = nullptr;
        Binding* binding = nullptr;
        FunctionTable* table = nullptr;
        for (const char* text : kBindingLines) {
            std::vector<std::string> f = split(text, '\t');
            if (f.empty()) continue;
            char kind = f[0][0];
            if (kind == 'T' && f.size() >= 5) {
                module = &result[std::atoi(f[1].c_str())];
                module->type = std::atoi(f[1].c_str());
                module->name = f[2];
                module->structure = f[3];
                module->names = split(f[4], ',');
                for (const std::string& signature : split(f[3], ',')) {
                    std::vector<std::string> part = split(signature, ':');
                    module->baselineTypes.push_back({part.size() > 0 ? std::atoi(part[0].c_str()) : 0, part.size() > 1 ? std::atoi(part[1].c_str()) : 0});
                }
                table = nullptr;
                binding = nullptr;
            } else if (!module) {
                continue;
            } else if (kind == 'L' && f.size() >= 5) {
                module->scale.scope = f[1];
                module->scale.noteName = f[2];
                module->scale.scaleName = f[3];
                module->scale.expertName = f[4];
            } else if (kind == 'l' && f.size() >= 3) {
                std::vector<int> steps;
                std::string number;
                for (char c : f[2] + ",") {
                    if (c == ',') {
                        if (!number.empty()) steps.push_back(std::atoi(number.c_str()));
                        number.clear();
                    } else {
                        number += c;
                    }
                }
                module->scale.steps.push_back(steps);
                module->scale.titles.push_back(f.size() >= 4 ? f[3] : std::string());
            } else if (kind == 'Q') {
                module->expertAlways = true;
            } else if (kind == 'D' && f.size() >= 5) {
                module->depthValues = {f[3], f[4]};
            } else if (kind == 'd' && f.size() >= 4) {
                module->depthRules.push_back({f[1], std::atoi(f[2].c_str()), std::atoi(f[3].c_str())});
            } else if (kind == 'A' && f.size() >= 5) {
                module->aftertouchPrefix = f[4];
            } else if (kind == 'F' && f.size() >= 4) {
                module->functions.emplace_back();
                table = &module->functions.back();
                table->scope = f[1];
                table->record = std::atoi(f[2].c_str());
                table->baseline = f[3];
                binding = nullptr;
            } else if (kind == 'f' && table && f.size() >= 7) {
                FunctionVariant v;
                v.value = f[1];
                v.type = std::atoi(f[2].c_str());
                v.version = std::atoi(f[3].c_str());
                for (std::size_t i = 0; i + 1 < f[4].size(); i += 2) v.record.push_back(static_cast<std::uint8_t>(std::strtol(f[4].substr(i, 2).c_str(), nullptr, 16)));
                if (f[5] != "-") v.text = f[5];
                if (f[6] != "-") {
                    for (const std::string& pair : split(f[6], ',')) {
                        std::vector<std::string> part = split(pair, ':');
                        if (part.size() == 2) v.disc.push_back({std::atoi(part[0].c_str()), std::atoi(part[1].c_str())});
                    }
                }
                table->variants.push_back(v);
            } else if (kind == 'P' && f.size() >= 8) {
                module->bindings.emplace_back();
                binding = &module->bindings.back();
                binding->scope = f[1];
                binding->name = f[2];
                binding->dirName = f[3];
                binding->when = f[4];
                binding->pair = f[5];
                binding->dirValues = {f[6], f[7]};
                binding->numeric = true;
            } else if ((kind == 'N' || kind == 'E' || kind == 'X') && f.size() >= 3) {
                module->bindings.emplace_back();
                binding = &module->bindings.back();
                binding->scope = f[1];
                binding->name = f[2];
                binding->numeric = kind != 'E';
                if (kind != 'E' && f.size() > 3) binding->base = std::atoi(f[3].c_str());
                if (f.size() > 4) binding->when = f[4];
            } else if (!binding) {
                continue;
            } else if (kind == 'o' && f.size() >= 2) {
                binding->options.emplace_back();
                binding->options.back().value = f[1];
            } else if (kind == 'b' && f.size() >= 4) {
                ByteCell cell;
                cell.record = std::atoi(f[1].c_str());
                cell.offset = std::atoi(f[2].c_str());
                cell.base = std::atoi(f[3].c_str());
                if (binding->numeric) {
                    if (f.size() > 4) cell.slope = std::atof(f[4].c_str());
                    binding->bytes.push_back(cell);
                } else if (!binding->options.empty()) {
                    if (f.size() > 4) cell.mask = std::atoi(f[4].c_str());
                    binding->options.back().cells.push_back(cell);
                }
            } else if (kind == 'x' && f.size() >= 4) {
                ScriptCell cell;
                cell.key = f[1];
                cell.base = std::atoi(f[2].c_str());
                cell.slope = std::atof(f[3].c_str());
                binding->text.push_back(cell);
            } else if (kind == 's' && f.size() >= 4) {
                ScriptCell cell;
                cell.key = f[1];
                cell.base = std::atoi(f[2].c_str());
                cell.slope = std::atof(f[3].c_str());
                binding->script.push_back(cell);
            } else if ((kind == 'a' || kind == 'r') && f.size() >= 2 && !binding->options.empty()) {
                (kind == 'a' ? binding->options.back().added : binding->options.back().removed).push_back(f[1]);
            }
        }
        return result;
    }();
    return table;
}

}  // namespace

const ModuleBindings* moduleBindings(int type) {
    auto it = all().find(type);
    return it == all().end() ? nullptr : &it->second;
}

}  // namespace joue
