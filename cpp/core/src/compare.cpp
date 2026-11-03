#include "joue/compare.hpp"

#include <map>

namespace joue {

namespace {

void collect(const Node& node, const std::string& path, std::map<std::string, std::string>& values) {
    std::size_t unnamed = 0;
    for (const auto& child : node.children) {
        std::string name = child->name();
        if (name.empty()) name = child->tag + "#" + std::to_string(unnamed++);
        std::string here = path.empty() ? name : path + "/" + name;
        if (const std::string* value = child->attr("value")) values[here] = *value;
        collect(*child, here, values);
    }
}

}  // namespace

std::vector<std::string> compareModules(const Node& first, const Node& second) {
    std::map<std::string, std::string> a, b;
    collect(first, "", a);
    collect(second, "", b);
    a.erase("Name");
    b.erase("Name");
    std::vector<std::string> lines;
    for (const auto& kv : a) {
        auto other = b.find(kv.first);
        if (other == b.end()) lines.push_back(kv.first + ": (only in the first)");
        else if (other->second != kv.second) lines.push_back(kv.first + ": " + kv.second + " -> " + other->second);
    }
    for (const auto& kv : b) {
        if (a.find(kv.first) == a.end()) lines.push_back(kv.first + ": (only in the second)");
    }
    return lines;
}

}  // namespace joue
