#include "joue/endpoints.hpp"

#include <algorithm>
#include <regex>

namespace joue {

namespace {

// Lower-cases ASCII and maps the common accented e (UTF-8) to a plain e, so "Joué" and "JOUÉ" fold to "joue".
std::string fold(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size()) {
            unsigned char next = static_cast<unsigned char>(text[i + 1]);
            if (next == 0xA9 || next == 0x89 || next == 0xA8 || next == 0x88 || next == 0xAA || next == 0x8A) {
                out += 'e';
                ++i;
                continue;
            }
        }
        out += static_cast<char>(c < 0x80 ? std::tolower(c) : c);
    }
    return out;
}

}  // namespace

PortRole roleOf(const EndpointInfo& endpoint) {
    static const std::regex bootloader(R"(joue bootloader)");
    static const std::regex editor(R"(joue edit|joue midi 2|(midiin|midiout)[^|]*joue)");
    static const std::regex play(R"(joue play|joue midi 1|joue(-[0-9]+)?)");
    std::string names = fold(endpoint.name) + " | " + fold(endpoint.displayName);
    std::string first = fold(endpoint.name);
    if (std::regex_search(names, bootloader)) return PortRole::Bootloader;
    if (std::regex_search(names, editor)) return PortRole::Editor;
    if (std::regex_match(first, play) || std::regex_search(first, std::regex(R"(joue play|joue midi 1)"))) return PortRole::Play;
    return PortRole::Other;
}

bool looksLikeJoue(const EndpointInfo& endpoint) { return fold(endpoint.allText()).find("joue") != std::string::npos; }

std::vector<PortPair> findPairs(const std::vector<EndpointInfo>& endpoints) {
    std::vector<EndpointInfo> sources, destinations;
    for (const auto& e : endpoints) {
        if (!looksLikeJoue(e)) continue;
        (e.isSource ? sources : destinations).push_back(e);
    }
    auto byIndex = [](const EndpointInfo& a, const EndpointInfo& b) { return a.index < b.index; };
    std::sort(sources.begin(), sources.end(), byIndex);
    std::sort(destinations.begin(), destinations.end(), byIndex);
    std::vector<PortPair> pairs;
    for (const auto& source : sources) {
        if (source.entityRef == 0) continue;
        auto match = std::find_if(destinations.begin(), destinations.end(), [&](const EndpointInfo& d) { return d.entityRef == source.entityRef; });
        if (match != destinations.end()) pairs.push_back({source, *match});
    }
    if (pairs.empty()) {
        std::size_t count = std::min(sources.size(), destinations.size());
        for (std::size_t i = 0; i < count; ++i) pairs.push_back({sources[i], destinations[i]});
    }
    return pairs;
}

int chooseEditorPair(const std::vector<PortPair>& pairs) {
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (roleOf(pairs[i].source) == PortRole::Editor && roleOf(pairs[i].destination) == PortRole::Editor) return static_cast<int>(i);
    }
    static const std::regex marker(R"((midi(in|out)\s*2|\bport\s*2\b|\b2\b))", std::regex::icase);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const PortPair& p = pairs[i];
        if (std::regex_search(p.source.name + " " + p.source.entity, marker) || std::regex_search(p.destination.name + " " + p.destination.entity, marker)) return static_cast<int>(i);
    }
    if (pairs.size() > 1) return 1;
    return pairs.empty() ? -1 : 0;
}

}  // namespace joue
