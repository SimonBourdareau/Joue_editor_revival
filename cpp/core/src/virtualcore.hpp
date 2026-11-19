#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "joue/bytes.hpp"

namespace joue {

// The platform side of a virtual port: a message written here reaches the program that listens to the port, and what that program sends
// to the port comes to the receiver. Messages are whole: a channel message, or a complete System Exclusive message.
class VirtualCore {
public:
    virtual ~VirtualCore() = default;
    virtual void write(const Bytes& message) = 0;
    void setReceiver(std::function<void(const Bytes&)> receiver) {
        std::lock_guard<std::mutex> lock(mutex_);
        receiver_ = std::move(receiver);
    }

protected:
    void received(const Bytes& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (receiver_) receiver_(message);
    }

private:
    std::mutex mutex_;
    std::function<void(const Bytes&)> receiver_;
};

std::unique_ptr<VirtualCore> makeVirtualCore(const std::string& name, std::string& error);
bool virtualCoreAvailable(std::string& explanation);

}  // namespace joue
