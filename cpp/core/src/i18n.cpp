#include "joue/i18n.hpp"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace joue::i18n {

namespace {

#include "i18n_fr.inc"

struct Pattern {
    std::vector<std::string> literals;  // the text around the {}: one more than the number of {}
    std::string french;
};

struct Catalogue {
    std::unordered_map<std::string, std::string> exact;
    std::vector<Pattern> patterns;
    std::unordered_map<std::string, std::string> labels;  // translated labels, with their ids, kept so that their pointers stay valid
    std::mutex mutex;
};

Language current = Language::English;

Catalogue& catalogue() {
    static Catalogue* c = [] {
        Catalogue* made = new Catalogue();
        for (const char* const* entry_ = kFrench; *entry_; ++entry_) {
            const char* line = *entry_;
            std::string entry = line;
            std::size_t tab = entry.find('\t');
            if (tab == std::string::npos) continue;
            std::string english = entry.substr(0, tab), french = entry.substr(tab + 1);
            if (french.empty()) continue;
            if (english.find("{}") == std::string::npos) {
                made->exact[english] = french;
                continue;
            }
            Pattern p;
            p.french = french;
            std::size_t from = 0;
            for (std::size_t at; (at = english.find("{}", from)) != std::string::npos; from = at + 2) p.literals.push_back(english.substr(from, at - from));
            p.literals.push_back(english.substr(from));
            made->patterns.push_back(std::move(p));
        }
        std::stable_sort(made->patterns.begin(), made->patterns.end(), [](const Pattern& a, const Pattern& b) {
            auto fixed = [](const Pattern& p) {
                std::size_t n = 0;
                for (const std::string& l : p.literals) n += l.size();
                return n;
            };
            return fixed(a) > fixed(b);
        });
        return made;
    }();
    return *c;
}

// The parts of the text that stand where the {} are, when the text has the shape of the pattern.
bool match(const Pattern& pattern, const std::string& text, std::vector<std::string>& parts) {
    const std::vector<std::string>& lit = pattern.literals;
    if (text.size() < lit.front().size() + lit.back().size()) return false;
    if (text.compare(0, lit.front().size(), lit.front()) != 0) return false;
    if (text.compare(text.size() - lit.back().size(), lit.back().size(), lit.back()) != 0) return false;
    std::size_t at = lit.front().size(), end = text.size() - lit.back().size();
    parts.clear();
    for (std::size_t i = 1; i < lit.size(); ++i) {
        std::size_t next;
        if (i + 1 == lit.size()) {
            next = end;  // the last part runs up to the closing text
            if (next < at) return false;
        } else {
            next = text.find(lit[i], at);
            if (next == std::string::npos || next > end) return false;
        }
        parts.push_back(text.substr(at, next - at));
        at = next + (i + 1 == lit.size() ? 0 : lit[i].size());
    }
    return true;
}

}  // namespace

void setLanguage(Language language) { current = language; }
Language language() { return current; }
const char* languageCode(Language language) { return language == Language::French ? "fr" : "en"; }
Language languageFromCode(const std::string& code) { return code.compare(0, 2, "fr") == 0 || code.compare(0, 2, "FR") == 0 ? Language::French : Language::English; }

Language detectSystemLanguage() {
#if defined(_WIN32)
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {0};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0 && (name[0] == L'f' || name[0] == L'F') && (name[1] == L'r' || name[1] == L'R')) return Language::French;
    return Language::English;
#elif defined(__APPLE__)
    Language found = Language::English;
    CFArrayRef languages = CFLocaleCopyPreferredLanguages();
    if (languages) {
        if (CFArrayGetCount(languages) > 0) {
            char code[32] = {0};
            CFStringRef first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, 0));
            if (CFStringGetCString(first, code, sizeof code, kCFStringEncodingUTF8)) found = languageFromCode(code);
        }
        CFRelease(languages);
    }
    return found;
#else
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANGUAGE", "LANG"}) {
        const char* value = std::getenv(name);
        if (value && *value) return languageFromCode(value);
    }
    return Language::English;
#endif
}

const char* tr(const char* english) {
    if (current != Language::French || !english || !*english) return english;
    Catalogue& c = catalogue();
    std::lock_guard<std::mutex> lock(c.mutex);
    auto cached = c.labels.find(english);
    if (cached != c.labels.end()) return cached->second.c_str();
    std::string full = english, text = english, id;
    std::size_t hashes = full.find("##");
    if (hashes != std::string::npos) {
        text = full.substr(0, hashes);
        id = full.substr(hashes);
    }
    auto hit = c.exact.find(text);
    std::string result = hit != c.exact.end() ? hit->second + id : full;
    return c.labels.emplace(full, std::move(result)).first->second.c_str();
}

namespace {

std::string translateMessage(const std::string& english, int depth) {
    Catalogue& c = catalogue();
    auto hit = c.exact.find(english);
    if (hit != c.exact.end()) return hit->second;
    std::vector<std::string> parts;
    for (const Pattern& p : c.patterns) {
        if (!match(p, english, parts)) continue;
        // a part that varies may be a message itself ("Not written: " followed by the problem that was found)
        if (depth < 3) {
            for (std::string& part : parts) part = translateMessage(part, depth + 1);
        }
        std::string out = p.french;
        std::size_t used = 0, from = 0;
        for (std::size_t at; used < parts.size() && (at = out.find("{}", from)) != std::string::npos; ++used) {
            out.replace(at, 2, parts[used]);
            from = at + parts[used].size();
        }
        return out;
    }
    return english;
}

}  // namespace

std::string message(const std::string& english) {
    if (current != Language::French || english.empty()) return english;
    Catalogue& c = catalogue();
    std::lock_guard<std::mutex> lock(c.mutex);
    return translateMessage(english, 0);
}

std::string text(const std::string& english) {
    if (current != Language::French || english.empty()) return english;
    Catalogue& c = catalogue();
    std::lock_guard<std::mutex> lock(c.mutex);
    auto hit = c.exact.find(english);
    return hit != c.exact.end() ? hit->second : english;
}

}  // namespace joue::i18n
