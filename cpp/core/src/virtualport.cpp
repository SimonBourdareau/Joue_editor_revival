#include <stdexcept>

#include "joue/endpoints.hpp"
#include "virtualcore.hpp"

namespace joue {

namespace {

class VirtualOut : public MidiPort {
public:
    explicit VirtualOut(std::shared_ptr<VirtualCore> core) : core_(std::move(core)) {}
    void send(const Bytes& message) override { core_->write(message); }
    void sendMessage(const Bytes& message) override {
        if (!message.empty()) core_->write(message);
    }

private:
    std::shared_ptr<VirtualCore> core_;
};

class VirtualIn : public MidiPort {
public:
    explicit VirtualIn(std::shared_ptr<VirtualCore> core) : core_(std::move(core)) {
        core_->setReceiver([this](const Bytes& message) { deliverAny(message); });
    }
    ~VirtualIn() override { core_->setReceiver(nullptr); }
    void send(const Bytes&) override { throw std::runtime_error("This port is only listened to."); }

private:
    std::shared_ptr<VirtualCore> core_;
};

}  // namespace

bool virtualPortsAvailable(std::string& explanation) { return virtualCoreAvailable(explanation); }

bool createVirtualPort(const std::string& name, VirtualPort& port, std::string& error) {
    std::unique_ptr<VirtualCore> made = makeVirtualCore(name, error);
    if (!made) {
        if (error.empty()) virtualCoreAvailable(error);
        return false;
    }
    std::shared_ptr<VirtualCore> core(std::move(made));
    port.toDaw = std::make_unique<VirtualOut>(core);
    port.fromDaw = std::make_unique<VirtualIn>(core);
    return true;
}

}  // namespace joue
