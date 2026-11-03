#ifdef __APPLE__
#include <stdexcept>

#include "joue/endpoints.hpp"
#include "joue/midiparse.hpp"

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

    ~CoreMidiPort() override {
        MIDIPortDisconnectSource(input_, source_);
        MIDIPortDispose(input_);
        MIDIPortDispose(output_);
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
    MIDIEndpointRef source_, destination_;
    MessageParser assembler_;
};

}  // namespace

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

}  // namespace joue

#endif  // __APPLE__
