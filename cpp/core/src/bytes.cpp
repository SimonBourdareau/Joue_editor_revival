#include "joue/bytes.hpp"

#include <cctype>
#include <stdexcept>

namespace joue {

std::string toHex(const Bytes& bytes, bool spaced) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (spaced && i > 0) out += ' ';
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

Bytes parseHex(const std::string& text) {
    std::string compact;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c)) && c != ',' && c != ':') compact += c;
    }
    if (compact.size() >= 2 && compact[0] == '0' && (compact[1] == 'x' || compact[1] == 'X')) compact.erase(0, 2);
    if (compact.empty() || compact.size() % 2 != 0) throw std::runtime_error("Expected an even number of hexadecimal digits.");
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("Expected an even number of hexadecimal digits.");
    };
    Bytes out;
    for (std::size_t i = 0; i < compact.size(); i += 2) out.push_back(static_cast<std::uint8_t>(value(compact[i]) * 16 + value(compact[i + 1])));
    return out;
}

}  // namespace joue
