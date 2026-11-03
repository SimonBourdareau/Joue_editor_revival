#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "joue/bytes.hpp"
#include "joue/sysex.hpp"

namespace joue {

// Platform MIDI backends derive from this; deliver() is called on any thread with one complete SysEx message.
class MidiPort {
public:
    virtual ~MidiPort() = default;
    virtual void send(const Bytes& sysex) = 0;
    void setHandler(std::function<void(const Bytes&)> handler);
    // Channel messages (notes, pressure, controllers, pitch bend) of a port that is listened to.
    void setMessageHandler(std::function<void(const Bytes&)> handler);

protected:
    void deliver(const Bytes& sysex);
    void deliverAny(const Bytes& message);  // SysEx goes to the transfer handler, anything else to the message handler

private:
    std::mutex mutex_;
    std::function<void(const Bytes&)> handler_, messageHandler_;
};

class TransferClient {
public:
    explicit TransferClient(MidiPort& port, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
    void setTimeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }  // how long to wait for the instrument's next message
    ~TransferClient();
    TransferClient(const TransferClient&) = delete;
    TransferClient& operator=(const TransferClient&) = delete;

    bool write(const std::string& name, const Bytes& data);
    std::optional<Bytes> read(const std::string& name);
    void setInfoHandler(std::function<void(const Bytes&)> handler);
    // The original editor never acknowledged packets it received; off until confirmed on hardware.
    void setAcknowledgeReceived(bool enabled) { acknowledgeReceived_ = enabled; }

private:
    void onSysex(const Bytes& frame);
    std::optional<sysex::Message> next();
    bool expectAck(int packet);
    void drain();

    MidiPort& port_;
    std::chrono::milliseconds timeout_;
    bool acknowledgeReceived_ = false;
    std::mutex operation_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<sysex::Message> inbox_;
    std::mutex infoMutex_;
    std::function<void(const Bytes&)> info_;
};

}  // namespace joue
