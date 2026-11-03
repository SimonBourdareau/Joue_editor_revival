#include "joue/sysex.hpp"

#include <algorithm>
#include <stdexcept>

namespace joue::sysex {

namespace {
constexpr std::size_t GroupSize = 7;
const std::uint8_t FileType[4] = {'B', 'I', 'N', ' '};
}  // namespace

// MIDI SysEx data bytes are 7-bit, so every group of up to 7 raw bytes is sent as one byte holding the stripped
// high bits (bit 6 - i belongs to raw byte i) followed by the 7-bit bytes.
Bytes pack(const std::uint8_t* raw, std::size_t size) {
    Bytes out;
    for (std::size_t position = 0; position < size; position += GroupSize) {
        std::size_t count = std::min(GroupSize, size - position);
        std::uint8_t high = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (raw[position + i] & 0x80) high |= static_cast<std::uint8_t>(1 << (6 - i));
        }
        out.push_back(high);
        for (std::size_t i = 0; i < count; ++i) out.push_back(raw[position + i] & 0x7F);
    }
    return out;
}

Bytes unpack(const std::uint8_t* wire, std::size_t size) {
    Bytes out;
    std::size_t position = 0;
    while (position < size) {
        std::uint8_t high = wire[position++];
        std::size_t count = std::min(GroupSize, size - position);
        for (std::size_t i = 0; i < count; ++i) {
            std::uint8_t value = wire[position + i] & 0x7F;
            if (high & (1 << (6 - i))) value |= 0x80;
            out.push_back(value);
        }
        position += count;
    }
    return out;
}

std::uint8_t xorSum(const std::uint8_t* data, std::size_t size) {
    std::uint8_t result = 0;
    for (std::size_t i = 0; i < size; ++i) result ^= data[i];
    return result;
}

Bytes openWrite(std::string_view name) {
    Bytes frame = {Start, UniversalNonRealtime, DeviceId, CommandFile, SubHeader, DeviceId};
    frame.insert(frame.end(), FileType, FileType + 4);
    frame.insert(frame.end(), 4, 0);
    frame.insert(frame.end(), name.begin(), name.end());
    frame.push_back(End);
    return frame;
}

Bytes requestRead(std::string_view name) {
    Bytes frame = {Start, UniversalNonRealtime, DeviceId, CommandFile, SubRequest, DeviceId};
    frame.insert(frame.end(), FileType, FileType + 4);
    frame.insert(frame.end(), name.begin(), name.end());
    frame.push_back(End);
    return frame;
}

Bytes dataPacket(int packet, const std::uint8_t* raw, std::size_t size) {
    if (size == 0 || size > MaxRawBytesPerPacket) throw std::invalid_argument("A packet carries 1 to 30 raw bytes.");
    Bytes wire = pack(raw, size);
    Bytes frame = {Start, UniversalNonRealtime, DeviceId, CommandFile, SubData, static_cast<std::uint8_t>(packet & 0x7F),
                   static_cast<std::uint8_t>(wire.size() - 1)};
    frame.insert(frame.end(), wire.begin(), wire.end());
    // The checksum covers everything between the start byte and itself.
    frame.push_back(xorSum(frame.data() + 1, frame.size() - 1));
    frame.push_back(End);
    return frame;
}

static Bytes shortMessage(std::uint8_t command, int packet) {
    return {Start, UniversalNonRealtime, DeviceId, command, static_cast<std::uint8_t>(packet & 0x7F), End};
}

Bytes ack(int packet) { return shortMessage(CommandAck, packet); }
Bytes nak(int packet) { return shortMessage(CommandNak, packet); }
Bytes eof() { return shortMessage(CommandEof, 0); }

std::optional<Message> parse(const Bytes& frame) {
    if (frame.size() < 6 || frame.front() != Start || frame.back() != End) return std::nullopt;
    if (frame[1] != UniversalNonRealtime || frame[2] != DeviceId) return std::nullopt;
    switch (frame[3]) {
        case CommandAck: return Message(Ack{frame[4]});
        case CommandNak: return Message(Nak{frame[4]});
        case CommandEof: return Message(Eof{});
        case CommandFile: break;
        default: return std::nullopt;
    }
    std::uint8_t sub = frame[4];
    if (sub == SubHeader) {
        if (frame.size() < 15 || frame[5] != DeviceId) return std::nullopt;
        // The name field runs to the end byte and is at most 50 bytes long.
        std::size_t nameLength = std::min<std::size_t>(frame.size() - 1 - 14, 50);
        return Message(Header{Bytes(frame.begin() + 6, frame.begin() + 10), Bytes(frame.begin() + 10, frame.begin() + 14),
                              Bytes(frame.begin() + 14, frame.begin() + 14 + nameLength)});
    }
    if (sub == SubData) {
        if (frame.size() < 10) return std::nullopt;
        std::size_t wireLength = static_cast<std::size_t>(frame[6]) + 1;
        std::size_t checksumIndex = 7 + wireLength;
        if (frame.size() != checksumIndex + 2) return std::nullopt;
        bool valid = xorSum(frame.data() + 1, checksumIndex - 1) == frame[checksumIndex];
        return Message(Data{frame[5], unpack(frame.data() + 7, wireLength), valid});
    }
    return std::nullopt;
}

// USB-MIDI drivers may hand over several messages in one buffer; cut it at every end byte.
std::vector<Bytes> split(const Bytes& buffer) {
    std::vector<Bytes> out;
    long start = -1;
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] == Start) {
            start = static_cast<long>(i);
        } else if (buffer[i] == End && start >= 0) {
            out.emplace_back(buffer.begin() + start, buffer.begin() + i + 1);
            start = -1;
        }
    }
    return out;
}

std::vector<Bytes> Assembler::feed(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t MaxLength = 4096;
    std::vector<Bytes> completed;
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t b = data[i];
        if (b >= 0xF8) continue;  // real-time bytes may be interleaved
        if (b == Start) {
            buffer_.assign(1, b);
            active_ = true;
        } else if (b == End) {
            if (active_) {
                buffer_.push_back(b);
                completed.push_back(buffer_);
            }
            reset();
        } else if (b >= 0x80) {
            reset();  // any other status byte means the SysEx was cut short
        } else if (active_) {
            buffer_.push_back(b);
            if (buffer_.size() > MaxLength) reset();
        }
    }
    return completed;
}

void Assembler::reset() {
    buffer_.clear();
    active_ = false;
}

}  // namespace joue::sysex
