#include "joue/update.hpp"

#include <cctype>
#include <vector>

namespace joue {

namespace {

bool numbers(const std::string& text, std::vector<long>& out) {
    std::size_t at = 0;
    if (at < text.size() && (text[at] == 'v' || text[at] == 'V')) ++at;
    std::string current;
    bool any = false;
    for (; at <= text.size(); ++at) {
        char c = at < text.size() ? text[at] : '\0';
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current += c;
            any = true;
        } else if (c == '.' || c == '\0' || c == '-' || c == '+') {
            if (current.empty()) return any;
            out.push_back(std::stol(current));
            current.clear();
            if (c != '.') return true;
        } else {
            return any && !out.empty();
        }
    }
    return any;
}

}  // namespace

bool isNewerVersion(const std::string& latest, const std::string& current) {
    std::vector<long> a, b;
    if (!numbers(latest, a) || !numbers(current, b) || a.empty() || b.empty()) return false;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

std::string jsonStringValue(const std::string& json, const std::string& key) {
    std::string quoted = "\"" + key + "\"";
    std::size_t at = json.find(quoted);
    while (at != std::string::npos) {
        std::size_t colon = json.find(':', at + quoted.size());
        if (colon == std::string::npos) return {};
        std::size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
        if (start != std::string::npos && json[start] == '"') {
            std::string out;
            for (std::size_t i = start + 1; i < json.size(); ++i) {
                if (json[i] == '\\' && i + 1 < json.size()) out += json[++i];
                else if (json[i] == '"') return out;
                else out += json[i];
            }
            return {};
        }
        at = json.find(quoted, at + quoted.size());
    }
    return {};
}

}  // namespace joue
