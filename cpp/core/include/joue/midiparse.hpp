#pragma once
#include <vector>

#include "joue/bytes.hpp"

namespace joue {

// Splits a MIDI byte stream into complete messages: channel messages (running status understood) and whole SysEx messages.
// Real-time bytes are skipped; other system messages are ignored.
class MessageParser {
public:
    std::vector<Bytes> feed(const std::uint8_t* data, std::size_t size);
    void reset();

private:
    Bytes sysex_, pending_;
    bool inSysex_ = false;
    std::uint8_t running_ = 0;
    std::size_t need_ = 0;
};

}  // namespace joue
