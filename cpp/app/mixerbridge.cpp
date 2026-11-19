#include "mixerbridge.hpp"

#include <chrono>

namespace {
constexpr std::size_t MaxLines = 400;
}

MixerBridge::MixerBridge() { thread_ = std::thread([this] { run(); }); }

MixerBridge::~MixerBridge() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    disconnect();
}

double MixerBridge::nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void MixerBridge::log(Kind kind, const joue::Bytes& message) {
    if (paused) return;
    if (kind != Kind::ToDaw && kind != Kind::ToExtender && !showIncoming) return;
    lines_.push_back({kind, joue::mixer::describe(message)});
    while (lines_.size() > MaxLines) lines_.pop_front();
}

void MixerBridge::note(const std::string& text) {
    lines_.push_back({Kind::Note, text});
    while (lines_.size() > MaxLines) lines_.pop_front();
}

void MixerBridge::deliver(const std::vector<joue::Bytes>& messages) {
    bool swapped = swapped_ && toExtenderPort_;
    for (const joue::Bytes& message : messages) {
        ++sent_;
        log(Kind::ToDaw, message);
        if (output) output(message);
        std::unique_ptr<joue::MidiPort>& port = swapped ? toExtenderPort_ : toDawPort_;
        if (port) {
            try {
                port->sendMessage(message);
                problem_.clear();
            } catch (const std::exception& error) {
                if (problem_ != error.what()) note(std::string("Sending to the DAW failed: ") + error.what());
                problem_ = error.what();
            }
        }
    }
    for (const joue::Bytes& message : bridge_.takeExtender()) {
        ++sentExtender_;
        log(Kind::ToExtender, message);
        std::unique_ptr<joue::MidiPort>& port = swapped ? toDawPort_ : toExtenderPort_;
        if (port) {
            try {
                port->sendMessage(message);
            } catch (const std::exception& error) {
                if (problem_ != error.what()) note(std::string("Sending to the second unit failed: ") + error.what());
                problem_ = error.what();
            }
        }
    }
}

void MixerBridge::fromSurface(const joue::Bytes& message) {
    if (!enabled) return;
    std::lock_guard<std::mutex> lock(mutex_);
    ++received_;
    log(Kind::FromScaler, message);
    std::vector<joue::Bytes> out;
    bridge_.fromSurface(message, nowMs(), out);
    deliver(out);
}

void MixerBridge::fromHost(const joue::Bytes& message, int pair) {
    std::lock_guard<std::mutex> lock(mutex_);
    pair = pair == 0 ? 0 : 1;
    if (message.size() >= 7 && message[0] == 0xF0 && message[1] == 0x00 && message[2] == 0x00 && message[3] == 0x66) {
        deviceSeen_[static_cast<std::size_t>(pair)] = message[4];
        bool sixteen = bridge_.options().faders == joue::mixer::Faders::Sixteen;
        if (sixteen && !swapLocked_ && !swapped_ && ((pair == 0 && message[4] == 0x15) || (pair == 1 && message[4] == 0x14))) {
            swapped_ = true;
            note("The DAW's main unit is on the second pair of ports: the two pairs have been swapped.");
        }
    }
    int unit = swapped_ ? 1 - pair : pair;
    if (unit == 0) ++fromDaw_;
    else ++fromExtender_;
    log(unit == 0 ? Kind::FromDaw : Kind::FromExtender, message);
    std::vector<joue::Bytes> out;
    bridge_.fromHost(message, out, unit);
    deliver(out);
}

MixerBridge::View MixerBridge::view() {
    std::lock_guard<std::mutex> lock(mutex_);
    View copy;
    copy.snapshot = bridge_.snapshot();
    copy.host = bridge_.host(0);
    copy.extender = bridge_.host(1);
    copy.options = bridge_.options();
    copy.shownStrips = bridge_.visibleStrips();
    copy.lines.assign(lines_.begin(), lines_.end());
    copy.received = received_;
    copy.sent = sent_;
    copy.fromDaw = fromDaw_;
    copy.fromExtender = fromExtender_;
    copy.sentExtender = sentExtender_;
    copy.connected = connected_;
    copy.connectedForMs = connected_ ? nowMs() - connectedAtMs_ : 0.0;
    copy.pairsSwapped = swapped_;
    copy.pairsLocked = swapLocked_;
    copy.deviceOnFirstPair = deviceSeen_[0];
    copy.deviceOnSecondPair = deviceSeen_[1];
    copy.connection = connection_;
    copy.problem = problem_;
    return copy;
}

void MixerBridge::attach(std::unique_ptr<joue::MidiPort> toDaw, std::unique_ptr<joue::MidiPort> fromDaw, std::unique_ptr<joue::MidiPort> toExtender, std::unique_ptr<joue::MidiPort> fromExtender, const std::string& text) {
    disconnect();
    if (fromDaw) {
        fromDaw->setHandler([this](const joue::Bytes& message) { fromHost(message, 0); });
        fromDaw->setMessageHandler([this](const joue::Bytes& message) { fromHost(message, 0); });
    }
    if (fromExtender) {
        fromExtender->setHandler([this](const joue::Bytes& message) { fromHost(message, 1); });
        fromExtender->setMessageHandler([this](const joue::Bytes& message) { fromHost(message, 1); });
    }
    std::lock_guard<std::mutex> lock(mutex_);
    toDawPort_ = std::move(toDaw);
    fromDawPort_ = std::move(fromDaw);
    toExtenderPort_ = std::move(toExtender);
    fromExtenderPort_ = std::move(fromExtender);
    connection_ = text;
    problem_.clear();
    connected_ = true;
    connectedAtMs_ = nowMs();
    deviceSeen_.fill(0);
    if (!swapLocked_) swapped_ = false;
    note("Connected: " + text);
}

bool MixerBridge::connectVirtual(const std::string& name, const std::string& secondName, std::string& error) {
    joue::VirtualPort first, second;
    if (!joue::createVirtualPort(name, first, error)) return false;
    std::string text = "the port \"" + name + "\" made by the program";
    if (!secondName.empty()) {
        if (!joue::createVirtualPort(secondName, second, error)) return false;
        text += " and the port \"" + secondName + "\" for the second unit";
    }
    attach(std::move(first.toDaw), std::move(first.fromDaw), std::move(second.toDaw), std::move(second.fromDaw), text);
    return true;
}

bool MixerBridge::connect(const joue::EndpointInfo& toDaw, const joue::EndpointInfo& fromDaw, const joue::EndpointInfo* toExtender, const joue::EndpointInfo* fromExtender, std::string& error) {
    if (toDaw.displayName == fromDaw.displayName) {
        error = "The port to send to and the port to listen to must be different.";
        return false;
    }
    if (toExtender && fromExtender) {
        if (toExtender->displayName == fromExtender->displayName) {
            error = "The port to send to and the port to listen to must be different.";
            return false;
        }
        if (toExtender->displayName == toDaw.displayName || fromExtender->displayName == fromDaw.displayName || toExtender->displayName == fromDaw.displayName || fromExtender->displayName == toDaw.displayName) {
            error = "The second unit needs its own two ports, not those of the first unit.";
            return false;
        }
    }
    try {
        std::unique_ptr<joue::MidiPort> out = joue::openOutputPort(toDaw);
        std::unique_ptr<joue::MidiPort> in = joue::openInputPort(fromDaw);
        std::unique_ptr<joue::MidiPort> extenderOut, extenderIn;
        std::string text = "sending to \"" + toDaw.displayName + "\", listening to \"" + fromDaw.displayName + "\"";
        if (toExtender && fromExtender) {
            extenderOut = joue::openOutputPort(*toExtender);
            extenderIn = joue::openInputPort(*fromExtender);
            text += "; second unit: sending to \"" + toExtender->displayName + "\", listening to \"" + fromExtender->displayName + "\"";
        }
        attach(std::move(out), std::move(in), std::move(extenderOut), std::move(extenderIn), text);
        return true;
    } catch (const std::exception& problem) {
        error = problem.what();
        return false;
    }
}

void MixerBridge::disconnect() {
    std::unique_ptr<joue::MidiPort> out, in, extenderOut, extenderIn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) return;
        std::vector<joue::Bytes> released;
        bridge_.releaseAll(released);
        deliver(released);
        out = std::move(toDawPort_);
        in = std::move(fromDawPort_);
        extenderOut = std::move(toExtenderPort_);
        extenderIn = std::move(fromExtenderPort_);
        connected_ = false;
        connection_.clear();
        note("Disconnected.");
    }
    in.reset();
    extenderIn.reset();
    out.reset();
    extenderOut.reset();
}

void MixerBridge::setBubbleSideways(joue::mixer::BubbleSideways mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    bridge_.setBubbleSideways(mode);
}

joue::mixer::BubbleSideways MixerBridge::bubbleSideways() {
    std::lock_guard<std::mutex> lock(mutex_);
    return bridge_.options().bubbleSideways;
}

void MixerBridge::setFaderMode(joue::mixer::FaderMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    bridge_.setFaderMode(mode);
}

void MixerBridge::setPairsSwapped(bool swapped) {
    swapLocked_ = true;
    swapped_ = swapped;
}

void MixerBridge::setBankStep(joue::mixer::BankStep step) {
    std::lock_guard<std::mutex> lock(mutex_);
    bridge_.setBankStep(step);
}

joue::mixer::BankStep MixerBridge::bankStep() {
    std::lock_guard<std::mutex> lock(mutex_);
    return bridge_.options().bankStep;
}

void MixerBridge::setOptions(const joue::mixer::Options& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    bridge_.setOptions(options);
}

void MixerBridge::goToFirstTrack() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 6; ++i) queued_.push_back(46);
    for (int i = 0; i < 24; ++i) queued_.push_back(48);
}

void MixerBridge::press(int mackieNote, int unit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<joue::Bytes> out;
    bridge_.press(mackieNote, out, unit);
    deliver(out);
}

void MixerBridge::clearLog() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
    received_ = 0;
    sent_ = 0;
    sentExtender_ = 0;
    fromDaw_ = 0;
    fromExtender_ = 0;
}

void MixerBridge::run() {
    bool wasEnabled = false;
    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bool now = enabled;
        std::vector<joue::Bytes> out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (now) bridge_.tick(nowMs(), out);
            else if (wasEnabled) bridge_.releaseAll(out);
            if (!queued_.empty() && nowMs() >= nextQueuedMs_) {
                bridge_.press(queued_.front(), out);
                queued_.pop_front();
                nextQueuedMs_ = nowMs() + 40.0;
            }
            deliver(out);
        }
        wasEnabled = now;
    }
}
