#include "joue/midiparse.hpp"

namespace joue {

std::vector<Bytes> MessageParser::feed(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t MaxSysex = 4096;
    std::vector<Bytes> out;
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t b = data[i];
        if (b >= 0xF8) continue;
        if (inSysex_) {
            if (b == 0xF7) {
                sysex_.push_back(b);
                out.push_back(sysex_);
                inSysex_ = false;
                sysex_.clear();
                continue;
            }
            if (b < 0x80) {
                sysex_.push_back(b);
                if (sysex_.size() > MaxSysex) {
                    inSysex_ = false;
                    sysex_.clear();
                }
                continue;
            }
            inSysex_ = false;  // a status byte cut the SysEx short; it is handled below
            sysex_.clear();
        }
        if (b == 0xF0) {
            inSysex_ = true;
            sysex_.assign(1, b);
            running_ = 0;
            pending_.clear();
        } else if (b >= 0xF0) {
            running_ = 0;
            pending_.clear();
        } else if (b >= 0x80) {
            running_ = b;
            pending_.clear();
            need_ = (b >= 0xC0 && b <= 0xDF) ? 1 : 2;
        } else if (running_) {
            pending_.push_back(b);
            if (pending_.size() == need_) {
                Bytes message{running_};
                message.insert(message.end(), pending_.begin(), pending_.end());
                out.push_back(message);
                pending_.clear();
            }
        }
    }
    return out;
}

void MessageParser::reset() {
    sysex_.clear();
    pending_.clear();
    inSysex_ = false;
    running_ = 0;
    need_ = 0;
}

}  // namespace joue
