#include "joue/transfer.hpp"

namespace joue {

void MidiPort::setHandler(std::function<void(const Bytes&)> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handler_ = std::move(handler);
}

void MidiPort::setMessageHandler(std::function<void(const Bytes&)> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    messageHandler_ = std::move(handler);
}

void MidiPort::deliverAny(const Bytes& message) {
    if (message.empty()) return;
    if (message[0] == 0xF0) {
        deliver(message);
        return;
    }
    std::function<void(const Bytes&)> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler = messageHandler_;
    }
    if (handler) handler(message);
}

void MidiPort::deliver(const Bytes& sysex) {
    std::function<void(const Bytes&)> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler = handler_;
    }
    if (handler) handler(sysex);
}

TransferClient::TransferClient(MidiPort& port, std::chrono::milliseconds timeout) : port_(port), timeout_(timeout) {
    port_.setHandler([this](const Bytes& frame) { onSysex(frame); });
}

TransferClient::~TransferClient() { port_.setHandler(nullptr); }

void TransferClient::setInfoHandler(std::function<void(const Bytes&)> handler) {
    std::lock_guard<std::mutex> lock(infoMutex_);
    info_ = std::move(handler);
}

void TransferClient::onSysex(const Bytes& buffer) {
    for (const Bytes& frame : sysex::split(buffer)) {
        auto message = sysex::parse(frame);
        if (!message) continue;
        if (auto* header = std::get_if<sysex::Header>(&*message)) {
            std::function<void(const Bytes&)> handler;
            {
                std::lock_guard<std::mutex> lock(infoMutex_);
                handler = info_;
            }
            if (handler) handler(header->name);
        } else {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                inbox_.push_back(std::move(*message));
            }
            wake_.notify_all();
        }
    }
}

std::optional<sysex::Message> TransferClient::next() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!wake_.wait_for(lock, timeout_, [this] { return !inbox_.empty(); })) return std::nullopt;
    sysex::Message message = std::move(inbox_.front());
    inbox_.pop_front();
    return message;
}

void TransferClient::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.clear();
}

bool TransferClient::expectAck(int packet) {
    while (true) {
        auto message = next();
        if (!message || std::holds_alternative<sysex::Nak>(*message)) return false;
        if (auto* ack = std::get_if<sysex::Ack>(&*message)) {
            // An acknowledgement for another packet number counts as a failure, as in the original editor.
            return ack->packet == packet;
        }
    }
}

bool TransferClient::write(const std::string& name, const Bytes& data) {
    std::lock_guard<std::mutex> operation(operation_);
    drain();
    port_.send(sysex::openWrite(name));
    if (!expectAck(0)) return false;
    int packet = 0;
    for (std::size_t position = 0; position < data.size(); position += sysex::MaxRawBytesPerPacket) {
        std::size_t count = std::min(sysex::MaxRawBytesPerPacket, data.size() - position);
        port_.send(sysex::dataPacket(packet, data.data() + position, count));
        if (!expectAck(packet)) return false;
        packet = (packet + 1) % sysex::PacketCount;
    }
    port_.send(sysex::eof());
    return true;
}

std::optional<Bytes> TransferClient::read(const std::string& name) {
    std::lock_guard<std::mutex> operation(operation_);
    drain();
    port_.send(sysex::requestRead(name));
    Bytes received;
    int expected = 0;
    while (true) {
        auto message = next();
        if (!message || std::holds_alternative<sysex::Nak>(*message)) return std::nullopt;
        if (std::holds_alternative<sysex::Eof>(*message)) return received;
        if (auto* data = std::get_if<sysex::Data>(&*message)) {
            if (data->packet != expected) return std::nullopt;
            expected = (expected + 1) % sysex::PacketCount;
            received.insert(received.end(), data->data.begin(), data->data.end());
            if (acknowledgeReceived_) port_.send(sysex::ack(data->packet));
        }
    }
}

}  // namespace joue
