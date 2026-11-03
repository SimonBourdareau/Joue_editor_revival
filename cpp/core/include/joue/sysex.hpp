#pragma once
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "joue/bytes.hpp"

namespace joue::sysex {

constexpr std::uint8_t Start = 0xF0;
constexpr std::uint8_t End = 0xF7;
constexpr std::uint8_t UniversalNonRealtime = 0x7E;
constexpr std::uint8_t DeviceId = 0x01;
constexpr std::uint8_t CommandFile = 0x07;
constexpr std::uint8_t SubHeader = 0x01;
constexpr std::uint8_t SubData = 0x02;
constexpr std::uint8_t SubRequest = 0x03;
constexpr std::uint8_t CommandEof = 0x7B;
constexpr std::uint8_t CommandNak = 0x7E;
constexpr std::uint8_t CommandAck = 0x7F;
constexpr std::size_t MaxRawBytesPerPacket = 30;
constexpr int PacketCount = 128;

Bytes pack(const std::uint8_t* raw, std::size_t size);
Bytes unpack(const std::uint8_t* wire, std::size_t size);
std::uint8_t xorSum(const std::uint8_t* data, std::size_t size);

Bytes openWrite(std::string_view name);
Bytes requestRead(std::string_view name);
Bytes dataPacket(int packet, const std::uint8_t* raw, std::size_t size);
Bytes ack(int packet);
Bytes nak(int packet);
Bytes eof();

struct Header { Bytes type, length, name; };
struct Data { int packet; Bytes data; bool checksumValid; };
struct Ack { int packet; };
struct Nak { int packet; };
struct Eof {};
using Message = std::variant<Header, Data, Ack, Nak, Eof>;

std::optional<Message> parse(const Bytes& frame);
std::vector<Bytes> split(const Bytes& buffer);

// Rebuilds complete messages from chunks: SysEx can arrive split over several packets, with real-time bytes interleaved.
class Assembler {
public:
    std::vector<Bytes> feed(const std::uint8_t* data, std::size_t size);
    void reset();

private:
    Bytes buffer_;
    bool active_ = false;
};

}  // namespace joue::sysex
