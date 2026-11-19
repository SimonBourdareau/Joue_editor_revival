#ifdef __APPLE__
#include <stdexcept>

#include "joue/endpoints.hpp"
#include "joue/midiparse.hpp"
#include "virtualcore.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <cstring>

namespace joue {

namespace {

std::string toString(CFStringRef text) {
    if (!text) return {};
    CFIndex length = CFStringGetLength(text);
    CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(text, out.data(), capacity, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string stringProperty(MIDIObjectRef object, CFStringRef key) {
    if (!object) return {};
    CFStringRef value = nullptr;
    if (MIDIObjectGetStringProperty(object, key, &value) != noErr || !value) return {};
    std::string out = toString(value);
    CFRelease(value);
    return out;
}

class CoreMidiPort : public MidiPort {
public:
    explicit CoreMidiPort(const PortPair& pair) : source_(pair.source.ref), destination_(pair.destination.ref) {
        check(MIDIClientCreate(CFSTR("Joue Editor NG"), nullptr, nullptr, &client_), "MIDIClientCreate");
        check(MIDIInputPortCreate(client_, CFSTR("Joue input"), &CoreMidiPort::readProc, this, &input_), "MIDIInputPortCreate");
        check(MIDIOutputPortCreate(client_, CFSTR("Joue output"), &output_), "MIDIOutputPortCreate");
        check(MIDIPortConnectSource(input_, source_, nullptr), "MIDIPortConnectSource");
    }

    CoreMidiPort(const EndpointInfo& endpoint, bool outputSide) {
        check(MIDIClientCreate(CFSTR("Joue Editor NG"), nullptr, nullptr, &client_), "MIDIClientCreate");
        if (outputSide) {
            destination_ = endpoint.ref;
            check(MIDIOutputPortCreate(client_, CFSTR("Joue output"), &output_), "MIDIOutputPortCreate");
        } else {
            source_ = endpoint.ref;
            check(MIDIInputPortCreate(client_, CFSTR("Joue input"), &CoreMidiPort::readProc, this, &input_), "MIDIInputPortCreate");
            check(MIDIPortConnectSource(input_, source_, nullptr), "MIDIPortConnectSource");
        }
    }

    ~CoreMidiPort() override {
        if (input_) {
            MIDIPortDisconnectSource(input_, source_);
            MIDIPortDispose(input_);
        }
        if (output_) MIDIPortDispose(output_);
        MIDIClientDispose(client_);
    }

    void send(const Bytes& sysex) override {
        Byte buffer[1024];
        MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buffer);
        MIDIPacket* packet = MIDIPacketListInit(list);
        packet = MIDIPacketListAdd(list, sizeof buffer, packet, 0, sysex.size(), sysex.data());
        if (!packet) throw std::runtime_error("Message too long for one MIDI packet list.");
        check(MIDISend(output_, destination_, list), "MIDISend");
    }

private:
    static void check(OSStatus status, const char* call) {
        if (status != noErr) throw std::runtime_error(std::string(call) + " failed with OSStatus " + std::to_string(status));
    }

    // Runs on a CoreMIDI thread: never let an exception escape into native code.
    static void readProc(const MIDIPacketList* list, void* refCon, void*) {
        auto* self = static_cast<CoreMidiPort*>(refCon);
        try {
            const MIDIPacket* packet = &list->packet[0];
            for (UInt32 i = 0; i < list->numPackets; ++i) {
                for (const Bytes& message : self->assembler_.feed(packet->data, packet->length)) self->deliverAny(message);
                packet = MIDIPacketNext(packet);
            }
        } catch (...) {
            self->assembler_.reset();
        }
    }

    MIDIClientRef client_ = 0;
    MIDIPortRef input_ = 0, output_ = 0;
    MIDIEndpointRef source_ = 0, destination_ = 0;
    MessageParser assembler_;
};

// A pair of CoreMIDI virtual endpoints with one name: a source that the program writes to and a destination that it listens to.
class CoreVirtualCore : public VirtualCore {
public:
    CoreVirtualCore(const std::string& name, std::string& error) {
        CFStringRef text = CFStringCreateWithCString(nullptr, name.c_str(), kCFStringEncodingUTF8);
        OSStatus status = MIDIClientCreate(text, nullptr, nullptr, &client_);
        if (status == noErr) status = MIDISourceCreate(client_, text, &source_);
        if (status == noErr) status = MIDIDestinationCreate(client_, text, &CoreVirtualCore::readProc, this, &destination_);
        CFRelease(text);
        if (status != noErr) {
            error = "The port \"" + name + "\" could not be created (OSStatus " + std::to_string(status) + ").";
            cleanup();
        }
    }

    ~CoreVirtualCore() override { cleanup(); }

    bool ok() const { return client_ != 0; }

    void write(const Bytes& message) override {
        if (message.empty()) return;
        std::vector<Byte> buffer(message.size() + 256);
        MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buffer.data());
        MIDIPacket* packet = MIDIPacketListInit(list);
        packet = MIDIPacketListAdd(list, buffer.size(), packet, 0, message.size(), message.data());
        if (!packet) throw std::runtime_error("Message too long for one MIDI packet list.");
        OSStatus status = MIDIReceived(source_, list);
        if (status != noErr) throw std::runtime_error("MIDIReceived failed with OSStatus " + std::to_string(status));
    }

private:
    void cleanup() {
        if (destination_) MIDIEndpointDispose(destination_);
        if (source_) MIDIEndpointDispose(source_);
        if (client_) MIDIClientDispose(client_);
        destination_ = 0;
        source_ = 0;
        client_ = 0;
    }

    static void readProc(const MIDIPacketList* list, void* refCon, void*) {
        auto* self = static_cast<CoreVirtualCore*>(refCon);
        try {
            const MIDIPacket* packet = &list->packet[0];
            for (UInt32 i = 0; i < list->numPackets; ++i) {
                for (const Bytes& message : self->assembler_.feed(packet->data, packet->length)) self->received(message);
                packet = MIDIPacketNext(packet);
            }
        } catch (...) {
            self->assembler_.reset();
        }
    }

    MIDIClientRef client_ = 0;
    MIDIEndpointRef source_ = 0, destination_ = 0;
    MessageParser assembler_;
};

}  // namespace

std::unique_ptr<VirtualCore> makeVirtualCore(const std::string& name, std::string& error) {
    auto core = std::make_unique<CoreVirtualCore>(name, error);
    if (!core->ok()) return nullptr;
    return core;
}

bool virtualCoreAvailable(std::string&) { return true; }

std::vector<EndpointInfo> listEndpoints() {
    std::vector<EndpointInfo> result;
    for (int pass = 0; pass < 2; ++pass) {
        bool isSource = pass == 0;
        ItemCount count = isSource ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < count; ++i) {
            MIDIEndpointRef endpoint = isSource ? MIDIGetSource(i) : MIDIGetDestination(i);
            MIDIEntityRef entity = 0;
            MIDIEndpointGetEntity(endpoint, &entity);
            MIDIDeviceRef device = 0;
            if (entity) MIDIEntityGetDevice(entity, &device);
            EndpointInfo info;
            info.ref = endpoint;
            info.isSource = isSource;
            info.index = static_cast<int>(i);
            info.name = stringProperty(endpoint, kMIDIPropertyName);
            info.displayName = stringProperty(endpoint, kMIDIPropertyDisplayName);
            info.entity = stringProperty(entity, kMIDIPropertyName);
            info.device = stringProperty(device, kMIDIPropertyName);
            info.manufacturer = stringProperty(endpoint, kMIDIPropertyManufacturer);
            info.model = stringProperty(endpoint, kMIDIPropertyModel);
            info.entityRef = entity;
            result.push_back(info);
        }
    }
    return result;
}

std::unique_ptr<MidiPort> openPort(const PortPair& pair, bool) { return std::make_unique<CoreMidiPort>(pair); }

std::unique_ptr<MidiPort> openOutputPort(const EndpointInfo& destination) { return std::make_unique<CoreMidiPort>(destination, true); }

std::unique_ptr<MidiPort> openInputPort(const EndpointInfo& source) { return std::make_unique<CoreMidiPort>(source, false); }

}  // namespace joue

#endif  // __APPLE__
