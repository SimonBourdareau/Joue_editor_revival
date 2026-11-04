#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace joue {

using Bytes = std::vector<std::uint8_t>;

std::string toHex(const Bytes& bytes, bool spaced = true);

// Accepts digits with optional spaces, commas, colons and a 0x prefix; throws std::runtime_error otherwise.

inline Bytes toBytes(const std::string& text) { return Bytes(text.begin(), text.end()); }

}  // namespace joue
