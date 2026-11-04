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

}  // namespace joue
